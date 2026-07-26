#!/usr/bin/env python3

import unittest

from wifi_raw_tcp_receiver import parse_board_stat, verify_pattern


class WifiRawReceiverTest(unittest.TestCase):
    def test_pattern_accepts_chunk_at_nonzero_offset(self) -> None:
        offset = 255
        data = bytes((offset + index) & 0xFF for index in range(1024))
        self.assertIsNone(verify_pattern(data, offset))

    def test_pattern_reports_first_bad_byte(self) -> None:
        data = bytearray(range(16))
        data[7] = 99
        self.assertEqual(7, verify_pattern(bytes(data), 0))

    def test_board_stat_parser_keeps_strings_and_numbers(self) -> None:
        parsed = parse_board_stat(
            "WIFI_RAW_STAT mode=sigio epoch=2 interval_bytes=4096"
        )
        self.assertEqual("WIFI_RAW_STAT", parsed["kind"])
        self.assertEqual("sigio", parsed["mode"])
        self.assertEqual(2, parsed["epoch"])
        self.assertEqual(4096, parsed["interval_bytes"])

    def test_unrelated_serial_line_is_ignored(self) -> None:
        self.assertIsNone(parse_board_stat("booting"))


if __name__ == "__main__":
    unittest.main()
