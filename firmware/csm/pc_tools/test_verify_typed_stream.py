import unittest

from verify_typed_stream import GapTracker, decode_can_rx_segment, describe


def u16le(value: int) -> bytes:
    return value.to_bytes(2, "little")


def u32le(value: int) -> bytes:
    return value.to_bytes(4, "little")


def u64le(value: int) -> bytes:
    return value.to_bytes(8, "little")


class CanRxSegmentDecoderTest(unittest.TestCase):
    def compact_payload(self) -> bytes:
        payload = bytearray(80)
        payload[0:8] = u64le(77)
        payload[8:16] = u64le(1000)
        payload[16:18] = u16le(2)
        payload[18] = 20
        payload[19] = 3
        payload[20:24] = u32le(3)
        payload[24:28] = u32le(4)
        payload[28] = 2
        payload[29] = 40
        payload[32:40] = u64le(5_000_000)
        payload[40:42] = u16le(0)
        payload[42:46] = u32le(0)
        payload[46:50] = u32le(0x123)
        payload[50] = 8
        payload[51] = 0
        payload[52:60] = bytes(range(8))
        payload[60:62] = u16le(2)
        payload[62:66] = u32le(250)
        payload[66:70] = u32le(0x20000123)
        payload[70] = 4
        payload[71] = 1
        payload[72:80] = bytes(range(0xA0, 0xA8))
        return bytes(payload)

    def legacy_payload(self) -> bytes:
        payload = bytearray(62)
        payload[0:8] = u64le(9)
        payload[8:16] = u64le(200)
        payload[16:18] = u16le(1)
        payload[18] = 30
        payload[19] = 1
        payload[32:40] = u64le(200)
        payload[40:48] = u64le(7_000_000)
        payload[48:52] = u32le(0x456)
        payload[52] = 8
        payload[53] = 1
        payload[54:62] = b"legacy!!"
        return bytes(payload)

    def test_decodes_compact_v2_exactly(self) -> None:
        segment = decode_can_rx_segment(self.compact_payload())

        self.assertEqual(2, segment["schema"])
        self.assertEqual([1000, 1002], [f["capture_sequence"] for f in segment["frames"]])
        self.assertEqual([5_000_000, 5_000_250], [f["mono_us"] for f in segment["frames"]])
        self.assertEqual(1, segment["frames"][1]["bus"])
        self.assertEqual(bytes(range(0xA0, 0xA8)), segment["frames"][1]["data"])

    def test_decodes_legacy_schema(self) -> None:
        segment = decode_can_rx_segment(self.legacy_payload())

        self.assertEqual(200, segment["frames"][0]["capture_sequence"])
        self.assertEqual(7_000_000, segment["frames"][0]["mono_us"])
        self.assertEqual(b"legacy!!", segment["frames"][0]["data"])

    def test_gap_tracker_observes_compact_capture_sequences(self) -> None:
        tracker = GapTracker()
        tracker.observe({"type": 16, "seq": 1, "payload": self.compact_payload()})

        self.assertEqual(1, tracker.capture_seq_gaps)
        self.assertEqual(0, tracker.segment_decode_errors)

    def test_truncated_compact_segment_is_visible(self) -> None:
        payload = self.compact_payload()[:-1]
        tracker = GapTracker()
        tracker.observe({"type": 16, "seq": 1, "payload": payload})

        self.assertEqual(1, tracker.segment_decode_errors)
        self.assertIn("INVALID", describe({"type": 16, "seq": 1, "payload": payload}))

    def test_invalid_compact_dlc_is_visible(self) -> None:
        payload = bytearray(self.compact_payload())
        payload[50] = 9

        with self.assertRaisesRegex(ValueError, "invalid DLC"):
            decode_can_rx_segment(bytes(payload))

    def test_duplicate_segment_sequence_is_discontinuity(self) -> None:
        tracker = GapTracker()
        payload = self.compact_payload()
        tracker.observe({"type": 16, "seq": 1, "payload": payload})
        tracker.observe({"type": 16, "seq": 2, "payload": payload})

        self.assertGreater(tracker.segment_seq_gaps, 0)
        self.assertGreater(tracker.capture_seq_gaps, 0)


if __name__ == "__main__":
    unittest.main()
