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
control_source_header = (
    project / "include/board/control_island/ControlSourceManager.h"
).read_text(encoding="utf-8")
control_source = (
    project / "src/board/control_island/ControlSourceManager.cpp"
).read_text(encoding="utf-8")
crsf_budget = (
    project / "include/board/remote/CrsfForegroundBudget.h"
).read_text(encoding="utf-8")
remote_shared_header = (
    project / "include/board/remote/RemoteSharedMemory.h"
).read_text(encoding="utf-8")
remote_types = (
    project / "include/board/remote/RemoteTypes.h"
).read_text(encoding="utf-8")
receiver_profile = (
    project / "include/board/remote/R16smReceiverProfile.h"
).read_text(encoding="utf-8")
receiver_admission = (
    project / "src/board/remote/ReceiverAdmission.cpp"
).read_text(encoding="utf-8")
crsf_parser = (project / "src/board/remote/CrsfParser.cpp").read_text(
    encoding="utf-8"
)
normalizer = (project / "src/board/remote/RcNormalizer.cpp").read_text(
    encoding="utf-8"
)
remote_source = (
    project / "src/board/remote/RemoteControlSource.cpp"
).read_text(encoding="utf-8")
control_test = (project / "test/remote_control_contract_test.cpp").read_text(
    encoding="utf-8"
)
remote_runtime = (
    project / "src/board/control/RemoteControlRuntime.cpp"
).read_text(encoding="utf-8")
typed_records = (project / "include/protocol/TypedRecords.h").read_text(
    encoding="utf-8"
)
host_commands = (project / "include/protocol/HostCommands.h").read_text(
    encoding="utf-8"
)
freshness_header = (
    project / "include/board/control/HostCommandFreshness.h"
).read_text(encoding="utf-8")
freshness_source = (
    project / "src/board/control/HostCommandFreshness.cpp"
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
    "source_priority: [remote, host, none]",
    "remote_activation_requires: receiver_qualified_fresh_control_channels",
    "link_statistics: optional_when_absent_zero_or_stale_veto_when_observed",
    "nominal_request_clock_owner: csm_m4_tim4",
    "latest_state_depth: 1",
    "raw_can_ring_capacity: 512",
    "strict_n_shot_owner: csm_m4_generic_success_budget",
    "strict_n_transaction_watermark: m7_boot_or_new_host_control_transport_epoch_only",
    "foreground_drain_budget: {bytes: 64, time_us: 500, evidence: budget_hit_counter}",
    "physical_transport_readiness: m4_fdcan1_transport_only",
    "active_motion_permission: fresh_coherent_source_owned_lane_permit_explicit_arm",
    "mutable_executor_state_writer: csm_m4_tim4_only",
    "health_read_side_effects: forbidden",
    "idle_safe: FixedSafeFrame_AA02000000000000",
    "idle_safe: FixedSafeFrame_8200000000000000",
    "evidence_only: true",
    "host_liveness_owner: csm_m7_receiver_local_causal_ack_proof",
    "host_command_id_reset: m7_boot_or_new_tcp_epoch_only",
    "host_mono_runtime_gate: false",
    "command_ack_tcp_port: 3334",
    "telemetry_evidence_tcp_port: 3333",
    "canonical_ack_mirror: evidence_only",
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
    "HostFreshnessResult::BootstrapAccepted",
    "kHostHeartbeatAckRefOffset",
    "host_command_freshness.acceptHeartbeat(\n          command_id, ack_ref",
    "emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk",
):
    if required not in heartbeat_handler:
        fail(f"Host heartbeat admission evidence missing {required}")
for source, required in (
    (host_commands, "kHostHeartbeatAckRefOffset = 8"),
    (typed_records, "kHostControlSchema = 3"),
    (freshness_source, "ack_ref != pending_heartbeat_id_"),
    (freshness_source, "consumeCommand(command_id)"),
    (freshness_source, "now_ms - last_proof_ms_ <= config_.proof_timeout_ms"),
):
    if required not in source:
        fail(f"Host causal-proof contract missing {required}")
for obsolete in (
    "heartbeat_max_extra_lag_ms",
    "command_max_age_ms",
    "clock_future_tolerance_ms",
    "estimated_host_now",
):
    if obsolete in freshness_header or obsolete in freshness_source:
        fail(f"obsolete cross-clock Host gate remains: {obsolete}")
if "host_command_freshness.resetTransportEpoch();" not in main:
    fail("Host command watermark lacks explicit transport-epoch reset")
if "control_source_manager.resetHostTransportEpoch();" not in main:
    fail("Host N-shot watermark lacks explicit transport-epoch reset")
for required in (
    "last_host_transaction_id_",
    "host_transaction_seen_",
    "sequenceNewer(transaction_id, last_host_transaction_id_)",
    "void ControlSourceManager::resetHostTransportEpoch()",
):
    if required not in control_source_header + control_source:
        fail(f"persistent Host N-shot watermark missing {required}")
if "last_host_transaction_id_ = 0u" in control_source[
    control_source.find("void ControlSourceManager::clearHost"):
    control_source.find("void ControlSourceManager::updateRemote")
]:
    fail("ordinary Host clear resets the N-shot transaction watermark")
for required in (
    "wifi_tcp_sink.offerControlAck(payload, sizeof(payload))",
    "wifi_tcp_sink.controlConnectionEpoch()",
    "wifi_tcp_sink.controlDownlinkStream()",
    "wifi_sink_config.control_port = BOARD_WIFI_CONTROL_TCP_PORT",
    "handle_observer_downlink_frame",
    "observer_downlink_parser.service(*observer, 32)",
):
    if required not in main:
        fail(f"independent Host control transport missing {required}")
observer_handler = main[
    main.find("static void handle_observer_downlink_frame"):
    main.find("static void handle_host_downlink_crc_failure")
]
if "RecordType::HostQueryCapability" not in observer_handler:
    fail("telemetry downlink lost bounded observer capability query")
for forbidden in ("dispatch_host_frame", "HostControlSession", "HostControlStateV2"):
    if forbidden in observer_handler:
        fail(f"telemetry downlink can dispatch Host control: {forbidden}")
close_handler = main[
    main.find("static void close_host_control_epoch"):
    main.find("static void service_host_authority_boundary")
]
if "host_command_freshness" in close_handler:
    fail("ordinary Host close resets causal proof or command watermark")
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
enable_failure = fdcan[fdcan.find("if (enable_failure_total_"):
                       fdcan.find("bool M4Fdcan1Owner::cancel")]
for required in ("accepted_buffer_mask_ |= buffer",
                 "HAL_FDCAN_AbortTxRequest",
                 "EnableFailedAbortPending"):
    if required not in enable_failure:
        fail(f"enable-failed physical pending is not terminal-tracked: {required}")
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

if "hasFreshPositiveLinkStatistics" in remote_types or \
        "hasFreshPositiveLinkStatistics(" in m4_frontend:
    fail("obsolete mandatory Link Statistics gate remains")
for required in (
    "kR16smCrsfAddress = 0xC8u",
    "kR16smConfiguredBaud = 416666u",
    "kR16smDriveChannelIndex = 3u",
    "kR16smSteeringChannelIndex = 1u",
    "kR16smAdmissionConsecutiveFrames = 3u",
):
    if required not in receiver_profile:
        fail(f"R16SM product profile missing {required}")
for required in (
    "link_statistics_observed",
    "ReceiverAdmissionRejectDetail::LinkQualityZero",
    "ReceiverAdmissionRejectDetail::LinkStatisticsStale",
    "consecutive_frames_required",
):
    if required not in receiver_admission:
        fail(f"receiver-qualified admission missing {required}")
for required in (
    "kCrsfFrameTypeSubsetRcChannelsPacked",
    "kCrsfFrameTypeLinkStatisticsRx",
    "kCrsfFrameTypeLinkStatisticsTx",
    "discardPrefix(1u)",
    "buffer_[0] != kR16smCrsfAddress",
):
    if required not in crsf_parser:
        fail(f"stream-resynchronizing CRSF parser missing {required}")
if "channels.count != kRcChannelCount" in normalizer:
    fail("RC normalization still requires a full 16-channel frame")
for required in (
    "config_.required_channel_mask",
    "usable_mask &=",
):
    if required not in normalizer:
        fail(f"required/optional channel separation missing {required}")
if "channelValid(snapshot.sample, config_.drive_channel_index)" not in remote_source or \
        "optionalChannel(snapshot.sample" not in remote_source:
    fail("RC source does not separate control validity from optional functions")
for required in (
    "testReceiverQualifiedAdmissionAndOptionalStatistics",
    "testCrsfStreamResynchronizationAndR16smFixture",
    "testCrsfModernFramesAndChannelValidity",
    "testTransmitterOffOnAndHostToRcTakeover",
    "testPhysicalPendingAlwaysReconciles",
):
    if required not in control_test:
        fail(f"focused RC/FDCAN regression missing {required}")
if "BOARD_M4_REMOTE_BAUD=420000" in platformio or "420000" in m4_frontend:
    fail("alternate R16SM runtime baud remains")
for required in (
    "kCrsfForegroundByteBudget = 64u",
    "kCrsfForegroundTimeBudgetUs = 500u",
    "class CrsfForegroundBudget",
):
    if required not in crsf_budget:
        fail(f"bounded M4 CRSF foreground budget missing {required}")
for required in (
    "CrsfForegroundBudget crsf_budget(micros())",
    "crsf_budget.mayConsume(micros())",
    "diagnostics.foreground_budget_hits",
    "serviceControlIngress();",
    "publishControlIslandHealth(now_ms);",
):
    if required not in m4_frontend:
        fail(f"M4 foreground service ordering/budget missing {required}")

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
    "TxRequestResult::EnableFailedAbortPending",
    "lane_state_[lane] = LaneState::PendingSafe",
    "lane_state_[lane] = LaneState::PendingActive",
):
    if required not in executor:
        fail(f"M4 executor missing {required}")
for required in (
    "kControlIslandHealthPayloadLen = 512",
    "kControlIslandHealthSchema = 5",
    "kControlIslandHealthLocalReadyReasonOffset = 9",
    "kControlPathDiagnosticPayloadLen = 340",
    "kRemoteControlStatePayloadLen = 232",
    "kRemoteControlStateSchema = 4",
    "kRemoteControlStateRejectedAddressOffset",
    "kRemoteControlStateMalformedTotalOffset",
    "kRemoteControlStateAdmissionRejectDetailOffset",
    "kRemoteControlStateForegroundBudgetHitsOffset",
    "kControlIslandHealthBringupStageOffset",
    "kControlIslandHealthAuthorityWordOffset",
):
    if required not in typed_records:
        fail(f"canonical observability contract missing {required}")
for required in (
    "kRemoteSharedMemoryVersion = 4",
    "foreground_budget_hits",
    "last_admission_reject_detail",
    "receiver_qualified",
):
    if required not in remote_shared_header:
        fail(f"M4/M7 RC observability bridge missing {required}")
for required in (
    "kRemoteControlStateRejectedAddressOffset",
    "kRemoteControlStateMalformedTotalOffset",
    "kRemoteControlStateAdmissionRejectDetailOffset",
    "kRemoteControlStateForegroundBudgetHitsOffset",
):
    if required not in main:
        fail(f"REMOTE_CONTROL_STATE producer omits {required}")
for required in (
    "testHostNShotTransactionWatermarkSurvivesLifecycle",
    "testCrsfForegroundBudgetIsByteTimeAndWrapBounded",
):
    if required not in control_test:
        fail(f"control lifecycle/budget regression missing {required}")

for obsolete_tool in (
    "pc_tools/send_host_can_tx_request.py",
    "pc_tools/exercise_host_can_tx_request.py",
    "pc_tools/hil_csm_dual_safety_gate.py",
):
    if (project / obsolete_tool).exists():
        fail(f"obsolete HostCanTxRequest tool remains: {obsolete_tool}")
if executor.count("driver_->cancel(lane)") != 1:
    fail("cancellation must remain one bounded request until terminal closure")
for token in (
    "return control_island_local_ready_reason(now_ms) == 0u;",
    "record_control_path_failure(1u, now_ms)",
    "record_control_path_failure(2u, now_ms)",
    "BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY && BOARD_ENABLE_WIFI_UPLINK",
    "now_ms - last_emit_ms < 1000u",
):
    if token not in main: fail(f"control attribution boundary missing {token}")
if "testLocalReadyTruthAndFirstFailureRetention" not in control_test:
    fail("local predicate equivalence/first failure regression missing")
for token in ("health_attempts", "health_published", "health_publish_failures", "diagnosticTickTotal"):
    if token not in m4_frontend: fail(f"M4 generation/publication evidence missing {token}")
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
