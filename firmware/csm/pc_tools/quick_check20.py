import serial, time

PORT="COM6"
BAUD=115200

def crc8_atm(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

s = serial.Serial(PORT, BAUD, timeout=0.2)
s.reset_input_buffer()

buf = bytearray()
ok = 0
t0 = time.time()

while time.time() - t0 < 3.0:
    buf += s.read(4096)
    i = 0
    while len(buf) - i >= 20:
        p = buf[i:i+20]
        if crc8_atm(p[:19]) == p[19]:
            ok += 1
            del buf[:i+20]
            i = 0
        else:
            i += 1

s.close()
print("ok_packets =", ok, "bytes_left =", len(buf))