#!/usr/bin/env python3
"""Prevent L3 experiment values from silently becoming production authority."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / "firmware" / "csm"


def fail(message: str) -> None:
    raise SystemExit(f"Experiment guard failed: {message}")


manifest = (
    ROOT / "docs" / "architecture" / "ACTIVE_ARCHITECTURE.yaml"
).read_text(encoding="utf-8")
current = (ROOT / "CURRENT.md").read_text(encoding="utf-8")
main = (PROJECT / "src" / "main.cpp").read_text(encoding="utf-8")
experiment = (
    ROOT / "docs" / "experiments" / "host_threshold_qualification.yaml"
).read_text(encoding="utf-8")

for token in (
    "threshold_state: exploratory",
    "production_authority: false",
):
    if token not in manifest:
        fail(f"active manifest missing {token}")
if "production_authority: false" not in current:
    fail("CURRENT does not identify active experiments as non-production")
for token in (
    "id: host-threshold-qualification",
    "authority_level: L3",
    "production_authority: false",
    "hypothesis:",
    "temporary_change:",
    "measurement:",
    "result:",
    "promotion_gate:",
    "cleanup:",
):
    if token not in experiment:
        fail(f"active experiment record missing {token}")
for token in (
    "#define BOARD_BUILTIN_CAN_TX_COMPLETION_TIMEOUT_US 0",
    "#define BOARD_HOST_HEARTBEAT_MAX_EXTRA_LAG_MS 100",
    "#define BOARD_HOST_CAN_TX_MAX_AGE_MS 40",
    "kThresholdQualificationExploratory",
):
    if token not in main:
        fail(f"exploratory source boundary missing {token}")

experiments = ROOT / "docs" / "experiments"
if experiments.exists():
    for path in experiments.rglob("*"):
        if path.is_file() and "production_authority: true" in path.read_text(
            encoding="utf-8", errors="ignore"
        ):
            fail(f"experiment claims production authority: {path.relative_to(ROOT)}")

print("Experiment guard PASS")
