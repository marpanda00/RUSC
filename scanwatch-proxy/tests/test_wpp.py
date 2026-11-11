import hashlib
import unittest

from wpp import (
    CMD_PROBE,
    CMD_VIBRATOR,
    TYPE_VIBRATOR_SET_SIMPLE,
    FrameAssembler,
    challenge_answer,
    challenge_reply_frame,
    parse_frame,
    parse_probe_challenge,
    parse_vibrator,
    probe_frame,
    vibrator_frame,
)


class VibratorTests(unittest.TestCase):
    def test_floor_loop_is_encoded_in_cmd_vibrator(self):
        frame = vibrator_frame(2)
        self.assertEqual(frame.opcode, CMD_VIBRATOR)
        on_ms, off_ms, loops = parse_vibrator(frame)
        self.assertEqual(loops, 2)
        self.assertGreater(on_ms, 0)
        self.assertGreater(off_ms, 0)
        obj = frame.first(TYPE_VIBRATOR_SET_SIMPLE)
        self.assertEqual(len(obj.data), 5)

    def test_round_trip_bytes(self):
        raw = vibrator_frame(3).to_bytes()
        again = parse_frame(raw)
        self.assertEqual(parse_vibrator(again)[2], 3)

    def test_rejects_counts_outside_three_floors(self):
        with self.assertRaises(ValueError):
            vibrator_frame(0)
        with self.assertRaises(ValueError):
            vibrator_frame(4)


class ProbeTests(unittest.TestCase):
    def test_probe_is_version_one_cmd_probe(self):
        raw = probe_frame().to_bytes()
        self.assertEqual(raw[0], 0x01)
        self.assertEqual(parse_frame(raw).opcode, CMD_PROBE)

    def test_challenge_matches_withouthings_vector(self):
        mac = "a4:7e:fa:44:d6:10"
        secret = "gUf8Np69A4GvJxjY1XOcIHKQm2HcPZnO"
        challenge = bytes(
            [244, 197, 79, 127, 24, 111, 82, 130, 216, 87, 5, 54, 35, 63, 193, 35]
        )
        expected = bytes(
            [84, 20, 165, 52, 232, 6, 253, 184, 77, 32, 105, 86, 199, 96, 220, 232, 42, 76, 25, 32]
        )
        self.assertEqual(challenge_answer(challenge, mac.upper(), secret), expected)
        self.assertEqual(
            hashlib.sha1(challenge + b"a4:7e:fa:44:d6:10" + secret.encode()).digest(),
            expected,
        )

    def test_challenge_reply_round_trip(self):
        mac = "a4:7e:fa:44:d6:10"
        secret = "gUf8Np69A4GvJxjY1XOcIHKQm2HcPZnO"
        challenge = bytes(range(16))
        frame = parse_frame(challenge_reply_frame(mac, challenge, secret).to_bytes())
        parsed_mac, parsed_challenge = parse_probe_challenge(frame.objects[1].data)
        self.assertEqual(parsed_mac, mac)
        self.assertEqual(parsed_challenge, bytes(16))


class AssemblerTests(unittest.TestCase):
    def test_split_gatt_notifications_are_rejoined(self):
        raw = vibrator_frame(1).to_bytes()
        assembler = FrameAssembler()
        self.assertEqual(assembler.feed(raw[:4]), [])
        frames = assembler.feed(raw[4:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(parse_vibrator(frames[0])[2], 1)


if __name__ == "__main__":
    unittest.main()
