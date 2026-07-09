import serial
import struct
import time

# CRC-8/ATM (poly 0x07, init 0x00, refin/refout false)
def crc8_atm(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def u32le(b): return struct.unpack_from("<I", b, 0)[0]
def u16le(b): return struct.unpack_from("<H", b, 0)[0]

PORT = "COM6"      # ★ 본인 COM으로 변경
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=0.1)
ser.reset_input_buffer()

buf = bytearray()
ok = 0
bad = 0

# 1초마다 FRAME는 3개만 출력(과출력 방지)
frame_print_budget = 0
last_budget_time = time.time()

while True:
    buf += ser.read(4096)

    i = 0
    while len(buf) - i >= 20:
        pkt = buf[i:i+20]

        # CRC로 20B 경계 맞추기(resync)
        if crc8_atm(pkt[:19]) == pkt[19]:
            if i > 0:
                del buf[:i]
            pkt = bytes(buf[:20])
            del buf[:20]
            i = 0

            ok += 1

            t_us = u32le(pkt[0:4])
            b17 = pkt[17]
            seq = pkt[18]
            ptype = (b17 >> 7) & 1  # 0=frame, 1=stats

            # 매초 frame 3개만 출력
            now = time.time()
            if now - last_budget_time >= 1.0:
                frame_print_budget = 3
                last_budget_time = now

            # FRAME 출력(초당 3개)
            if ptype == 0 and frame_print_budget > 0:
                can_id_flags = u32le(pkt[4:8])
                can_id = can_id_flags & 0x1FFFFFFF
                ext = (can_id_flags >> 29) & 1
                rtr = (can_id_flags >> 30) & 1
                dlc = pkt[8] & 0x0F
                data = pkt[9:17]
                print(f"[FRAME] ok={ok} bad={bad} seq={seq} t_us={t_us} id=0x{can_id:X} ext={ext} rtr={rtr} dlc={dlc} data={data.hex(' ')}")
                frame_print_budget -= 1

            # STATS 출력(들어올 때마다 = 보통 1초마다 1번)
            if ptype == 1:
                dropped = u32le(pkt[4:8])
                ovf = u32le(pkt[8:12])
                rx_fps = u16le(pkt[12:14])
                tx_fps = u16le(pkt[14:16])
                errp = pkt[16]
                busoff = pkt[17] & 0x7F
                print(f"[STATS] ok={ok} bad={bad} seq={seq} t_us={t_us} dropped={dropped} ovf={ovf} rx_fps={rx_fps} tx_fps={tx_fps} errp={errp} busoff={busoff}")

        else:
            i += 1

    # 버퍼 폭주 방지
    if len(buf) > 200000:
        buf = buf[-4000:]
        bad += 1