#!/usr/bin/env python3
"""Constitution guard for the REV.B M4 control island."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / "firmware" / "csm"


def fail(message: str) -> None:
    raise SystemExit(f"Constitution guard failed: {message}")


constitution = (ROOT / "docs/product/PRODUCT_CONSTITUTION_KO.md").read_text(
    encoding="utf-8"
)
manifest = (ROOT / "docs/architecture/ACTIVE_ARCHITECTURE.yaml").read_text(
    encoding="utf-8"
)
main = (PROJECT / "src/main.cpp").read_text(encoding="utf-8")
executor = (
    PROJECT / "src/board/control_island/M4StaticCyclicExecutor.cpp"
).read_text(encoding="utf-8")
fdcan = (PROJECT / "src/board/control_island/M4Fdcan1Owner.cpp").read_text(
    encoding="utf-8"
)
shared = (
    PROJECT / "src/board/control_island/ControlIslandSharedMemory.cpp"
).read_text(encoding="utf-8")
platformio = (PROJECT / "platformio.ini").read_text(encoding="utf-8")

for token in (
    "단일 권한·hard-safety",
    "physical execution owner가 정확히 하나",
    "fail closed",
    "hidden retry",
    "무제한 control backlog",
    "hardware evidence",
):
    if token not in constitution:
        fail(f"missing L1 marker: {token}")

for token in (
    "physical_can_owner: csm_m4_control_island",
    "hard_safety_owner: csm_m4_control_island",
    "nominal_request_clock_owner: csm_m4_tim4",
    "host_software_execution_backlog: false",
    "hidden_retry: false",
    "hidden_replay: false",
    "physical_success_requires_hardware_evidence: true",
):
    if token not in manifest:
        fail(f"active architecture missing {token}")

for obsolete in (
    "BuiltinCanTxOwner",
    "BuiltinFdcanDiagnostics",
    "ControlReleaseSchedule",
    "handle_host_can_tx_request",
    "builtin_can_ref().write(",
):
    if obsolete in main:
        fail(f"obsolete M7 execution path remains in main.cpp: {obsolete}")

for path in (
    "include/board/can/BuiltinCanTxOwner.h",
    "src/board/can/BuiltinCanTxOwner.cpp",
    "include/board/can/BuiltinFdcanDiagnostics.h",
    "src/board/can/BuiltinFdcanDiagnostics.cpp",
    "include/board/control/ControlReleaseSchedule.h",
    "src/board/control/ControlReleaseSchedule.cpp",
):
    if (PROJECT / path).exists():
        fail(f"obsolete owner file remains: {path}")

for required in (
    "HAL_FDCAN_AddMessageToTxBuffer",
    "HAL_FDCAN_EnableTxBufferRequest",
    "HAL_FDCAN_AbortTxRequest",
    "TXBRP",
    "TXBTO",
    "TXBCF",
    "TIM4",
):
    if required not in fdcan:
        fail(f"M4 physical owner missing {required}")

for required in (
    "if (next_slot_ == 0u)",
    "releaseLane(kLane005)",
    "releaseLane(kLane007)",
    "releaseLane(kLane364)",
    "elapsedAtLeast(now_us, last_publish_seen_us_, publish_timeout_us_)",
    "transaction_completed",
    "TransactionState::Complete",
    "cancelAllPending()",
):
    if required not in executor:
        fail(f"M4 executor missing {required}")

if executor.count("driver_->cancel(lane)") != 2:
    fail("cancellation must remain bounded to one request plus one resnapshot retry")
if "while (" in executor or "for (;;" in executor:
    fail("M4 slot execution may not contain an unbounded loop")

for required in (
    "ControlSnapshotSlot slot",
    "slot.crc32 = slotCrc(slot)",
    "RawCanEntry",
    "fill >= kRawCanRingCapacity",
):
    if required not in shared:
        fail(f"shared-memory evidence boundary missing {required}")

for required in (
    "BOARD_ENABLE_CONTROL_ISLAND=1",
    "BOARD_ENABLE_HOST_CAN_TX=0",
    "BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0",
    "BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=0",
):
    if required not in platformio:
        fail(f"build profile missing {required}")

print("Constitution guard PASS")
