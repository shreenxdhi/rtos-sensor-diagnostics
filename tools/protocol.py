from dataclasses import dataclass
import struct

DELIMITER, ESCAPE = 0x7E, 0x7D
SAMPLE, COMMAND = 1, 2
STATUS, TEMP_LIMIT, PRESSURE_LIMIT, STALE_MS, PROCESS_DELAY, REPORT_MS, RESYNC = range(1, 8)

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def envelope(body: bytes) -> bytes:
    encoded = bytearray([DELIMITER])
    for byte in body:
        if byte in (DELIMITER, ESCAPE):
            encoded.extend((ESCAPE, byte ^ 0x20))
        else:
            encoded.append(byte)
    encoded.append(DELIMITER)
    return bytes(encoded)

@dataclass(frozen=True)
class Frame:
    type: int
    channel: int
    sequence: int
    value: int

    def body(self) -> bytes:
        payload = struct.pack(">BBBH i", 1, self.type, self.channel, self.sequence, self.value)
        return payload + struct.pack(">H", crc16(payload))

    def encode(self) -> bytes:
        return envelope(self.body())

def decode(packet: bytes) -> Frame:
    if len(packet) < 2 or packet[0] != DELIMITER or packet[-1] != DELIMITER:
        raise ValueError("missing delimiter")
    raw = bytearray()
    escaped = False
    for byte in packet[1:-1]:
        if byte == DELIMITER:
            raise ValueError("unescaped delimiter")
        if escaped:
            raw.append(byte ^ 0x20)
            escaped = False
        elif byte == ESCAPE:
            escaped = True
        else:
            raw.append(byte)
    if escaped or len(raw) != 11:
        raise ValueError("invalid body length/escape")
    if crc16(raw[:9]) != int.from_bytes(raw[9:], "big"):
        raise ValueError("CRC mismatch")
    version, kind, channel, sequence, value = struct.unpack(">BBBH i", raw[:9])
    if version != 1 or kind not in (SAMPLE, COMMAND):
        raise ValueError("unsupported version/type")
    return Frame(kind, channel, sequence, value)
