# Binary input and JSON output

The UART is full duplex: input is binary frames; output is newline-delimited JSON.
Protocol version 1 uses a fixed body of 11 decoded bytes.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Version, exactly 1 |
| 1 | 1 | Type: sample=1, command=2 |
| 2 | 1 | Sample channel (0/1), or command opcode |
| 3 | 2 | Per-channel sequence, or command correlation token; big endian |
| 5 | 4 | Signed two's-complement fixed-point value/command argument; big endian |
| 9 | 2 | CRC-16/CCITT-FALSE over bytes 0..8; big endian |

CRC polynomial 0x1021, initial 0xFFFF, no reflection, final XOR zero.
Check vector `123456789` -> 0x29B1. CRC is corruption detection, not authentication.

Wrap body in 0x7E delimiters. Escape body bytes 0x7E/0x7D as 0x7D followed by
byte XOR 0x20. Empty delimiters are ignored; a delimiter can end one frame and
start another. Max encoded packet length is 24 bytes. Overlong bodies are
discarded until a delimiter. Incomplete nonempty/escaped frames expire after
100 emulated milliseconds of inter-byte silence. Parser wakes at most 20 ms
later to observe timeout. It also checks expiry on each input byte.

Channel/range/sequence validation is separate from syntactic framing/CRC checks.
Receiving a valid packet does not guarantee its sample was queued or accepted.

## Commands

| Opcode | Name | Argument | ACK |
|---|---|---|---|
| 1 | STATUS | Exactly zero | ACK + status with matching token |
| 2 | TEMP_LIMIT | -3900..12500 | ACK |
| 3 | PRESSURE_LIMIT | 100..30000 | ACK |
| 4 | STALE_MS | 100..5000 | ACK |
| 5 | PROCESS_DELAY | 0..200 ms | ACK; test hook |
| 6 | REPORT_MS | 100..5000 ms | ACK |

Invalid opcode/argument -> `ok:0`, no configuration change. Invalid version,
type, size, or CRC produces no ACK because the command cannot be trusted.
ACK `ok:1` means configuration accepted, not physical action confirmed. Command
tokens are host correlation identifiers; duplicate-command suppression is not
implemented. Current commands set values or query state, rather than incrementing
or performing non-idempotent actuator operations.

## JSON events

- `boot`: emulated platform and kernel version.
- `ack`: token, opcode, and ok=0/1.
- `state`: channel and new state (`normal`, `warning`, `unavailable`).
- `status`: cumulative counters, configuration, resource observations, two sensors.
- `fatal`: terminal development failure.

Periodic status uses token zero; explicit STATUS replies use the command token.
`tick_ms` is the wrapping 32-bit 1-kHz FreeRTOS tick. Time arithmetic assumes
intervals shorter than a complete wrap and observations frequent enough to avoid
losing multiple wraps. Counts wrap at their 32-bit storage limit.

`frames` includes syntactically valid sample and command frames. `accepted` is
per-channel monitor acceptance. `missing` is sequence-gap accounting, while
`sample_drops` is directly observed downstream queue rejection. These counters
answer different questions and should not be added as independent loss causes.

