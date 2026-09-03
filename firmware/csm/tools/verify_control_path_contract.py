"""L2 diagnostic wire/source/decoder proof (no device or network access)."""
from pathlib import Path
import re
import struct
import sys

project = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(project / "pc_tools"))
from control_path_diagnostic import decode_control_path_diagnostic

header = (project / "include/protocol/TypedRecords.h").read_text(encoding="utf-8")
main = (project / "src/main.cpp").read_text(encoding="utf-8")
offsets = {name: int(value) for name, value in re.findall(
    r"kControlPathDiagnostic(\w+)Offset = (\d+);", header)
    if name not in ("MonoUs", "Schema")}
assert sorted(offsets.values()) == list(range(12, 340, 4))
assert len(offsets) == 82
payload = bytearray(340)
payload[8] = 1
for name, offset in offsets.items():
    assert f"payload + csm::kControlPathDiagnostic{name}Offset" in main, name
    struct.pack_into("<I", payload, offset, 0x80000000 + offset)
decoded = decode_control_path_diagnostic(payload)
assert decoded is not None
for name, offset in offsets.items():
    assert decoded[name] == 0x80000000 + offset
assert decode_control_path_diagnostic(payload[:-1]) is None
payload[8] = 0
assert decode_control_path_diagnostic(payload) is None
assert "now_ms - last_emit_ms < 1000u" in main
assert "BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY && BOARD_ENABLE_WIFI_UPLINK" in main
print("PASS: 82 canonical control-path fields, bounded producer, strict decoder")
