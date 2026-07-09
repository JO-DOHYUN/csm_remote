import serial
import time
import argparse
from datetime import datetime

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="")
    ap.add_argument("--seconds", type=int, default=0, help="0이면 수동 종료(Ctrl+C)")
    args = ap.parse_args()

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    out = args.out or f"canstream_{ts}.bin"

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    ser.reset_input_buffer()

    print(f"[LOG] port={args.port} -> {out}")
    start = time.time()
    total = 0
    last = start

    with open(out, "wb") as f:
        try:
            while True:
                chunk = ser.read(8192)
                if chunk:
                    f.write(chunk)
                    total += len(chunk)

                now = time.time()
                if now - last >= 1.0:
                    rate = total / (now - start + 1e-9)
                    print(f"[LOG] {total} bytes  ({rate/1024:.1f} KB/s)")
                    last = now

                if args.seconds and (now - start) >= args.seconds:
                    break

        except KeyboardInterrupt:
            pass

    ser.close()
    print(f"[DONE] wrote {total} bytes to {out}")

if __name__ == "__main__":
    main()