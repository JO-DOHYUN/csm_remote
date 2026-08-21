#!/usr/bin/env python3
"""Static conformance checks for HNO1 REV.B control-island ownership."""

from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"Architecture conformance guard failed: {message}")


project = Path(__file__).resolve().parents[1]
root = project.parents[1]
manifest = (root / "docs/architecture/ACTIVE_ARCHITECTURE.yaml").read_text(
    encoding="utf-8"
)
platformio = (project / "platformio.ini").read_text(encoding="utf-8")
main = (project / "src/main.cpp").read_text(encoding="utf-8")
contract = (
    project / "include/board/control_island/ControlIslandContract.h"
).read_text(encoding="utf-8")
shared = (
    project / "src/board/control_island/ControlIslandSharedMemory.cpp"
).read_text(encoding="utf-8")
executor = (
    project / "src/board/control_island/M4StaticCyclicExecutor.cpp"
).read_text(encoding="utf-8")
m7_linker = (project / "linker/portenta_h7_m7_product.ld").read_text(
    encoding="utf-8"
)
m4_linker = (project / "linker/portenta_h7_m4_product.ld").read_text(
    encoding="utf-8"
)

for required in (
    "architecture_id: csm-hno1-m4-control-island-rev-b",
    "physical_can_owner: csm_m4_control_island",
    "host_semantic_owner: android_vsm",
    "remote_semantic_owner: csm_m7",
    "global_source_authority_owner: csm_m7",
    "nominal_request_clock_owner: csm_m4_tim4",
    "latest_state_depth: 1",
    "raw_can_ring_capacity: 512",
    "strict_n_shot_owner: csm_m4_generic_success_budget",
):
    if required not in manifest:
        fail(f"active manifest missing {required}")

for required in (
    "kControlIpcAddress = 0x3800A800u",
    "kCan1RawRingAddress = 0x3800B800u",
    "kLanePeriodsUs[kLaneCount] = {5000u, 20000u, 20000u}",
    "kLaneDedicatedBuffers[kLaneCount] = {0u, 1u, 2u}",
    "ControlSnapshotSlot control[2]",
    "ControlHealthSlot health[2]",
):
    if required not in contract:
        fail(f"frozen contract missing {required}")

for linker, name in ((m7_linker, "M7"), (m4_linker, "M4")):
    for address in ("0x3800A800", "0x3800B800", "0x3800F800"):
        if address not in linker:
            fail(f"{name} linker missing D3 boundary {address}")

for required in (
    "initializeControlIpcForM7",
    "control_source_manager.acceptHostState",
    "control_source_manager.acceptHostNShot",
    "control_source_manager.updateRemote",
    "publishFinalControlSnapshot",
    "readControlHealth",
    "popRawCanForM7",
):
    if required not in main:
        fail(f"M7 integration missing {required}")

for forbidden in (
    "HostCanTxRequest))",
    "builtin_can_driver_write",
    "noteCanTxEnqueueResult",
    "cycle_period_ms = 5",
    "frame_gap_ms",
):
    if forbidden in main:
        fail(f"obsolete execution marker remains: {forbidden}")

for required in (
    "BOARD_ENABLE_CONTROL_ISLAND=1",
    "BOARD_CONTROL_ISLAND_HEALTH_TIMEOUT_MS=0UL",
    "BOARD_HNO1_CAN1_BITRATE_QUALIFIED=0",
    "BOARD_HNO1_HARD_SAFETY_QUALIFIED=0",
    "BOARD_HNO1_IRQ_PRIORITY_QUALIFIED=0",
    "BOARD_M4_M7_PUBLISH_TIMEOUT_US=0UL",
):
    if required not in platformio:
        fail(f"qualification/build marker missing {required}")

if "nextEvenSequence" not in shared or "slotCrc" not in shared:
    fail("two-slot sequence/CRC IPC protocol missing")
if "active_.transaction.requested_success_count" not in executor:
    fail("generic successful-TX budget missing")

print("Architecture conformance guard PASS")
