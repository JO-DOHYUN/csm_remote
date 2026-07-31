from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"Control execution guard failed: {message}")


root = Path(__file__).resolve().parents[1]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8")
platformio = (root / "platformio.ini").read_text(encoding="utf-8")


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
    "BOARD_ENABLE_MDPS_BENCH_MAPPING=1",
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
if "noteCanTxCompletion(" not in completion_body:
    fail("remote CAN success/failure must be driven by hardware completion")
if (
    "const bool first_failure =" not in completion_body
    or "!completion.failure_previously_reported" not in completion_body
):
    fail("completion callback must identify the first failure per submission")
if completion_body.count(
    "increment_builtin_can_counter(&builtin_can_tx_failed_total);"
) != 1:
    fail("each submission failure must be counted at exactly one callback site")
if "else if (first_failure)" not in completion_body:
    fail("nonterminal first failure must enter failure accounting")
if "if (first_failure) {\n    builtin_can_tx_inhibit_latched = true;" not in completion_body:
    fail("first failure must latch local CAN TX inhibit")
if "else if (first_failure) {\n      remote_control_runtime.noteCanTxCompletion(millis(), false);" not in completion_body:
    fail("first failure must reach remote runtime failure accounting")
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
if "slot->failure_reported = true;" not in owner_source:
    fail("owner must remember the first reported failure")

runtime_header = (
    root / "include" / "board" / "control" / "RemoteControlRuntime.h"
).read_text(encoding="utf-8")
runtime_source = (
    root / "src" / "board" / "control" / "RemoteControlRuntime.cpp"
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
if "safety_supervisor.invalidateHostSession(millis());" not in wifi_downlink_body:
    fail("Wi-Fi epoch change must invalidate heartbeat, arm, and lease")

print("Control execution guard passed.")
