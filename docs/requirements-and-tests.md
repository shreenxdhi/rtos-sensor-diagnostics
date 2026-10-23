# Requirements and verification matrix

| ID | Requirement | Evidence |
|---|---|---|
| R01 | Boot the Cortex-M3 ARM image and start FreeRTOS | Integration `boot_and_resources` |
| R02 | Acquire two independent synthetic channels | `two_channels_and_negative_values` |
| R03 | Reconstruct fragmented and escaped frames | `fragmented_and_escaped_frame`; C/Python round trips |
| R04 | Reject corrupt CRC without updating a sensor | `crc_rejection_and_recovery`; C known-vector/corruption tests |
| R05 | Expire an incomplete frame after inter-byte silence | `truncated_frame_timeout`; C exact-boundary/wrap tests |
| R06 | Reject overlong input and accept a following valid frame | `oversize_resynchronization`; C bounded parser/noise tests |
| R07 | Reject duplicate/old samples, account for forward gaps | `duplicate_gap_and_order`; C modular sequence tests |
| R08 | Support ordinary 16-bit per-channel wrap | `sequence_wrap` |
| R09 | Mark stale source unavailable and restore on fresh data | `stale_detection_and_recovery` |
| R10 | Apply defined warning hysteresis | `warning_hysteresis`; C threshold boundary tests |
| R11 | Reject invalid configuration, channel and value | `invalid_commands_and_ranges` |
| R12 | Apply accepted runtime configuration | `live_configuration` |
| R13 | Count bounded-queue overload and recover | `overload_is_visible_and_recovers` |
| R14 | Process a bounded nominal run without observed drops | `sustained_nominal_stream` (100 samples/channel) |
| R16 | Reset a restarted source in FIFO order without clearing lifetime diagnostics | `source_restart_and_ordered_resync`; C explicit-resync test |
| R15 | Keep protocol/model memory access within bounds on exercised host paths | ASan/UBSan C tests; 100,000-byte deterministic noise run |

These requirements cover the emulated acquisition and diagnostic system. They
do not establish compliance with a medical device standard. CI is configured;
the recorded results are local executions. QEMU integration validates behavior under
the emulator's model; it is not physical UART, interrupt-latency, sensor-accuracy,
or worst-case response-time certification.

## Deliberate limitations and future tests

- ISR/software RX-overflow and event-queue counters exist, but deterministic
  forced saturation of those two queues is not an integration test here.
- The overload scenario forces sample-queue saturation through a slow-consumer
  hook; it does not establish a maximum supported input rate.
- UART TX backpressure/error injection is not covered. Host waits are bounded.
- Epoch-bearing samples and randomized scheduling are future work. Explicit ordered sequence resynchronization is tested.
- Sensor inputs arrive as frames; no analog or I2C/SPI behavior is emulated.
- Heap/stack telemetry is observed headroom during scenarios, not exhaustive proof.

## Reproduce

Run `make test` from the repository root. Host tests fail on assertions or sanitizer
findings. Python unit tests use a fixed RNG seed. Each integration test starts a
fresh QEMU instance, uses bounded waits, and returns nonzero if any scenario fails.
The JSON report stores per-scenario evidence and host wall duration. That duration
is test runtime only, not simulated MCU execution latency.
