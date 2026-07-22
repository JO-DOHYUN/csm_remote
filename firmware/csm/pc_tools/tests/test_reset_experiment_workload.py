#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

PC_TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PC_TOOLS))

from run_reset_experiment import (  # noqa: E402
    PROFILE_EXPECTATIONS,
    add_i2_load_failures,
    add_workload_failures,
)


def health(connect: int) -> list[dict]:
    return [{"wifi_connect": connect}, {"wifi_connect": connect}]


class ResetExperimentWorkloadTest(unittest.TestCase):
    def test_idle_does_not_claim_wifi(self) -> None:
        failures: list[str] = []
        add_workload_failures("IDLE_STABILITY", PROFILE_EXPECTATIONS[0], health(0), {}, failures)
        self.assertEqual([], failures)

    def test_zero_wifi_cannot_pass_connected_gate(self) -> None:
        failures: list[str] = []
        add_workload_failures(
            "CONNECTED_TCP", PROFILE_EXPECTATIONS[0], health(0),
            {"wifi_sent": 0, "wifi_connect": 0, "wifi_disconnect": 0}, failures,
        )
        self.assertGreaterEqual(len(failures), 2)

    def test_connected_gate_accepts_real_stream_progress(self) -> None:
        failures: list[str] = []
        add_workload_failures(
            "CONNECTED_TCP", PROFILE_EXPECTATIONS[0], health(1),
            {"wifi_sent": 20, "wifi_connect": 0, "wifi_disconnect": 0}, failures,
        )
        self.assertEqual([], failures)

    def test_reconnect_gate_requires_both_edges(self) -> None:
        failures: list[str] = []
        add_workload_failures(
            "RECONNECT_CHURN", PROFILE_EXPECTATIONS[0], health(1),
            {"wifi_sent": 20, "wifi_connect": 0, "wifi_disconnect": 0}, failures,
        )
        self.assertIn("RECONNECT_CHURN observed no new TCP connection", failures)
        self.assertIn("RECONNECT_CHURN observed no client disconnect", failures)

    def test_i2_requires_declared_can_and_rc_progress(self) -> None:
        failures: list[str] = []
        state = {
            "can_rx_frames": 99,
            "rc_states": [{"valid_frames": 10}, {"valid_frames": 19}],
        }
        add_i2_load_failures("I2_SIMULTANEOUS", state, 100, 10, failures)
        self.assertEqual(2, len(failures))

    def test_i2_accepts_declared_can_and_rc_progress(self) -> None:
        failures: list[str] = []
        state = {
            "can_rx_frames": 6000,
            "rc_states": [{"valid_frames": 10}, {"valid_frames": 3010}],
        }
        add_i2_load_failures("I2_SIMULTANEOUS", state, 5000, 2500, failures)
        self.assertEqual([], failures)


if __name__ == "__main__":
    unittest.main()
