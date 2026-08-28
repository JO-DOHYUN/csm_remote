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
fdcan = (
    project / "src/board/control_island/M4Fdcan1Owner.cpp"
).read_text(encoding="utf-8")
m4_frontend = (project / "src/m4_remote_frontend.cpp").read_text(encoding="utf-8")
remote_types = (
    project / "include/board/remote/RemoteTypes.h"
).read_text(encoding="utf-8")
remote_runtime = (
    project / "src/board/control/RemoteControlRuntime.cpp"
).read_text(encoding="utf-8")
typed_records = (project / "include/protocol/TypedRecords.h").read_text(
    encoding="utf-8"
)
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
    "source_priority: [remote, host, none]",
    "remote_activation_requires: fresh_rc_channels_and_fresh_positive_link_statistics",
    "nominal_request_clock_owner: csm_m4_tim4",
    "latest_state_depth: 1",
    "raw_can_ring_capacity: 512",
    "strict_n_shot_owner: csm_m4_generic_success_budget",
    "physical_transport_readiness: m4_fdcan1_transport_only",
    "active_motion_permission: fresh_coherent_source_owned_lane_permit_explicit_arm",
    "mutable_executor_state_writer: csm_m4_tim4_only",
    "health_read_side_effects: forbidden",
    "idle_safe: FixedSafeFrame_AA02000000000000",
    "idle_safe: FixedSafeFrame_8200000000000000",
    "evidence_only: true",
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
    "SafeWireAction::FixedSafeFrame",
    "SafeWireAction::SuppressTx",
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
    "readBringupTrace",
    "service_control_island",
    "popRawCanForM7",
):
    if required not in main:
        fail(f"M7 integration missing {required}")
heartbeat_handler = main[
    main.find("static void handle_host_heartbeat"):
    main.find("static void handle_host_control_session")
]
for required in (
    "HostFreshnessResult::AnchorEstablished",
    "emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk",
):
    if required not in heartbeat_handler:
        fail(f"Host heartbeat admission evidence missing {required}")
for required in (
    "initializeControlIpcForM7",
    "initializeRemoteSharedMemoryForM7",
    "bootM4();",
):
    if required not in main:
        fail(f"M7 preboot sequence missing {required}")
if main.find("bootM4();") > main.find("remote_control_runtime.begin("):
    fail("M4 boot remains gated behind RC semantic initialization")
preboot = (
    main.find("initializeControlIpcForM7"),
    main.find("initializeRemoteSharedMemoryForM7"),
    main.find("bootM4();"),
    main.find("remote_control_runtime.begin("),
    main.find("wifi_tcp_sink.begin("),
)
if any(index < 0 for index in preboot) or tuple(sorted(preboot)) != preboot:
    fail("M7 Control/Remote IPC -> M4 -> RC -> Wi-Fi preboot order drift")
if "if (!remote_control_runtime_ok) return" in main:
    fail("Control-Island service still short-circuits on RC runtime")
if main.count("service_control_island();") < 5:
    fail("independent Control-Island coordinator service points missing")
if "initializeRemoteSharedMemoryForM7" in remote_runtime:
    fail("RC semantic runtime still owns Remote IPC initialization")
for obsolete in ("authority_manager_", "RemoteControlOrchestrator", "backend_state"):
    if obsolete in remote_runtime:
        fail(f"RC semantic runtime retains global authority owner: {obsolete}")

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
    "BOARD_M4_M7_PUBLISH_TIMEOUT_US=300000UL",
):
    if required not in platformio:
        fail(f"required build marker missing {required}")

if "nextEvenSequence" not in shared or "slotCrc" not in shared:
    fail("two-slot sequence/CRC IPC protocol missing")
if "active_.transaction.requested_success_count" not in executor:
    fail("generic successful-TX budget missing")
for required in (
    "activeMotionAllowed",
    "releaseSafeLane",
    "revokeActive",
):
    if required not in executor:
        fail(f"transport/motion/safe-wire split missing {required}")
if "globalExecutionAllowed" in executor:
    fail("obsolete transport-motion combined gate remains")
for obsolete in ("hardInhibitActive", "safeWireQualified", "hardSafetyQualified"):
    if obsolete in executor:
        fail(f"obsolete GPIO/qualification gate remains: {obsolete}")

for obsolete in (
    "SafetySupervisor", "SafetyState", "SafetyInputs", "HostClearFaultLockout",
    "estop_asserted", "fault_lockout", "safety_supervisor_allows",
    "BOARD_AUTONOMY_RELEASE_PROVIDER_AVAILABLE",
    "BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH",
):
    if obsolete in main:
        fail(f"retired GPIO safety path remains: {obsolete}")

for obsolete in (
    "BuiltinCanTxOwner",
    "BuiltinFdcanDiagnostics",
    "ControlReleaseSchedule",
    "handle_host_can_tx_request",
    "builtin_can_ref().write(",
):
    if obsolete in main:
        fail(f"obsolete M7 execution path remains: {obsolete}")

for path in (
    "include/board/can/BuiltinCanTxOwner.h",
    "src/board/can/BuiltinCanTxOwner.cpp",
    "include/board/can/BuiltinFdcanDiagnostics.h",
    "src/board/can/BuiltinFdcanDiagnostics.cpp",
    "include/board/control/ControlReleaseSchedule.h",
    "src/board/control/ControlReleaseSchedule.cpp",
    "include/board/authority/AuthorityManager.h",
    "src/board/authority/AuthorityManager.cpp",
    "include/board/authority/AutonomyAuthorityMonitor.h",
    "src/board/authority/AutonomyAuthorityMonitor.cpp",
    "include/board/control/RemoteControlOrchestrator.h",
    "src/board/control/RemoteControlOrchestrator.cpp",
    "include/board/control/CanTxGateway.h",
    "src/board/control/CanTxGateway.cpp",
):
    if (project / path).exists():
        fail(f"obsolete owner file remains: {path}")

if "BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH" in platformio:
    fail("obsolete virtual autonomy permission remains in active build")

for required in (
    "HAL_FDCAN_AddMessageToTxBuffer",
    "HAL_FDCAN_EnableTxBufferRequest",
    "HAL_FDCAN_AbortTxRequest",
    "HAL_FDCAN_ConfigInterruptLines",
    "HAL_FDCAN_ActivateNotification",
    "TXBRP",
    "TXBTO",
    "TXBCF",
    "TIM4",
    "TxRequestResult::Accepted",
    "EnableFailedAbortFailed",
    "nominal_bitrate_ == BOARD_HNO1_CAN1_BITRATE",
    "fdcan_irq_total_",
    "tx_complete_callback_total_",
    "handle_->Init.AutoRetransmission = DISABLE",
    "reconcileAcceptedTxBuffers",
    "accepted_buffer_mask_",
    "BringupFailure::ClockContract",
):
    if required not in fdcan:
        fail(f"M4 physical owner missing {required}")
if "handle_->Init.AutoRetransmission = ENABLE" in fdcan:
    fail("FDCAN hidden hardware retransmission is enabled")
for ignored in (
    "(void)HAL_FDCAN_ConfigInterruptLines",
    "(void)HAL_FDCAN_ActivateNotification",
):
    if ignored in fdcan:
        fail(f"FDCAN init failure is ignored: {ignored}")

for required in (
    "advanceBringupTrace",
    "next_stage > trace->stage",
    "trace->failure == static_cast<uint16_t>(BringupFailure::None)",
):
    if required not in shared:
        fail(f"monotonic bring-up trace missing {required}")
if "bringup_trace.stage =" in m4_frontend:
    fail("M4 frontend bypasses monotonic bring-up trace owner")
if "recordBringup(BringupStage::ForegroundLoopEntered)" not in m4_frontend or \
        "recordBringup(BringupStage::FirstTim4Tick)" not in m4_frontend:
    fail("M4 foreground/TIM4 milestones bypass bring-up trace owner")

if "hasFreshPositiveLinkStatistics" not in remote_types or \
        "hasFreshPositiveLinkStatistics(" not in m4_frontend:
    fail("RC authority can be admitted without fresh positive link evidence")

for required in (
    "if (next_slot_ == 0u)",
    "releaseLane(kLane005)",
    "releaseLane(kLane007)",
    "releaseLane(kLane364)",
    "elapsedAtLeast(now_us, last_publish_seen_us_, publish_timeout_us_)",
    "transaction_completed",
    "TransactionState::Complete",
    "cancelActivePending()",
    "consumeTerminalEvents",
    "health_write_sequence_",
    "schedule_due",
    "transport_blocked",
    "request_attempt",
    "request_accepted",
    "request_failed",
):
    if required not in executor:
        fail(f"M4 executor missing {required}")
for required in (
    "kControlIslandHealthPayloadLen = 512",
    "kControlIslandHealthSchema = 4",
    "kRemoteControlStateSchema = 3",
    "kControlIslandHealthBringupStageOffset",
    "kControlIslandHealthAuthorityWordOffset",
):
    if required not in typed_records:
        fail(f"schema-4 observability contract missing {required}")
if executor.count("driver_->cancel(lane)") != 1:
    fail("cancellation must remain one bounded request until terminal closure")
for obsolete in ("CancelRequested", "cancelAllPending", "healthForPublish"):
    if obsolete in executor:
        fail(f"obsolete executor race path remains: {obsolete}")
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
    "BOARD_ENABLE_HOST_CAN_TX=0",
    "BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0",
    "BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=0",
):
    if required not in platformio:
        fail(f"build profile missing {required}")

for required in (
    "stageSnapshot", "latchTerminalEvent", "latchTrackingFault",
    "consumeIngress", "consumeTerminalEvents", "PendingSafe", "PendingActive",
    "source_epoch", "activation_epoch", "healthSnapshot",
):
    if required not in executor and required not in contract:
        fail(f"TIM4 event-state contract missing {required}")

print("Architecture conformance guard PASS")
