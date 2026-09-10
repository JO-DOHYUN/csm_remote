"""Canonical observation generator/codec proof, not runtime hook or HIL coverage."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
GENERATOR = PROJECT / 'tools/generate_observability.py'
CONTRACT = PROJECT / 'shared/observability/contract.json'


def module_at(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class ObservationContractTest(unittest.TestCase):
    def test_required_generation_entrypoint(self):
        result = subprocess.run([sys.executable, str(GENERATOR), '--check'],
                                capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def setUp(self):
        if GENERATOR.exists():
            self.gen = module_at(GENERATOR)
            self.schema = json.loads(CONTRACT.read_text(encoding='utf-8'))

    def test_bad_id_type_length_endianness_rejected(self):
        for key, value in [('id', 0), ('id', 65536)]:
            broken = copy.deepcopy(self.schema)
            broken['events'][0][key] = value
            with self.assertRaises(ValueError):
                self.gen.validate(broken)
        broken = copy.deepcopy(self.schema)
        broken['events'][1]['id'] = broken['events'][0]['id']
        with self.assertRaises(ValueError):
            self.gen.validate(broken)
        for typename in ['float', 'bytes0', 'bytes513']:
            broken = copy.deepcopy(self.schema)
            broken['events'][0]['fields'][0]['type'] = typename
            with self.assertRaises(ValueError):
                self.gen.validate(broken)
        broken = copy.deepcopy(self.schema)
        broken['byte_order'] = 'big'
        with self.assertRaises(ValueError):
            self.gen.validate(broken)
        broken = copy.deepcopy(self.schema)
        broken['events'][0]['fields'][0]['type'] = 'bytes512'
        with self.assertRaisesRegex(ValueError, 'payload limit'):
            self.gen.validate(broken)

    def test_stale_generated_output_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            args = [sys.executable, str(GENERATOR), '--output', directory]
            self.assertEqual(0, subprocess.run(args, capture_output=True).returncode)
            path = Path(directory) / 'observation.py'
            path.write_text(path.read_text(encoding='utf-8') + '# stale\n', encoding='utf-8')
            result = subprocess.run(args + ['--check'], capture_output=True, text=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn('stale', result.stderr)

    def test_canonical_golden_all_events(self):
        codec = module_at(PROJECT / 'shared/observability/generated/observation.py')
        vectors = json.loads((PROJECT / 'shared/observability/golden.json').read_text())
        self.assertEqual(set(codec.EVENTS), {item['id'] for item in vectors})
        for vector in vectors:
            with self.subTest(event=vector['id']):
                event = codec.CLASSES[vector['id']](**{
                    k: bytes.fromhex(v['hex']) if isinstance(v, dict) else v
                    for k, v in vector['values'].items()})
                meta = codec.Meta(**vector['meta'])
                raw = codec.encode(meta, event)
                self.assertEqual(vector['hex'], raw.hex())
                decoded_meta, decoded_event = codec.decode(raw)
                self.assertEqual(meta, decoded_meta)
                self.assertEqual(event, decoded_event)

    def test_frame_corruption_and_schema_hash_rejected(self):
        codec = module_at(PROJECT / 'shared/observability/generated/observation.py')
        vector = json.loads((PROJECT / 'shared/observability/golden.json').read_text())[0]
        raw = bytes.fromhex(vector['hex'])
        for offset in [0, 2, 3, 4, 5, 7, 9, 12, 13, 45, 47, 49, len(raw)-1]:
            mutated = bytearray(raw)
            mutated[offset] ^= 0x80
            with self.assertRaises(ValueError, msg=str(offset)):
                codec.decode(mutated)
        for offset in [3, 4, 5, 9, 10, 11, 12, 13, 45, 47, 49]:
            mutated = bytearray(raw)
            mutated[offset] ^= 0x80
            mutated[-2:] = codec.crc16(mutated[2:-2]).to_bytes(2, 'little')
            with self.assertRaises(ValueError, msg=str(offset)):
                codec.decode(mutated)
        for invalid in [raw[:-1], raw + b'\x00']:
            with self.assertRaises(ValueError):
                codec.decode(invalid)

    def test_registry_changes_invalidate_hash(self):
        original = self.gen.schema_hash(self.schema)
        altered = copy.deepcopy(self.schema)
        altered['events'][0]['relation'] += '.changed'
        self.assertNotEqual(original, self.gen.schema_hash(altered))
        self.assertEqual(original, self.gen.schema_hash(json.loads(json.dumps(self.schema))))

    def test_android_epoch_preserves_64_bit_owner_identity(self):
        codec = module_at(PROJECT / 'shared/observability/generated/observation.py')
        # AndroidVsmNetworkManager / NetworkEpochLease use Long, not a u32 wire ID.
        meta = codec.Meta(16, 3, 1, 0x100000001, 0, 1)
        event = codec.Intent(1, 1, 0, 0, 0, 0)
        decoded, _ = codec.decode(codec.encode(meta, event))
        self.assertEqual(0x100000001, decoded.epoch)

    def test_binding_implementation_is_not_runtime_coverage(self):
        bindings = json.loads((PROJECT.parents[1] / 'tools/observability/bindings.json').read_text())
        self.gen.validate_bindings(self.schema, bindings, PROJECT.parents[1])
        self.assertFalse(any(b.get('runtime_coverage', False) for b in bindings['bindings']))
        for field, value in [('symbol', 'nonexistent_observation_owner_anchor'),
                             ('runtime_coverage', True), ('status', 'invented'),
                             ('owner_test', '../escaped-test.cpp'), ('owner_test_scope', '')]:
            broken = copy.deepcopy(bindings)
            broken['bindings'][0][field] = value
            with self.assertRaises(ValueError):
                self.gen.validate_bindings(self.schema, broken, PROJECT.parents[1])


if __name__ == '__main__':
    unittest.main()
