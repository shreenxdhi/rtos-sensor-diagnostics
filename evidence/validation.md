# Recorded validation

A clean local `make test` passed on the toolchain recorded in `environment.json`.

| Layer | Result |
| --- | --- |
| C core under ASan/UBSan | 12/12 test groups, including 100,000 noise bytes |
| Python protocol | 6/6 tests, including 2,000 seeded round trips |
| ARM FreeRTOS firmware in QEMU | 15/15 scenarios |
| Demonstration and five-second synthetic fault stream | Completed |

The added resynchronization scenario checks rejection of a restarted sequence before reset, invalid channels, counter retention, warning clearing, channel isolation and ordering behind queued samples. Nominal traffic accepted 100 samples per channel with no observed queue drops. Forced overload reached depth 8, rejected 49 queued inputs, and recovered with sequence 100. Counts during overload depend on scheduling.

The recorded ARM ELF contains 13116 bytes text, 8 data and 65976 BSS. The nominal snapshot had 56168 bytes free heap and stack high-water observations of [459, 326, 448] remaining words. BSS includes the configured heap. These values are observations, not WCET or stack-safety guarantees.

`build-and-test.log` records the clean build and tests. `integration-results.json` contains scenario snapshots. `demo-transcript.txt` and `fault-stream.jsonl` record the demonstrations. `environment.json` records versions and the tested ELF hash. Host C static analysis completed without diagnostics on protocol.c and monitor.c. No sanitizer finding occurred in exercised paths.

This establishes behavior in the stated host/emulator model. Physical UART timing, sensor accuracy, I2C/SPI peripherals, board bring-up, power behavior and medical compliance remain unvalidated. Hosted CI is configured but was not executed during this local run.
