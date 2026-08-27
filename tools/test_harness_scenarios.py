#!/usr/bin/env python3
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def route(case: dict) -> dict:
    task_kind = case["task_kind"]
    if task_kind == "architecture":
        primary = "architecture-change"
    elif task_kind == "experiment":
        primary = "experiment"
    else:
        primary = "implement"
    procedure = {
        "build": "embedded-platformio",
        "hil": "can-hil",
    }.get(task_kind, "verification")
    return {
        "primary": primary,
        "procedure": procedure,
        "exec_plan": bool(case["broad"] or case["cross_repo"]),
        "compare": task_kind == "architecture" and case["decision_state"] != "approved",
        "integration": bool(case["cross_repo"]),
        "history_default": False,
        "current_default": False,
        "git_preflight": True,
        "reconstruct_from_git": bool(case["context_compacted"]),
    }


class HarnessV3ScenarioTest(unittest.TestCase):
    def test_routes(self) -> None:
        cases = json.loads((ROOT / "tools/fixtures/harness_v3_scenarios.json").read_text(encoding="utf-8"))
        self.assertEqual(7, len(cases))
        for case in cases:
            with self.subTest(case=case["id"]):
                actual = route(case)
                self.assertEqual(case["expected_primary"], actual["primary"])
                self.assertEqual(case["expected_procedure"], actual["procedure"])
                self.assertEqual(case["expected_plan"], actual["exec_plan"])
                self.assertEqual(case["expected_compare"], actual["compare"])
                self.assertEqual(case["expected_integration"], actual["integration"])
                self.assertFalse(actual["history_default"])
                self.assertFalse(actual["current_default"])
                self.assertTrue(actual["git_preflight"])
                self.assertEqual(case["context_compacted"], actual["reconstruct_from_git"])

    def test_default_route_is_v3(self) -> None:
        agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        harness = next((ROOT / "docs/harness").glob("*V3*")).read_text(encoding="utf-8")
        self.assertNotIn("CURRENT.md", agents)
        self.assertIn("Git state", agents)
        self.assertIn("approved", agents)
        self.assertIn("owner -> input -> state -> output", agents)
        self.assertIn("HISTORY/CURRENT default-read", harness)

    def test_h0_has_no_l2_implementation_ownership(self) -> None:
        h0 = (ROOT / "tools/verify_harness.py").read_text(encoding="utf-8")
        for token in ("TIM4", "FDCAN", "0x005", "0x007", "0x364", "platformio.ini"):
            self.assertNotIn(token, h0)


if __name__ == "__main__":
    unittest.main()
