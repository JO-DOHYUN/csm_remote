"""Generate observation codecs only. Never generates or authorizes control behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[1]
SOURCE = PROJECT / 'shared/observability/contract.json'
OUTPUT = PROJECT / 'shared/observability/generated'
TEMPLATES = Path(__file__).parent / 'observability_templates'
TYPES = {'u8': (1, 'uint8_t', 'Int', 'B'), 'u16': (2, 'uint16_t', 'Int', 'H'),
         'u32': (4, 'uint32_t', 'Long', 'I'), 'u64': (8, 'uint64_t', 'ULong', 'Q'),
         'i32': (4, 'int32_t', 'Int', 'i')}


def type_info(name):
    if name in TYPES:
        return TYPES[name]
    if re.fullmatch(r'bytes[1-9][0-9]*', name):
        count = int(name[5:])
        if count <= 512:
            return count, 'bytes', 'ByteArray', str(count) + 's'
    raise ValueError('unsupported field type: ' + name)


def schema_hash(schema):
    return hashlib.sha256(json.dumps(schema, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=True).encode()).hexdigest()


def layout(event):
    offset = 0
    result = []
    for field in event['fields']:
        size, cpp, kotlin, fmt = type_info(field['type'])
        result.append(dict(field, offset=offset, bytes=size, cpp=cpp, kotlin=kotlin, fmt=fmt))
        offset += size
    return result


def validate(schema):
    if schema['schema_version'] != 1 or schema['byte_order'] != 'little':
        raise ValueError('unsupported version/endianness')
    transport = schema['transport']
    if transport['header_bytes'] != 72 or transport['endian_tag'] != 0x01020304:
        raise ValueError('unsupported observation envelope')
    if not 1 <= transport['record_type'] <= 255:
        raise ValueError('invalid record type')
    if transport['allowed_sinks'] != ['usb_debug', 'android_local_file']:
        raise ValueError('debug sink scope changed')
    def unique(items, key):
        values = [x[key] for x in items]
        if len(values) != len(set(values)):
            raise ValueError('duplicate ' + key)
    unique(schema['producers'], 'id')
    unique(schema['clocks'], 'id')
    producers = {x['id'] for x in schema['producers']}
    clocks = {x['id'] for x in schema['clocks']}
    for producer in schema['producers']:
        if not 1 <= producer['id'] <= 255 or not set(producer['clocks']) <= clocks:
            raise ValueError('invalid producer clocks')
    for clock in schema['clocks']:
        if not 1 <= clock['id'] <= 255 or clock['width_bits'] not in (32, 64):
            raise ValueError('invalid clock')
    unique(schema['events'], 'id')
    unique(schema['events'], 'name')
    masks = list(schema['admission_valid_fields'].values())
    if len(set(masks)) != len(masks) or any(m <= 0 or m > 0x80000000 or m & (m-1) for m in masks):
        raise ValueError('invalid admission validity masks')
    for event in schema['events']:
        if not 1 <= event['id'] <= 65535 or event['id'] in schema['retired_event_ids']:
            raise ValueError('invalid/retired event ID')
        if not re.fullmatch('[A-Z][A-Za-z0-9]*', event['name']):
            raise ValueError('invalid event name')
        if event['class'] != 'DEBUG_TRACE' or not event['producers'] or not set(event['producers']) <= producers:
            raise ValueError('invalid event ownership/class')
        if not event['relation'] or not event['authority_ref']:
            raise ValueError('missing relation or authority')
        unique(event['fields'], 'name')
        for field in event['fields']:
            if not re.fullmatch('[a-z][a-z0-9_]*', field['name']) or not field['unit']:
                raise ValueError('invalid field name/unit')
        if sum(x['bytes'] for x in layout(event)) + transport['header_bytes'] > 512:
            raise ValueError('event exceeds typed payload limit')


def validate_bindings(schema, bindings, root):
    ids = {x['id'] for x in schema['events']}
    seen = set()
    for binding in bindings['bindings']:
        if binding['id'] in seen:
            raise ValueError('duplicate binding')
        seen.add(binding['id'])
        path = (root / binding['path']).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            raise ValueError('missing/escaped binding path: ' + binding['path'])
        if not binding['symbol'] or binding['symbol'] not in path.read_text(encoding='utf-8'):
            raise ValueError('missing binding anchor: ' + binding['id'])
        if not set(binding['event_ids']) <= ids:
            raise ValueError('unknown binding event')
        if binding['status'] not in ['unimplemented', 'implemented', 'existing_summary', 'not_applicable']:
            raise ValueError('unknown implementation status')
        if binding.get('runtime_coverage') is not False:
            raise ValueError('source bindings cannot grant runtime coverage')
        if binding['status'] == 'implemented':
            # Static location/proof routing ONLY. No test execution or HIL verdict inferred here.
            if 'OBS_BOUNDARY:'+binding['id'] not in path.read_text(encoding='utf-8'):
                raise ValueError('missing implemented boundary marker')
            test = (root / binding.get('owner_test', '')).resolve()
            if not test.is_relative_to(root.resolve()) or not test.is_file() or not binding.get('owner_test_scope'):
                raise ValueError('implemented boundary requires scoped owner proof route')
            if binding.get('owner_test_status') not in ['PASS', 'FAIL', 'NOT_RUN']:
                raise ValueError('unknown owner execution status')
        for event in schema['events']:
            if event['id'] in binding['event_ids'] and binding['producer'] not in event['producers']:
                raise ValueError('binding producer mismatch')


def wire(schema):
    text = (ROOT / schema['transport']['typed_frame_header']).read_text(encoding='utf-8')
    def number(name):
        return int(re.search(r'\b' + name + r'\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)', text)[1], 0)
    existing_ids = {int(x) for x in re.findall(r'^\s+[A-Za-z0-9_]+\s*=\s*([0-9]+),', text, re.M)}
    if schema['transport']['record_type'] in existing_ids:
        raise ValueError('debug record collides with existing typed record')
    if number('kMaxPayloadLen') != 512 or number('kTypedFrameOverheadLen') != 11:
        raise ValueError('typed frame constraint changed')
    return dict(sof0=number('kFrameSof0'), sof1=number('kFrameSof1'),
                version=number('kProtocolVersion'), max_payload=number('kMaxPayloadLen'))


def cpp_event(event):
    fields = layout(event)
    lines = ['struct ' + event['name'] + ' {',
             '  static constexpr uint16_t id = ' + str(event['id']) + ';',
             '  static constexpr size_t body_size = ' + str(sum(x['bytes'] for x in fields)) + ';']
    for f in fields:
        lines.append('  ' + ('uint8_t ' + f['name'] + '[' + str(f['bytes']) + ']{};'
                            if f['cpp'] == 'bytes' else f['cpp'] + ' ' + f['name'] + '{};'))
    lines.append('  void write(uint8_t* p) const {')
    for f in fields:
        lines.append('    ' + ('memcpy(p + '+str(f['offset'])+', '+f['name']+', '+str(f['bytes'])+');'
                     if f['cpp'] == 'bytes' else 'put(p + '+str(f['offset'])+', static_cast<uint64_t>('+f['name']+'), '+str(f['bytes'])+');'))
    lines += ['  }', '  static bool read(const View& v, '+event['name']+'& out) {',
              '    if (v.event_id != id || v.body_length != body_size) return false;']
    for f in fields:
        if f['cpp'] == 'bytes':
            lines.append('    memcpy(out.'+f['name']+', v.body + '+str(f['offset'])+', '+str(f['bytes'])+');')
        else:
            expr = 'get(v.body + '+str(f['offset'])+', '+str(f['bytes'])+')'
            if f['type'] == 'i32':
                expr = 'signed32(static_cast<uint32_t>('+expr+'))'
            lines.append('    out.'+f['name']+' = static_cast<'+f['cpp']+'>('+expr+');')
    lines += ['    return true;', '  }', '};']
    return '\n'.join(lines)


def kotlin_event(event):
    fields = layout(event)
    lines = ['    data class '+event['name']+'(']
    lines += ['        val '+f['name']+': '+f['kotlin']+',' for f in fields]
    lines += ['    ) : Event {', '        override val id: Int get() = '+str(event['id']),
              '        override fun write(out: ByteArray, base: Int) {']
    for f in fields:
        n=f['name']
        if f['type'].startswith('bytes'):
            lines += ['            require('+n+'.size == '+str(f['bytes'])+')',
                      '            '+n+'.copyInto(out, base + '+str(f['offset'])+')']
        else:
            if f['type'] != 'u64' and f['type'] != 'i32':
                maxv={'u8':'255', 'u16':'65535', 'u32':'4294967295L'}[f['type']]
                lines.append('            require('+n+' in '+('0L' if f['type']=='u32' else '0')+'..'+maxv+')')
            value = n if f['type'] == 'u64' else n+'.toULong()'
            lines.append('            put(out, base + '+str(f['offset'])+', '+value+', '+str(f['bytes'])+')')
    lines += ['        }', '    }']
    return '\n'.join(lines)


def kotlin_writer(event):
    # Same canonical field encoder as the offline Event; no field/offset copy.
    fields = layout(event)
    body = kotlin_event(event).split('override fun write(out: ByteArray, base: Int) {', 1)[1].rsplit('        }', 1)[0]
    if event['name'] == 'DatagramBytes':
        body = body.replace('require(data.size == 128)',
            'require(captured_length in 0..128 && dataOffset >= 0 && dataOffset + captured_length <= data.size)')
        body = body.replace('data.copyInto(out, base + 16)',
            'out.fill(0, base + 16, base + 144)\n            data.copyInto(out, base + 16, dataOffset, dataOffset + captured_length)')
    parameters = ', '.join(f['name']+': '+f['kotlin'] for f in fields)
    if event['name'] == 'DatagramBytes': parameters += ', dataOffset: Int = 0'
    return ('        fun '+event['name']+'('+parameters+') {\n'
            '            require(id == 0)\n            id = '+str(event['id'])+'\n'
            '            val out = bytes\n            val base = 81'+body+'        }')


def render(schema):
    validate(schema)
    w = wire(schema)
    digest = schema_hash(schema)
    events = schema['events']
    header_fields = [
        ('schema_version','u8',0),('producer','u8',1),('clock','u8',2),('reserved','u8',3),
        ('endian_tag','u32',4),('trace_sequence','u32',8),('epoch','u64',12),
        ('boot_id','u64',20),('ticks','u64',28),('event_id','u16',36),('body_length','u16',38),
        ('schema_sha256','bytes32',40)]
    manifest = dict(schema_sha256=digest, schema_version=1, profile=schema['profile'],
                    runtime_coverage='NOT_PROVEN', sinks=schema['transport']['allowed_sinks'],
                    record_type=schema['transport']['record_type'], header_bytes=72,
                    header=[dict(name=n,type=t,offset=o) for n,t,o in header_fields],
                    typed_frame=w, producers=schema['producers'], clocks=schema['clocks'],
                    stages=schema['stages'], admission_valid_fields=schema['admission_valid_fields'],
                    events=[dict(e, body_bytes=sum(f['bytes'] for f in layout(e)), fields=layout(e)) for e in events])
    constants = {'@HASH@':digest, '@RECORD@':str(schema['transport']['record_type']),
                 '@SOF0@':str(w['sof0']), '@SOF1@':str(w['sof1']), '@VERSION@':str(w['version'])}
    groups = {'stage': schema['stages'],
              'producer': {p['name']: p['id'] for p in schema['producers']},
              'clock': {c['name']: c['id'] for c in schema['clocks']},
              'admission_field': schema['admission_valid_fields']}
    cpp_constants = '\n'.join('namespace '+group+' {\n'+
        '\n'.join('static constexpr uint32_t '+name+' = '+str(value)+'u;' for name,value in values.items())+
        '\n}' for group,values in groups.items())
    kt_constants = '\n'.join('    object '+group.title().replace('_','')+' {\n'+
        '\n'.join('        const val '+name.upper()+': '+('Long' if group=='admission_field' else 'Int')+
                  ' = '+str(value)+('L' if group=='admission_field' else '') for name,value in values.items())+
        '\n    }' for group,values in groups.items())
    def fill(name, replacements):
        content = (TEMPLATES / name).read_text(encoding='utf-8')
        for key,value in dict(constants, **replacements).items():
            content = content.replace(key,value)
        return content.rstrip() + '\n'
    c_cases='\n'.join('    case '+str(e['id'])+': return '+str(sum(f['bytes'] for f in layout(e)))+';' for e in events)
    c_owner='\n'.join('    case '+str(e['id'])+': return '+ ' || '.join('p == '+str(p) for p in e['producers'])+';' for e in events)
    c_clocks='\n'.join('    case '+str(p['id'])+': return '+' || '.join('c == '+str(c) for c in p['clocks'])+';' for p in schema['producers'])
    cpp=fill('codec.h.in', {'@CONSTANTS@':cpp_constants,'@SIZES@':c_cases,'@OWNERS@':c_owner,'@CLOCKS@':c_clocks,
                            '@HASH_BYTES@':','.join('0x'+digest[i:i+2] for i in range(0,64,2)),
                            '@EVENTS@':'\n\n'.join(cpp_event(e) for e in events)})
    py_classes='\n\n'.join('@dataclass(frozen=True)\nclass '+e['name']+':\n    id: ClassVar[int] = '+str(e['id'])+'\n'+
        '\n'.join('    '+f['name']+': '+('bytes' if f['type'].startswith('bytes') else 'int') for f in e['fields']) for e in events)
    py=fill('codec.py.in',{'@CONSTANTS@':'\n'.join(group.upper()+' = '+repr(values) for group,values in groups.items()),
                          '@MANIFEST@':repr(manifest),'@CLASSES@':py_classes,
                          '@CLASS_MAP@':repr({e['id']:e['name'] for e in events})})
    kdecode=[]
    for e in events:
        values=[]
        for f in layout(e):
            base='start + '+str(f['offset'])
            if f['type'].startswith('bytes'):
                expr='bytes.copyOfRange('+base+', '+base+' + '+str(f['bytes'])+')'
            else:
                expr='get(bytes, '+base+', '+str(f['bytes'])+')'+('' if f['type']=='u64' else '.toLong()' if f['type']=='u32' else '.toInt()')
            values.append(expr)
        kdecode.append('            '+str(e['id'])+' -> '+e['name']+'('+', '.join(values)+')')
    kt=fill('codec.kt.in', {
        '@CONSTANTS@':kt_constants,
        '@SIZES@':'\n'.join('        '+str(e['id'])+' -> '+str(sum(f['bytes'] for f in layout(e))) for e in events),
        '@OWNERS@':'\n'.join('        '+str(e['id'])+' -> '+' || '.join('p == '+str(p) for p in e['producers']) for e in events),
        '@CLOCKS@':'\n'.join('        '+str(p['id'])+' -> '+' || '.join('c == '+str(c) for c in p['clocks']) for p in schema['producers']),
        '@HASH_BYTES@':', '.join(str(int(digest[i:i+2],16)-256 if int(digest[i:i+2],16)>127 else int(digest[i:i+2],16)) for i in range(0,64,2)),
        '@EVENTS@':'\n\n'.join(kotlin_event(e) for e in events),
        '@WRITERS@':'\n\n'.join(kotlin_writer(e) for e in events),
        '@DECODE@':'\n'.join(kdecode)})
    products = {'Observation.h':cpp,'CanonicalObservation.kt':kt,'observation.py':py}
    manifest['generated_sha256'] = {name: hashlib.sha256(text.encode()).hexdigest()
                                    for name, text in products.items()}
    products['manifest.json'] = json.dumps(manifest,indent=2,ensure_ascii=True)+'\n'
    return products


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--contract',type=Path,default=SOURCE)
    parser.add_argument('--output',type=Path,default=OUTPUT)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--android-root',type=Path)
    args=parser.parse_args()
    schema=json.loads(args.contract.read_text(encoding='utf-8'))
    products=render(schema)
    validate_bindings(schema,json.loads((ROOT/'tools/observability/bindings.json').read_text()),ROOT)
    if args.android_root:
        validate_bindings(schema,json.loads((args.android_root/'tools/observability/bindings.json').read_text()),args.android_root)
    for name,text in products.items():
        path=args.output/name
        if args.check:
            if not path.is_file() or path.read_bytes()!=text.encode('utf-8'):
                raise ValueError('missing/stale generated output: '+str(path))
        else:
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(text.encode('utf-8'))
    print('PASS: observation schema/generation; runtime hooks NOT_PROVEN; sha256='+schema_hash(schema))


if __name__=='__main__':
    try:
        main()
    except (ValueError,KeyError,TypeError) as error:
        print('FAIL: '+str(error),file=sys.stderr)
        sys.exit(1)
