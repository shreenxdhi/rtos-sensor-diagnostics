import argparse
import json
from pathlib import Path
import subprocess
import time
import traceback

from device import Device
from protocol import (Frame, SAMPLE, STATUS, TEMP_LIMIT, PRESSURE_LIMIT, STALE_MS,
                      PROCESS_DELAY, REPORT_MS, envelope)

SCENARIOS = []

def scenario(fn):
    SCENARIOS.append(fn)
    return fn

def send_samples(d, frames, pace=0.01):
    for frame in frames:
        d.send(frame)
        time.sleep(pace)

@scenario
def boot_and_resources(d):
    s = d.status()
    assert all(x["state"] == "unavailable" for x in s["sensors"])
    assert s["heap_free"] > 0 and min(s["stack_free_words"]) > 32

@scenario
def two_channels_and_negative_values(d):
    send_samples(d, [Frame(SAMPLE, 0, 1, -125), Frame(SAMPLE, 1, 1, 10125)])
    s = d.until_status(lambda s: all(x["accepted"] == 1 for x in s["sensors"]))
    assert [x["value"] for x in s["sensors"]] == [-125, 10125]
    assert all(x["state"] == "normal" for x in s["sensors"])

@scenario
def fragmented_and_escaped_frame(d):
    packet = Frame(SAMPLE, 0, 0x7E7D, 0x7D).encode()
    for chunk in (packet[:2], packet[2:5], packet[5:]):
        d.raw(chunk)
        time.sleep(0.01)
    s = d.until_status(lambda s: s["sensors"][0]["accepted"] == 1)
    assert s["sensors"][0]["sequence"] == 0x7E7D
    assert s["sensors"][0]["value"] == 125

@scenario
def crc_rejection_and_recovery(d):
    body = bytearray(Frame(SAMPLE, 0, 1, 2000).body())
    body[8] ^= 1
    d.raw(envelope(body))
    d.send(Frame(SAMPLE, 0, 2, 2500))
    s = d.until_status(lambda s: s["sensors"][0]["accepted"] == 1)
    assert s["crc_errors"] == 1 and s["sensors"][0]["value"] == 2500

@scenario
def truncated_frame_timeout(d):
    d.raw(b"\x7e\x01\x01\x00\x7d")
    d.until_status(lambda s: s["frame_timeouts"] >= 1)
    d.send(Frame(SAMPLE, 0, 1, 2500))
    d.until_status(lambda s: s["sensors"][0]["accepted"] == 1)

@scenario
def oversize_resynchronization(d):
    d.raw(b"\x7e" + b"X" * 40 + b"\x7e")
    d.send(Frame(SAMPLE, 0, 1, 2000))
    s = d.until_status(lambda s: s["sensors"][0]["accepted"] == 1)
    assert s["malformed"] >= 1

@scenario
def duplicate_gap_and_order(d):
    send_samples(d, [Frame(SAMPLE, 0, n, 2000) for n in (10, 10, 13, 12)])
    s = d.until_status(lambda s: s["sensors"][0]["out_of_order"] == 1)["sensors"][0]
    assert s["accepted"] == 2 and s["duplicates"] == 1 and s["missing"] == 2

@scenario
def sequence_wrap(d):
    send_samples(d, [Frame(SAMPLE, 0, n, 2000) for n in (65534, 65535, 0, 1)])
    s = d.until_status(lambda s: s["sensors"][0]["accepted"] == 4)["sensors"][0]
    assert s["sequence"] == 1 and s["missing"] == 0

@scenario
def stale_detection_and_recovery(d):
    d.command(STALE_MS, 150)
    mark = d.mark()
    d.send(Frame(SAMPLE, 0, 1, 2000))
    d.wait(lambda e: e.get("event") == "state" and e.get("state") == "normal", since=mark)
    d.until_status(lambda s: s["sensors"][0]["accepted"] == 1 and s["sensors"][0]["state"] == "unavailable")
    mark = d.mark()
    d.send(Frame(SAMPLE, 0, 2, 2000))
    d.wait(lambda e: e.get("event") == "state" and e.get("state") == "normal", since=mark)

@scenario
def warning_hysteresis(d):
    for seq, value, expected in [(1, 3100, "warning"), (2, 2950, "warning"), (3, 2900, "normal")]:
        mark = d.mark()
        d.send(Frame(SAMPLE, 0, seq, value))
        s = d.until_status(lambda s: s["sensors"][0]["sequence"] == seq, since=mark)
        assert s["sensors"][0]["state"] == expected

@scenario
def invalid_commands_and_ranges(d):
    before = d.status()
    d.command(TEMP_LIMIT, -4000, ok=False)
    d.command(PRESSURE_LIMIT, 50000, ok=False)
    d.command(STALE_MS, 0, ok=False)
    d.command(PROCESS_DELAY, 201, ok=False)
    d.command(REPORT_MS, 1, ok=False)
    d.command(99, 0, ok=False)
    d.command(STATUS, 1, ok=False)
    send_samples(d, [Frame(SAMPLE, 0, 1, 99999), Frame(SAMPLE, 9, 1, 0)])
    s = d.until_status(lambda s: s["range_errors"] == 1 and s["channel_errors"] == 1)
    assert s["stale_ms"] == before["stale_ms"]
    assert s["sensors"][0]["threshold"] == before["sensors"][0]["threshold"]

@scenario
def live_configuration(d):
    d.command(TEMP_LIMIT, 2600)
    d.command(PRESSURE_LIMIT, 11000)
    d.command(REPORT_MS, 100)
    send_samples(d, [Frame(SAMPLE, 0, 1, 2700), Frame(SAMPLE, 1, 1, 11500)])
    s = d.until_status(lambda s: all(x["accepted"] == 1 for x in s["sensors"]))
    assert all(x["state"] == "warning" for x in s["sensors"])
    assert s["report_ms"] == 100

@scenario
def overload_is_visible_and_recovers(d):
    d.command(PROCESS_DELAY, 100)
    send_samples(d, [Frame(SAMPLE, 0, n, 2000) for n in range(1, 61)], pace=0.005)
    overloaded = d.until_status(lambda s: s["sample_drops"] > 0)
    assert overloaded["queue_peak"] == 8
    d.command(PROCESS_DELAY, 0)

    time.sleep(0.3)
    d.send(Frame(SAMPLE, 0, 100, 2300))
    s = d.until_status(lambda s: s["sensors"][0]["sequence"] == 100)
    assert s["sample_drops"] > 0 and s["sensors"][0]["value"] == 2300

@scenario
def sustained_nominal_stream(d):
    for seq in range(1, 101):
        d.send(Frame(SAMPLE, 0, seq, 2400 + seq % 20))
        d.send(Frame(SAMPLE, 1, seq, 10000 + seq % 30))
        time.sleep(0.01)
    s = d.until_status(lambda s: all(x["accepted"] == 100 for x in s["sensors"]))
    assert s["sample_drops"] == s["rx_drops"] == s["event_drops"] == 0
    assert all(x["missing"] == 0 for x in s["sensors"])
    assert min(s["stack_free_words"]) > 32

def run(args):
    results = []
    for test in SCENARIOS:
        started = time.monotonic()
        try:
            with Device(args.elf, args.qemu) as d:
                test(d)
                evidence = d.status()
            result = {"name": test.__name__, "passed": True, "status": evidence}
        except Exception as exc:
            traceback.print_exc()
            result = {"name": test.__name__, "passed": False, "error": str(exc)}
        result["wall_seconds"] = round(time.monotonic() - started, 3)
        print(("PASS" if result["passed"] else "FAIL"), test.__name__, flush=True)
        results.append(result)
    version = subprocess.check_output([args.qemu, "--version"], text=True).splitlines()[0]
    report = {"qemu": version, "passed": sum(r["passed"] for r in results),
              "total": len(results), "results": results,
              "scope": "Emulated functional behavior, not physical/clinical or worst-case timing validation"}
    target = Path(args.output)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{report['passed']}/{report['total']} QEMU integration scenarios passed")
    return report["passed"] == report["total"]

if __name__ == "__main__":
    p = argparse.ArgumentParser(description='Black-box tests against the actual ARM ELF running FreeRTOS in QEMU.')
    p.add_argument("--elf", default="build/firmware.elf")
    p.add_argument("--qemu", default="qemu-system-arm")
    p.add_argument("--output", default="build/integration-results.json")
    raise SystemExit(0 if run(p.parse_args()) else 1)
