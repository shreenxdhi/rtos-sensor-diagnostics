import argparse
import json
import math
import time
from device import Device
from protocol import Frame, SAMPLE, envelope

def main():
    p = argparse.ArgumentParser(description='Generate reproducible synthetic readings and print firmware status JSON.')
    p.add_argument("--elf", default="build/firmware.elf")
    p.add_argument("--qemu", default="qemu-system-arm")
    p.add_argument("--seconds", type=float, default=10)
    p.add_argument("--hz", type=float, default=25, help="samples per channel per host second")
    p.add_argument("--faults", action="store_true", help="inject CRC errors and source dropouts")
    a = p.parse_args()
    if not 0 < a.hz <= 500 or not 0 < a.seconds <= 3600:
        p.error("use 0 < hz <= 500 and 0 < seconds <= 3600")
    with Device(a.elf, a.qemu) as d:
        start = next_due = time.monotonic()
        seq, last_printed = 0, 0
        while time.monotonic() - start < a.seconds:
            now = time.monotonic()
            t = now - start
            if now >= next_due:
                seq = (seq + 1) & 0xFFFF
                if not (a.faults and 2 <= t % 5 < 3):
                    for ch, value in ((0, 2800 + int(500 * math.sin(t))),
                                      (1, 10500 + int(2000 * math.sin(t / 2)))):
                        f = Frame(SAMPLE, ch, seq, value)
                        if a.faults and seq % 17 == 0:
                            raw = bytearray(f.body()); raw[-1] ^= 1
                            d.raw(envelope(raw))
                        else:
                            d.send(f)
                next_due += 1 / a.hz
                if next_due < now:
                    next_due = now + 1 / a.hz
            with d.condition:
                pending = [(i, e) for i, e in d.events if i > last_printed]
            for index, event in pending:
                if event["event"] in ("boot", "state", "status", "fatal"):
                    print(json.dumps(event), flush=True)
                last_printed = index
            time.sleep(0.001)
        print(json.dumps(d.status()), flush=True)

if __name__ == "__main__":
    main()
