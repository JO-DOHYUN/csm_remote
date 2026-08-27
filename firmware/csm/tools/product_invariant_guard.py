#!/usr/bin/env python3
"""Validate the architecture-independent L1 Product Constitution."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
CONSTITUTION = ROOT / "docs" / "product" / "PRODUCT_CONSTITUTION_KO.md"


def fail(message: str) -> None:
    raise SystemExit(f"Product invariant guard failed: {message}")


text = CONSTITUTION.read_text(encoding="utf-8")
if "Authority: `L1 PRODUCT INVARIANTS`" not in text:
    fail("L1 authority classification is missing")

numbered = re.findall(r"(?m)^(\d+)\.\s+(.+)$", text)
if [int(number) for number, _ in numbered] != list(range(1, 16)):
    fail("Product Constitution must contain exactly invariants 1..15")

for marker in (
    "단일 권한·admission",
    "physical execution owner가 정확히 하나",
    "fail closed",
    "hidden retry",
    "무제한 control backlog",
    "hardware evidence",
    "production policy",
):
    if marker not in text:
        fail(f"missing architecture-independent invariant marker: {marker}")

for implementation_token in ("M4", "M7", "TIM4", "FDCAN", "0x005", "0x007", "0x364"):
    if implementation_token in text:
        fail(f"L2 implementation leaked into L1: {implementation_token}")

print("Product invariant guard PASS")
