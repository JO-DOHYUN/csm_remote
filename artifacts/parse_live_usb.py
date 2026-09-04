import serial
import time

s = serial.Serial("COM7", 115200, timeout=0.1)
s.dtr = True
buffer = bytearray()
deadline = time.monotonic() + 30
print("USB_CAPTURE_START", flush=True)
while time.monotonic() < deadline:
    data = s.read(8192)
    if data:
        buffer.extend(data)
    while True:
        start = buffer.find(bytes((0xA5, 0x5A)))
        if start < 0:
            buffer.clear()
            break
        if start:
            del buffer[:start]
        if len(buffer) < 9:
            break
        payload_length = buffer[7] | (buffer[8] << 8)
        frame_length = 11 + payload_length
        if len(buffer) < frame_length:
            break
        frame = bytes(buffer[:frame_length])
        del buffer[:frame_length]
        if frame[3] == 6 and payload_length == 28:
            payload = frame[9:37]
            command_id = int.from_bytes(payload[8:12], "little")
            counter = int.from_bytes(payload[20:24], "little")
            rejected_total = int.from_bytes(payload[24:28], "little")
            print(
                f"{time.monotonic():.3f} ACK cmd={command_id} "
                f"status={payload[12]} reason={payload[13]} "
                f"counter={counter} rejectedTotal={rejected_total}",
                flush=True,
            )
print("USB_CAPTURE_END", flush=True)
