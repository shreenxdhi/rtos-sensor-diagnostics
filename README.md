# RTOS Sensor Acquisition and Diagnostic System

ARM Cortex-M3 firmware executing **FreeRTOS V11.1.0 in QEMU**, with a Python
sensor generator and fault-injection harness. No physical hardware is required.

The firmware receives two synthetic measurement channels, checks their integrity
and sequence, tracks warning and stale states, and reports diagnostics. Its main
design constraints are bounded memory, responsive reception, and visible recovery
from bad input and overload. The test signals do not represent calibrated sensors
or clinical measurements.

## Installation

macOS with Homebrew:

```sh
brew install qemu arm-none-eabi-gcc make llvm python
```

Ubuntu or Debian:

```sh
sudo apt-get update
sudo apt-get install gcc-arm-none-eabi qemu-system-arm clang make python3
```

The Python tools use only the standard library; no pip dependencies.
The required FreeRTOS source subset is vendored, so building does not fetch code.

## Build and run

Run from the repository root:

```sh
make check-tools
make all
make demo
make test
python3 tools/stream.py --seconds 10 --hz 25 --faults
```

The build produces `build/firmware.elf` and a linker map. The demo exercises normal,
warning, unavailable, and recovered states. Tests cover host C, Python protocol
handling, and the ARM image running in QEMU.

`make run` starts the firmware with a raw binary UART on stdin and JSON on stdout.
Use the demo or stream tool for ordinary interaction, not typed text commands.
Ctrl-C stops `make run`. Each Python tool starts its own isolated QEMU instance
and terminates it on exit. Its UART TCP socket is bound only to localhost.

## Architecture

```text
Host Python: synthetic frames / commands / injected failures
                         |
                 emulated UART socket
                         |
ARM firmware in QEMU (mps2-an385)
  UART0 RX IRQ -> 256-byte FreeRTOS queue -> parser task (priority 3)
                                             |
                                     8-sample queue
                                             |
                                    monitor task (priority 2)
                                             |
                              state/ACK events (16 entries)
                                             |
                                   reporter task (priority 1)
                                             |
                                UART JSON status / events
```

The scheduler, ISR entry/return, SysTick, ARM FreeRTOS port, and UART register
accesses execute in the emulated machine. The host does **not** compute the
firmware's diagnostic answers. `core/` contains the same parser/monitor code used
by both host tests and the ARM image.

## Features

- Two independent synthetic channels, fixed-point values in hundredths of a unit.
- CRC-16/CCITT-FALSE, delimiter escaping, bounded frame size, partial-frame timeout.
- Per-channel sequence tracking, duplicate/out-of-order rejection, gap accounting,
  and normal 16-bit sequence wraparound.
- Configurable thresholds with 1.00-unit hysteresis; stale readings become
  `unavailable` and fresh accepted readings restore availability.
- Bounded queues with reject-newest policy and explicit drop/high-water counters.
- Commands with request tokens and ACKs; validation before configuration changes.
- Periodic consistent status snapshots, heap availability, and task-stack high-water
  marks. UART output is owned by one task during normal operation.
- Configurable slow-consumer test hook to reproduce overload and recovery.
- Static application state; kernel queues and task stacks allocate once at startup
  from heap_4. No per-frame allocation.

## Repository structure

| Path | Purpose |
|---|---|
| `core/protocol.c` | CRC, framing, bounded parser, timeout/resynchronization |
| `core/monitor.c` | Validation, sequence rules, hysteresis, stale-state calculation |
| `firmware/main.c` | FreeRTOS tasks, queues, commands, JSON reporting |
| `firmware/bsp.c` | CMSDK UART registers, RX ISR, fault hooks |
| `firmware/startup.S` | Vector table, initialization, reset entry |
| `firmware/linker.ld` | ARM code/RAM layout and exception-stack reserve |
| `tools/protocol.py` | Independent Python wire-format reference |
| `tools/device.py` | QEMU lifecycle, UART socket, event/ACK waiting |
| `tools/integration.py` | Black-box emulator scenarios and JSON evidence |
| `tests/` | C boundary/noise tests and Python protocol tests |
| `docs/` | Architecture, wire protocol, requirements, and verification scope |
| `evidence/` | Results captured from the verified build |

## Debugging

```sh
make debug
```

In a second terminal:

```sh
arm-none-eabi-gdb build/firmware.elf
```

In GDB:

```text
target remote 127.0.0.1:3333
break main
continue
```

QEMU starts halted with `make debug`. An ARM-capable debugger is optional and is
not supplied by this repository. The build does not require newlib: a deliberately
minimal freestanding memory runtime and integer-only formatter are included.
`firmware/freestanding/` is used only for the ARM build, never the host tests.

## Validation and limits

The captured run passed **11 C test groups**, **6 Python tests**, and **14 QEMU
integration scenarios**. See `evidence/validation.md` and the machine-readable
results. C host tests use AddressSanitizer and UndefinedBehaviorSanitizer.

This does not establish real UART line timing, physical sensor accuracy, MCU
board bring-up, I2C/SPI drivers, power behavior, worst-case execution time, or
medical/regulatory compliance. QEMU tick intervals are emulated-time observations;
host wall time is not a Cortex-M performance benchmark.

Technical details: [architecture](docs/architecture.md),
[wire protocol](docs/protocol.md), and
[requirements and verification](docs/requirements-and-tests.md).

## License and dependency

Original application code: MIT. FreeRTOS-Kernel retains its upstream MIT license.
See `third_party/README.md` for the exact revision and retained source subset.

