# Validation results

## Executed checks

The delivered source subset was rebuilt from a clean `build/` directory and
tested locally with `make test`. All executed checks passed:

| Layer | Result | Important exercised cases |
|---|---|---|
| C core | 11/11 test groups | CRC reference, signed extremes, escaping, bounds, timeout/wrap, hysteresis, modular sequence rules, 100,000-byte deterministic noise |
| Python wire format | 6/6 unit tests | Known CRC, 2,000 seeded round trips, escaping, malformed envelopes, version/type rejection |
| ARM/FreeRTOS/QEMU | 14/14 integration scenarios | Actual IRQ/task/queue execution, corruption, truncation, overload, sequence wrap, stale recovery, configuration |
| Human-readable demo | Completed | Normal -> warning -> unavailable -> recovered |
| Synthetic fault stream | Completed | Injected CRC errors and source pauses; observed unavailability and fresh-data recovery |

Host C checks were compiled with AddressSanitizer, UndefinedBehaviorSanitizer,
and strict compiler warnings. No sanitizer finding occurred in the exercised run.

## Observed results from the captured run

- Nominal stream: 100 samples per channel, 200 accepted samples total; zero
  observed RX/sample/event-queue drops and zero sequence gaps.
- Forced slow-consumer scenario: sample queue reached its configured capacity of
  eight; 49 samples were rejected in this run; after removing the delay, a fresh
  sequence-100 sample was accepted. Exact drop count is scheduling-dependent;
  tests require visible loss and recovery, not exactly 49.
- Firmware image: 13,040 bytes of text, 8 bytes of initialized data, and 65,968
  bytes of BSS for the recorded build. The configured kernel heap is inside BSS;
  these numbers do not mean all BSS represents simultaneously used heap objects.
- Nominal-run snapshot: 56,168 heap bytes free; task stack high-water observations
  were 463, 328, and 450 remaining words for parser, monitor, and reporter.

These are observations from an emulator and the specified scenarios, not hardware
throughput, interrupt-latency, WCET, stack-safety, or clinical claims.

## Saved artifacts

- `build-and-test.log`: actual clean-build/test output.
- `integration-results.json`: pass/fail, final status, and host wall duration per
  scenario, plus QEMU version.
- `demo-transcript.txt`: readable normal/warning/stale/recovery walkthrough.
- `fault-stream.jsonl`: captured synthetic stream with deliberate failures.
- `environment.json`: actual tool versions, host, and verified firmware ELF hash.

GitHub Actions is configured but has not been executed remotely. Rerun `make test`
after modifying code or on another toolchain. New integration evidence is written
to `build/integration-results.json`; retained `evidence/` files describe this
captured build and are not silently overwritten by ordinary tests.
