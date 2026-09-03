"""Read-only Service/HIL evidence decoder; offsets come from canonical headers."""
import re
import struct
from pathlib import Path

_header = (Path(__file__).resolve().parents[1] / "include/protocol/TypedRecords.h").read_text(encoding="utf-8")
_constants = {name: int(value, 0) for name, value in re.findall(
    r"kControlPathDiagnostic(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;", _header)}


def decode_control_path_diagnostic(payload: bytes) -> dict | None:
    if len(payload) != _constants["PayloadLen"] or payload[8] != _constants["Schema"]:
        return None
    result = {"mono_us": struct.unpack_from("<Q", payload)[0]}
    for name, offset in _constants.items():
        if name.endswith("Offset") and name not in ("MonoUsOffset", "SchemaOffset"):
            result[name[:-6]] = struct.unpack_from("<I", payload, offset)[0]
    return result
