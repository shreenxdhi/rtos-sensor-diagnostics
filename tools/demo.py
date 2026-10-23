import argparse
import json
from device import Device
from protocol import Frame, SAMPLE, REPORT_MS

def show(label, event):
    print(f"\n{label}")
    print(json.dumps(event, indent=2))

def main():
    p = argparse.ArgumentParser(description='A short readable demonstration: normal, warning, stale, recovered.')
    p.add_argument("--elf", default="build/firmware.elf")
    p.add_argument("--qemu", default="qemu-system-arm")
    args = p.parse_args()
    with Device(args.elf, args.qemu) as d:
        d.command(REPORT_MS, 100)
        d.send(Frame(SAMPLE, 0, 1, 2500))
        show("Normal: synthetic temperature 25.00", d.until_status(
            lambda s: s["sensors"][0]["accepted"] == 1))
        d.send(Frame(SAMPLE, 0, 2, 3200))
        show("Warning: threshold crossed", d.until_status(
            lambda s: s["sensors"][0]["sequence"] == 2))
        show("Unavailable: source stopped sending", d.until_status(
            lambda s: s["sensors"][0]["accepted"] == 2 and s["sensors"][0]["state"] == "unavailable"))
        d.send(Frame(SAMPLE, 0, 3, 2500))
        show("Recovered: fresh data restored", d.until_status(
            lambda s: s["sensors"][0]["sequence"] == 3))

if __name__ == "__main__":
    main()
