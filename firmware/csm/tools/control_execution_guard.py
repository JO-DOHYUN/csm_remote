from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"Control execution guard failed: {message}")


root = Path(__file__).resolve().parents[1]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8")
platformio = (root / "platformio.ini").read_text(encoding="utf-8")
host_parser_source = (root / "src" / "board" / "HostDownlinkParser.cpp").read_text(
    encoding="utf-8"
)
freshness_header = (
    root / "include" / "board" / "control" / "HostCommandFreshness.h"
).read_text(encoding="utf-8")
freshness_source = (
    root / "src" / "board" / "control" / "HostCommandFreshness.cpp"
).read_text(encoding="utf-8")


def environment_body(name: str) -> str:
    marker = f"[env:{name}]"
    start = platformio.find(marker)
    if start < 0:
        fail(f"missing build environment {name}")
    end = platformio.find("\n[env:", start + len(marker))
    return platformio[start : end if end >= 0 else len(platformio)]


service_hil_feeder = environment_body(
    "portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi"
)
for required in (
    "BOARD_CSM_PROFILE_FULL_INSTRUMENTED=1",
    "BOARD_CSM_PROFILE_REMOTE_PRODUCT=0",
    "BOARD_ENABLE_FEEDER_UART=1",
    "BOARD_ENABLE_REMOTE_CONTROL=1",
    "BOARD_ENABLE_REMOTE_AUTHORITY=1",
    "BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING=0",
    "BOARD_ENABLE_SERVICE_HIL_VEHICLE_COMMAND_MAPPING=1",
    "BOARD_ENABLE_MDPS_BENCH_MAPPING=0",
    "BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS=1",
    "BOARD_ENABLE_HOST_CAN_TX_BUILTIN=1",
    "BOARD_ENABLE_HOST_DOWNLINK=1",
    "BOARD_HOST_DOWNLINK_TRANSPORT_WIFI=1",
    "BOARD_ENABLE_MCP2515=0",
):
    if required not in service_hil_feeder:
        fail(f"feeder Service/HIL profile missing {required}")

overlay = (root / "tools" / "product_mbed_overlay.py").read_text(encoding="utf-8")
if "portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi" not in overlay:
    fail("feeder Service/HIL profile must use the pinned product Mbed artifact")

direct_write = "builtin_can_ref().write("
if main.count(direct_write) != 1:
    fail("built-in CAN must have exactly one direct driver write site")

driver_begin = main.find("builtin_can_driver_write(")
driver_end = main.find("\n}", driver_begin)
write_site = main.find(direct_write)
if driver_begin < 0 or driver_end < 0 or not (driver_begin < write_site < driver_end):
    fail("the only direct driver write must be owned by builtin_can_driver_write")

loop_begin = main.find("void loop() {")
if loop_begin < 0:
    fail("loop() not found")
loop_body = main[loop_begin:]
if loop_body.count("service_remote_control();") < 2:
    fail("loop must retain repeated bounded remote-control polls")

completion_poll = loop_body.find("service_builtin_can_tx_completions();")
remote_profile_branch = loop_body.find("#if BOARD_ENABLE_REMOTE_CONTROL")
first_early_return = loop_body.find("return;")
if completion_poll < 0:
    fail("loop must poll the FDCAN completion owner")
if remote_profile_branch >= 0 and completion_poll > remote_profile_branch:
    fail("completion polling must not depend on the RC profile")
if first_early_return >= 0 and completion_poll > first_early_return:
    fail("completion polling must precede every profile early return")

control_service = loop_body.find("service_remote_control();")
feeder_service = loop_body.find("service_feeder_uart_to_queue(")
uplink_service = loop_body.find("service_uplink(")
if feeder_service >= 0 and control_service > feeder_service:
    fail("control service must precede feeder draining")
if uplink_service >= 0 and control_service > uplink_service:
    fail("control service must precede uplink draining")
if "BOARD_WIFI_MAIN_IDLE_SLICE_MS" in loop_body:
    fail("control loop must not carry a fixed Wi-Fi idle delay")
if "rtos::ThisThread::yield();" not in loop_body:
    fail("control loop must use only a nonblocking worker yield")

for required in (
    "FifoEnqueueTracked",
    "submit_builtin_can_frame(",
    "builtin_can_tx_owner.submit(",
    "builtin_can_tx_owner.serviceCompletions(",
    "snapshot.txbrp, snapshot.txbto",
    "tracking_fault_latched_",
):
    if required not in main and required not in (
        root / "include" / "board" / "can" / "BuiltinCanTxOwner.h"
    ).read_text(encoding="utf-8"):
        fail(f"missing {required}")

completion_begin = main.find("static void builtin_can_tx_completion(")
completion_end = main.find("\n}", completion_begin)
if completion_begin < 0 or completion_end < 0:
    fail("completion callback not found")
completion_body = main[completion_begin:completion_end]
if "completion.transmitted()" not in completion_body:
    fail("CAN_TX_RAW completion callback must require transmitted outcome")
if completion_body.count("emit_can_tx_raw(") != 1:
    fail("completion callback must be the sole built-in CAN_TX_RAW publisher")
if (
    "if (completion.terminal && completion.frame.origin ==" not in completion_body
):
    fail("CONTROL_TX_EVIDENCE must be emitted only for a terminal Host outcome")
if "noteCanTxCompletion(" not in completion_body:
    fail("remote CAN success/failure must be driven by hardware completion")
if "const bool intentional_cancel =" not in completion_body:
    fail("completion callback must classify intentional cancellation")
if "const bool hardware_failure =" not in completion_body:
    fail("completion callback must classify hardware failure separately")
if "const bool tracking_failure =" not in completion_body:
    fail("completion callback must classify tracking failure separately")
if completion_body.count(
    "increment_builtin_can_counter(&builtin_can_tx_failed_total);"
) != 2:
    fail("hardware and tracking terminal failures need separate accounting")
if "else if (intentional_cancel)" not in completion_body:
    fail("intentional cancellation must have independent accounting")
if "if (hardware_failure || tracking_failure) {\n    builtin_can_tx_inhibit_latched = true;" not in completion_body:
    fail("hardware/tracking failure must latch local CAN TX inhibit")
if "else if (hardware_failure || tracking_failure) {\n      remote_control_runtime.noteCanTxCompletion(millis(), false);" not in completion_body:
    fail("hardware/tracking failure must reach remote runtime failure accounting")
if main.count("builtin_can_runtime_ready_for_health()") != 4:
    fail("CAN inhibit must feed its health helper, LED, and both health flags")
health_ready_begin = main.find(
    "static bool builtin_can_runtime_ready_for_health()"
)
health_ready_end = main.find("\n}", health_ready_begin)
if "!builtin_can_tx_inhibit_latched" not in main[
    health_ready_begin:health_ready_end
]:
    fail("built-in CAN runtime health must fail closed on TX inhibit")

for function_name in (
    "static void service_remote_control()",
    "static void handle_host_can_tx_request(",
    "static void service_builtin_can_tx_test()",
):
    begin = main.find(function_name)
    end = main.find("\n}", begin)
    if begin < 0 or end < 0:
        fail(f"{function_name} not found")
    if "emit_can_tx_raw(" in main[begin:end]:
        fail(f"{function_name} must not publish enqueue-time CAN_TX_RAW")

diagnostics_header = (
    root / "include" / "board" / "can" / "BuiltinFdcanDiagnostics.h"
).read_text(encoding="utf-8")
if "class BuiltinFdcanCan" not in diagnostics_header:
    fail("product FDCAN handle wrapper missing")
if "abortTxRequest(" not in diagnostics_header:
    fail("FDCAN abort boundary missing")

driver_source = (
    root / "src" / "board" / "can" / "BuiltinFdcanDiagnostics.cpp"
).read_text(encoding="utf-8")
if "HAL_FDCAN_AbortTxRequest(" not in driver_source:
    fail("deadline cancellation must call HAL_FDCAN_AbortTxRequest")

owner_source = (
    root / "src" / "board" / "can" / "BuiltinCanTxOwner.cpp"
).read_text(encoding="utf-8")
for required in (
    "tracking_fault_latched_ = true;",
    "BuiltinCanTxCompletionCode::DisappearedWithoutOutcome",
    "BuiltinCanTxCompletionCode::CancelRequestFailed",
    "if (transmitted)",
):
    if required not in owner_source:
        fail(f"completion owner missing {required}")
if "constexpr uint32_t kSupportedFlags = kExtendedFlag;" not in owner_source:
    fail("built-in CAN TX owner must reject RTR frames")
owner_header = (
    root / "include" / "board" / "can" / "BuiltinCanTxOwner.h"
).read_text(encoding="utf-8")
if "bool failure_previously_reported = false;" not in owner_header:
    fail("completion failure identity metadata missing")
for required in (
    "BuiltinCanTxDisposition::TransientRejected",
    "bool transientAdmissionFailure() const",
    "bool terminalFailure() const",
):
    if required not in owner_header + owner_source:
        fail(f"CAN admission outcome classification missing {required}")
if "slot->failure_reported = true;" not in owner_source:
    fail("owner must remember the first reported failure")

runtime_header = (
    root / "include" / "board" / "control" / "RemoteControlRuntime.h"
).read_text(encoding="utf-8")
runtime_source = (
    root / "src" / "board" / "control" / "RemoteControlRuntime.cpp"
).read_text(encoding="utf-8")
if "if (inputs.host_output_reserved)" not in runtime_source:
    fail("Host active/draining phase must silence every RC CAN origin")
mapper_header = (
    root / "include" / "board" / "control" / "VehicleCommandMapper.h"
).read_text(encoding="utf-8")
mapper_source = (
    root / "src" / "board" / "control" / "VehicleCommandMapper.cpp"
).read_text(encoding="utf-8")
if "noteCanTxEnqueueResult(" not in runtime_header:
    fail("runtime enqueue result API missing")
if "noteCanTxCompletion(" not in runtime_header:
    fail("runtime completion result API missing")
if "noteDriveDispatch(" not in (
    root / "include" / "board" / "control" / "ControlReleaseSchedule.h"
).read_text(encoding="utf-8"):
    fail("asynchronous safety stop must participate in drive release spacing")
enqueue_begin = runtime_source.find("void RemoteControlRuntime::noteCanTxEnqueueResult(")
enqueue_end = runtime_source.find("\n}", enqueue_begin)
if "terminal_failure || safety_neutral" not in runtime_source[enqueue_begin:enqueue_end]:
    fail("runtime must latch transient admission failure only for safety neutral")
if "can_tx_success" in runtime_source[enqueue_begin:enqueue_end]:
    fail("FIFO enqueue must not increment CAN TX success")
completion_runtime_begin = runtime_source.find(
    "void RemoteControlRuntime::noteCanTxCompletion("
)
completion_runtime_end = runtime_source.find("\n}", completion_runtime_begin)
if "can_tx_success" not in runtime_source[
    completion_runtime_begin:completion_runtime_end
]:
    fail("hardware completion must own CAN TX success accounting")

poll_begin = main.find("static void service_builtin_can_tx_completions()")
poll_end = main.find("\n}", poll_begin)
poll_body = main[poll_begin:poll_end]
if poll_body.find("const uint32_t now_us = micros();") > poll_body.find(
    "builtin_fdcan_diagnostics.snapshot()"
):
    fail("completion timestamp must be captured before the hardware snapshot")

wifi_downlink_begin = main.find("static void service_host_downlink(int budget) {")
wifi_downlink_end = main.find("\n}", wifi_downlink_begin)
wifi_downlink_body = main[wifi_downlink_begin:wifi_downlink_end]
if "BOARD_HOST_DOWNLINK_TRANSPORT_WIFI" not in wifi_downlink_body:
    fail("host downlink transport branch missing")
if "wifi_epoch != last_wifi_epoch" not in wifi_downlink_body:
    fail("Wi-Fi downlink parser must be scoped to a connection epoch")
if "HostControlCloseReason::TransportEpochClosed" not in wifi_downlink_body:
    fail("Wi-Fi epoch change must atomically close Host admission/epoch/HW")
if "#define BOARD_HOST_DOWNLINK_SERVICE_BYTE_BUDGET 256" not in main:
    fail("host downlink must use the bounded multi-record ingress budget")
if "service_host_downlink(BOARD_HOST_DOWNLINK_SERVICE_BYTE_BUDGET);" not in loop_body:
    fail("loop must use the bounded host downlink byte budget")
if "process();\n      if (len_ >= kBufferSize)" not in host_parser_source:
    fail("Host parser must consume complete records before full-buffer resync")

host_downlink_service = loop_body.find(
    "service_host_downlink(BOARD_HOST_DOWNLINK_SERVICE_BYTE_BUDGET);"
)
feeder_service = loop_body.find("service_feeder_uart_to_queue(")
bulk_uplink_service = loop_body.find("service_uplink(1024);")
if feeder_service >= 0 and host_downlink_service > feeder_service:
    fail("Host downlink must precede feeder draining")
if bulk_uplink_service >= 0 and host_downlink_service > bulk_uplink_service:
    fail("Host downlink must precede bulk uplink draining")
host_tx_begin = main.find("static void handle_host_can_tx_request(")
host_tx_end = main.find("\n}", host_tx_begin)
host_tx_body = main[host_tx_begin:host_tx_end]
for required in (
    "memcpy(data, &payload[csm::kHostCanTxRequestDataOffset], sizeof(data));",
    "(frame_flags & ~0x03u) != 0",
    "raw_can_id > 0x7FFu",
    "host_command_freshness.acceptCommand(",
    "submit_builtin_can_frame(",
    "FifoEnqueueTracked",
    "ControlReasonQueueFull",
    "One Host request is one immediate attempt at the physical 3-slot FDCAN",
):
    if required not in host_tx_body:
        fail(f"Host immediate HW-admission path missing {required}")
if "static constexpr uint32_t kServiceHilAllowedEhbCanId = 0x364u;" not in main or \
        "(can_id == kServiceHilAllowedEhbCanId && dlc == 8)" not in main:
    fail("Service/HIL static CAN allowlist must retain EHB ID 0x364 with DLC 8")
for forbidden in (
    "ServiceHilIntentRuntime",
    "ServiceSteeringCenterGuard",
    "service_hil_intent_runtime",
    "service_steering_center_guard",
    "is_valid_service_hil_payload",
    "service_service_hil_control",
    "service_steering_center_timeout",
):
    if forbidden in main + mapper_header + mapper_source:
        fail(f"Host raw path still contains semantic policy {forbidden}")
if (root / "include" / "board" / "control" / "ServiceHilIntentRuntime.h").exists():
    fail("retired Service/HIL semantic runtime header still exists")
if (root / "src" / "board" / "control" / "ServiceHilIntentRuntime.cpp").exists():
    fail("retired Service/HIL semantic runtime source still exists")
if "config.host_tx_queue_size = 0;" not in main:
    fail("legacy Host software retention capability must be zero")
if "config.hardware_tx_slots = csm::board::can::BuiltinCanTxOwner::kJournalSlots;" not in main:
    fail("CAPABILITY v7 must expose physical FDCAN slots separately")
if "builtin_can_tx_owner.activeJournalSlots() != 0" not in main:
    fail("new Host ARM must wait for unresolved CAN attempts of every origin")

for retired in (
    root / "include" / "board" / "can" / "CanTxCadenceQueue.h",
    root / "src" / "board" / "can" / "CanTxCadenceQueue.cpp",
):
    if retired.exists():
        fail(f"retired Host software cadence layer still exists: {retired.name}")
for forbidden in (
    "CanTxCadenceQueue",
    "host_can_tx_cadence",
    "service_host_can_tx_cadence",
    "BOARD_ENABLE_SERVICE_HIL_HOST_CAN_CADENCE",
):
    if forbidden in main:
        fail(f"retired Host software cadence symbol returned: {forbidden}")
for required in (
    "HostFreshnessResult::AnchorEstablished",
    "HostFreshnessResult::NotQualified",
    "config_.command_max_age_ms",
    "last_command_id_",
    "fault_latched_ = true",
):
    if required not in freshness_header + freshness_source:
        fail(f"Host sender-time freshness contract missing {required}")
for required in (
    "requestCancellation(BuiltinCanTxOrigin origin",
    "requestCancellationAll(BuiltinCanTxCancelReason reason",
):
    if required not in owner_header + owner_source:
        fail(f"FDCAN owner cancellation boundary missing {required}")
for required in (
    "#define BOARD_BUILTIN_CAN_TX_COMPLETION_TIMEOUT_US 0",
    "#define BOARD_HOST_HEARTBEAT_MAX_EXTRA_LAG_MS 0",
    "#define BOARD_HOST_CAN_TX_MAX_AGE_MS 0",
    "kThresholdQualificationExploratory",
):
    if required not in main:
        fail(f"exploratory threshold qualification contract missing {required}")
envelope = (
    root / "include" / "board" / "uplink" / "ProductUplinkEnvelope.h"
).read_text(encoding="utf-8")
for required in (
    "kProductEnabledRecordsPerSecond == 1095",
    "kProductEnabledWireBytesPerSecond == 131513",
    "kControlTxEvidencePayloadLen",
):
    if required not in envelope:
        fail(f"enabled uplink envelope missing {required}")
for forbidden in ("120000", "135000"):
    if forbidden in envelope:
        fail(f"unapproved uplink qualification number remains: {forbidden}")
uplink_policy = (
    root / "src" / "board" / "uplink" / "UplinkPriorityPolicy.cpp"
).read_text(encoding="utf-8")
delivery_begin = uplink_policy.find("UplinkDeliveryClass default_delivery_for_record")
delivery_body = uplink_policy[delivery_begin:]
if "case csm::RecordType::CanTxRaw:" not in delivery_body or \
        "return UplinkDeliveryClass::LatencyBounded;" not in delivery_body:
    fail("CAN_TX_RAW must use latency-bounded uplink delivery")
if "frame.data[0] = mapSteering(command.steer_permille);" not in mapper_source:
    fail("RC semantic mapper must remain intact")
if "if (command.auxiliary_permille != 0) {\n    frame.data[7]" not in mapper_source:
    fail("RC auxiliary is not an overlay on the mapped steering command")

for required in (
    "VehicleMdps0x007Only",
    "result.frames[result.frame_count++] = makeSteeringFrame(command, profile_);",
    "mapSafetyStop(cycle_sequence_)",
):
    if required not in mapper_header + mapper_source + runtime_source + main:
        fail(f"profile-scoped neutral/mapping contract missing {required}")

safety_header = (root / "include" / "board" / "SafetySupervisor.h").read_text(encoding="utf-8")
safety_source = (root / "src" / "board" / "SafetySupervisor.cpp").read_text(encoding="utf-8")
gateway_header = (root / "include" / "board" / "control" / "CanTxGateway.h").read_text(encoding="utf-8")
gateway_source = (root / "src" / "board" / "control" / "CanTxGateway.cpp").read_text(encoding="utf-8")
for forbidden in (
    "ArmKey",
    "arm_key",
    "CanTxEnable",
    "hardware_gate_allows",
    "RejectedHardwareGate",
):
    if forbidden in safety_header + safety_source + main + gateway_header + gateway_source:
        fail(f"removed external control interlock returned: {forbidden}")

feeder_header = (root / "include" / "board" / "feeder" / "FeederUartIngress.h").read_text(encoding="utf-8")
feeder_source = (root / "src" / "board" / "feeder" / "FeederUartIngress.cpp").read_text(encoding="utf-8")
if "std::atomic<uint32_t> pending_" not in feeder_header:
    fail("Feeder DMA ISR error event must be atomic")
note_error = feeder_source[feeder_source.find("void FeederUartIngress::noteDmaError") :]
note_error = note_error[: note_error.find("\n}")]
if "publishFromIsr()" not in note_error or "ingress_stats_" in note_error:
    fail("Feeder DMA ISR must publish only an event; main owns stats")

print("Control execution guard passed.")
