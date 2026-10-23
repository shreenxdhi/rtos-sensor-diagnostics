# Architecture and design decisions

## Platform

The target is QEMU `mps2-an385`, Cortex-M3, with a 25-MHz clock assumption and
1-kHz FreeRTOS tick. UART0 is the CMSDK APB UART at `0x40004000`; RX is IRQ0.
The image links code at zero and uses 128 KiB of the emulated SRAM starting at
`0x20000000`. The linker reserves 4 KiB above BSS for the exception/main stack.
FreeRTOS's Cortex-M3 port manages task stacks and context switching.

This is not an STM32 target and the UART is not STM32 USART hardware. Porting
requires a different BSP, startup/linker configuration, clocks, and validation.

## Task and ISR contracts

| Context | Priority | Work | Blocking |
|---|---|---|---|
| UART RX ISR | raw 0x60 | Read one byte, enqueue using FromISR, request reschedule | None |
| Parser | task 3 | Framing, CRC, dispatch samples/commands | RX wait up to 20 ms |
| Monitor | task 2 | Validate samples, update channel state, detect transitions | Sample wait up to 20 ms; optional bounded injected delay |
| Reporter | task 1 | Serialize ACK/state/status and own normal TX | Event wait up to 20 ms; bounded TX polling |
| Idle | task 0 | Kernel idle | Kernel defined |

Interrupt and task priority numbers use different conventions. IRQ priority 0x60
is below the configured 0x50 FreeRTOS syscall boundary, so it may call FromISR
queue APIs. No formatting, allocation, command handling, or delay occurs in RX ISR.

Parser gets the highest task priority to remove bytes promptly from the software
RX queue. It can still overload its downstream sample queue; this is intentional,
counted, and tested. The architecture does not guarantee lossless operation at
arbitrary rates. Bounded waits ensure parser timeouts and state checks run even
without further input.

## Ownership and synchronization

Queues copy bytes, frames, and small events. No task passes a pointer to a local
buffer for delayed use. Parser state belongs to parser; monitor state belongs to
monitor except bounded command updates. Reporter takes a copy of all shared
status inside a critical section and formats/transmits outside it.

Critical sections also protect counters/configuration. They do not contain UART
polling, queue waits, or delays. Volatile is used for memory-mapped registers;
it is not used as a substitute for synchronization. The ISR's drop counter is
read under interrupt masking in a snapshot.

The initial kernel heap is 64 KiB. The application allocates three queues and
three tasks once at startup. Heap_4 supplies the kernel allocator. Parser,
monitor, and reporter stacks are respectively 512, 384, and 1,024 **32-bit words**.
High-water values in telemetry are remaining words observed by FreeRTOS, not a
formal worst-case stack proof.

## Channel policy

- Channel 0: synthetic temperature, range -4000..12500, default warning 3000.
- Channel 1: synthetic pressure-like value, range 0..30000, default warning 12000.
- Both use integer hundredths. No calibration or physiological interpretation.
- Warning sets at value >= threshold and clears at value <= threshold-100.
- Initial and stale channels are `unavailable`; last value is retained but must
  not be interpreted as fresh when state is unavailable.
- Staleness uses the frame's parser-arrival tick, not delayed consumer time.
- Duplicates, out-of-order frames, and invalid values do not refresh freshness.
- Updating a threshold recomputes the warning latch from the latest value.

Sequence numbers are per channel. Delta modulo 65536: zero is duplicate;
1..32767 is forward; 32768..65535 is old/ambiguous. Forward gaps count missing
sequence positions. This is not a transport-loss root-cause estimator: missing
positions may reflect source drops, corruption, or the sample queue's own drops.
A source restarting its sequence counter must use the ordered RESYNC command
documented in protocol.md. Merely restarting at zero is not universally accepted.
Samples still lack an epoch; transport flushing is required across resynchronization.

## Failure visibility

RX queue full -> `rx_drops`; sample queue full -> `sample_drops`; event queue
full -> `event_drops`. All use reject-newest. An overloaded event queue can lose
individual state notifications/ACKs; periodic status remains the authoritative
state/counter snapshot when TX remains available. Host command waits are bounded.

UART TX polling is bounded; a failed TX operation may omit output, so host waits
time out. A future physical target should expose TX-error counters and/or a
nonblocking transmit queue/interrupt driver. Allocation failure, stack-overflow
hook, asserts and unexpected exceptions enter a terminal fatal handler. This is
a development failure response, not a certified safety mechanism.

## Primary references

- https://www.qemu.org/docs/master/system/arm/mps2.html
- https://github.com/qemu/qemu/blob/master/hw/char/cmsdk-apb-uart.c
- https://github.com/qemu/qemu/blob/master/hw/arm/mps2.c
- https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.1.0
- https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/03-Emulation-and-simulation/QEMU/freertos-on-qemu-mps2-an385-model

