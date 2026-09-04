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

service_hil = (EXPERIMENTS / "service_hil_boundary_observability.yaml").read_text(
    encoding="utf-8"
)
for token in (
    "BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY",
    "TRANSPORT_DIAGNOSTIC schema 4",
    "socket arena capacity, used, high-water, and allocation-fail",
    "NOT_OBSERVED",
    "NOT_RUN",
    "No measured value becomes product policy",
):
    if token not in service_hil:
        fail(f"Service/HIL observability lifecycle missing {token!r}")

firmware = ROOT / "firmware" / "csm"
for relative in (
    "src/board/ControlPolicy.cpp",
    "src/board/control/HostControlAuthorityGate.cpp",
    "src/board/control/HostRealtimeAuthority.cpp",
    "src/board/control/RemoteControlRuntime.cpp",
    "src/board/control_island/ControlSourceManager.cpp",
    "src/board/control_island/M4StaticCyclicExecutor.cpp",
    "src/board/control_island/M4Fdcan1Owner.cpp",
):
    source = (firmware / relative).read_text(encoding="utf-8")
    if "BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY" in source or \
            "TransportDiagnostic" in source:
        fail(f"Service/HIL evidence leaked into runtime owner {relative}")

print("Experiment guard PASS")
