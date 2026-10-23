from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from protocol import Frame, SAMPLE, COMMAND, crc16, decode, envelope

class ProtocolTests(unittest.TestCase):
    def test_known_crc(self):
        self.assertEqual(crc16(b"123456789"), 0x29B1)

    def test_signed_extremes_and_escaping(self):
        for value in (-(2**31), -1, 0, 125, 126, 2**31 - 1):
            f = Frame(SAMPLE, 1, 0x7E7D, value)
            self.assertEqual(decode(f.encode()), f)
            self.assertLessEqual(len(f.encode()), 24)

    def test_seeded_roundtrips(self):
        rng = random.Random(42)
        for _ in range(2000):
            f = Frame(rng.choice((SAMPLE, COMMAND)), rng.randrange(256),
                      rng.randrange(65536), rng.randrange(-(2**31), 2**31))
            self.assertEqual(decode(f.encode()), f)

    def test_corrupt_crc(self):
        body = bytearray(Frame(SAMPLE, 0, 1, 2000).body())
        body[8] ^= 1
        with self.assertRaises(ValueError):
            decode(envelope(body))

    def test_invalid_envelopes(self):
        for data in (b"", b"\x7e", b"\x7e\x7d\x7e", b"\x7e\x7e\x7e", b"garbage"):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode(data)

    def test_version_and_type(self):
        for index in (0, 1):
            raw = bytearray(Frame(SAMPLE, 0, 1, 0).body()[:9])
            raw[index] = 99
            raw.extend(crc16(raw).to_bytes(2, "big"))
            with self.assertRaises(ValueError):
                decode(envelope(raw))

if __name__ == "__main__":
    unittest.main()
