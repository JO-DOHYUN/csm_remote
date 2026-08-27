#!/usr/bin/env python3
"""Prevent L3 experiment records from claiming production authority."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
EXPERIMENTS = ROOT / "docs" / "experiments"


def fail(message: str) -> None:
    raise SystemExit(f"Experiment guard failed: {message}")


index = (EXPERIMENTS / "index.yaml").read_text(encoding="utf-8")
for token in (
    "authority: L3",
    "production_authority: false",
    "experiments:",
):
    if token not in index:
        fail(f"experiment index missing {token}")

records = [path for path in EXPERIMENTS.glob("*.yaml") if path.name != "index.yaml"]
if not records:
    fail("experiment index has no record")
for path in records:
    text = path.read_text(encoding="utf-8")
    for token in (
        "authority_level: L3",
        "production_authority: false",
        "hypothesis:",
        "measurement:",
        "promotion_gate:",
        "cleanup:",
    ):
        if token not in text:
            fail(f"{path.name} missing {token}")
    if "production_authority: true" in text:
        fail(f"{path.name} claims production authority")

print("Experiment guard PASS")
