import struct


SOF = b"\xA5\x5A"
VERSION = 1


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_typed_frame(record_type: int, sequence: int, payload: bytes) -> bytes:
    body = struct.pack(
        "<BBBHH", VERSION, record_type, 0, sequence & 0xFFFF, len(payload)
    ) + payload
    return SOF + body + struct.pack("<H", crc16_ccitt(body))
