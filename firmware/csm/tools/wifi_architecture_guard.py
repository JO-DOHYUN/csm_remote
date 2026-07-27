#!/usr/bin/env python3
"""Static ownership and runtime-mode guard for the CSM Wi-Fi boundary."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SINK = ROOT / "src" / "board" / "uplink" / "WifiTcpSink.cpp"
WORKER = ROOT / "src" / "board" / "uplink" / "WifiSocketWorker.cpp"
CONTRACT = ROOT / "include" / "board" / "uplink" / "WifiWorkerContract.h"


def fail(message):
    print(f"Wi-Fi architecture guard FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


sink = SINK.read_text(encoding="utf-8")
worker = WORKER.read_text(encoding="utf-8")
contract = CONTRACT.read_text(encoding="utf-8")
worker_header = (ROOT / "include" / "board" / "uplink" / "WifiSocketWorker.h").read_text(
    encoding="utf-8"
)
mailbox_header = (ROOT / "include" / "board" / "uplink" / "WifiWorkerMailbox.h").read_text(
    encoding="utf-8"
)
mailbox_source = (
    ROOT / "src" / "board" / "uplink" / "WifiWorkerMailbox.cpp"
).read_text(encoding="utf-8")
platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")

for token in (
    "WiFi.",
    "TCPSocket",
    "acceptRaw(",
    "->send(",
    "->recv(",
    "->close(",
    "delete ",
):
    if token in sink:
        fail(f"facade owns forbidden network operation {token!r}")

for token in (
    "WhdSoftAPInterface::get_default_instance()",
    "ap_interface_->set_network(",
    "ap_interface_->start(",
    "config_.channel, true, nullptr, config_.ap_sta_concur",
    "server_.open(ap_interface_)",
    "server_.setsockopt(",
    "server_.bind(",
    "server_.listen(1)",
    "server_.accept(",
    "client_->send(",
    "client_->recv(",
    "candidate->set_blocking(false)",
    "owned->close(",
    "osPriorityNormal",
    "BOARD_WIFI_ACCEPT_POLL_MS",
    "wake_flags_.wait_any(",
    "candidate->sigio(",
    "owned->sigio(nullptr)",
    "WifiWakeTxData",
    "WifiWakeSocketState",
    "WifiWakeControl",
):
    if token not in worker:
        fail(f"socket worker is missing required ownership marker {token!r}")

if "osPriorityBelowNormal" in worker:
    fail("socket worker may be starved by always-runnable normal-priority threads")

for token in ("sleep_for(", "BOARD_WIFI_WORKER_PERIOD_MS"):
    if token in worker or token in contract:
        fail(f"periodic polling remains the primary Wi-Fi trigger: found {token!r}")

if "#define BOARD_WIFI_TX_CHUNK_BYTES 1024" not in worker_header:
    fail("Wi-Fi TX chunk must match the 1024-byte nonblocking pump budget")

if "FixedFrameByteQueue" not in mailbox_header or "FixedFrameQueue<" in mailbox_header:
    fail("Wi-Fi mailbox must use the bounded byte-pool queue")
for token in (
    "BOARD_WIFI_SINK_QUEUE_RECORDS=1280",
    "BOARD_WIFI_SINK_QUEUE_BYTES=65520",
    "BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES=2112",
    "BOARD_WIFI_STALL_TIMEOUT_MS=5000",
    "BOARD_CAN_RX_SEGMENT_FLUSH_US=20000",
):
    if token not in platformio:
        fail(f"product throughput envelope is missing {token!r}")

if "WifiTxProgressTracker" not in contract:
    fail("TX progress tracker is missing from the Wi-Fi contract")
for token in (
    "BOARD_WIFI_STARTUP_ATTEMPT_LIMIT",
    "BOARD_WIFI_STARTUP_RETRY_MS",
    "wifiStartupAttemptsExhausted",
    "wifiObservedAgeMs",
    "bool coherent = false",
):
    if token not in contract:
        fail(f"worker recovery/evidence contract is missing {token!r}")

for token in (
    "wifiStartupAttemptsExhausted(",
    "config_.startup_retry_ms",
):
    if token not in worker:
        fail(f"bounded AP startup recovery is missing {token!r}")

for token in (
    "snapshot.coherent = true",
    "return WifiWorkerCallSnapshot{}",
):
    if token not in mailbox_source:
        fail(f"coherent worker-call snapshot boundary is missing {token!r}")
if "!call.coherent" not in sink:
    fail("facade must reject incoherent worker-call snapshots")
if sink.count("wifiObservedAgeMs(") < 3:
    fail("facade cross-thread timestamps must use wrap-safe observed age")
for token in (
    "tx_progress_",
    "progress.close_no_progress",
    "WifiCloseReason::TransmitNoProgress",
):
    if token not in worker:
        fail(f"socket worker is missing deterministic TX progress policy {token!r}")

if "client_->set_timeout(" in worker:
    fail("product worker send path must remain nonblocking")

service_client = worker[
    worker.index("void WifiSocketWorker::serviceClient(") :
    worker.index("void WifiSocketWorker::serviceAccept(")
]
if service_client.index("serviceReceive(") > service_client.index(
    "serviceTransmit("
):
    fail("worker must service downlink before the nonblocking TX pump")

for token in (
    "BOARD_WIFI_QUEUE_PRESSURE_NO_PROGRESS_GRACE_MS",
    "queue_pressure_no_progress_grace_ms",
    "wifiShouldIsolateQueuePressure",
):
    if token in contract or token in worker:
        fail(f"legacy high-water timer close remains: found {token!r}")

for token in (
    "queued_bytes_",
    "queued_records_",
    "queue_high_water_bytes_",
    "queue_high_water_records_",
    "updateQueueSnapshot",
):
    if token in mailbox_header or token in mailbox_source:
        fail(f"mailbox contains a racing cached queue snapshot: found {token!r}")

for token in (
    "queuePressureDisconnectRequestSequence",
    "queuePressureDisconnectHandledSequence",
    "queue_pressure_disconnect_latched_",
    "compare_exchange_strong",
):
    if token not in mailbox_header + mailbox_source:
        fail(f"one-shot admission isolation latch is missing {token!r}")

for token in (
    "WifiWorkerNotifier",
    "setNotifier",
    "empty_to_nonempty_wake_total",
    "latency_wake_total",
    "latency_records_",
    "notifyWorker(WifiWakeTxData)",
    "notifyWorker(WifiWakeControl)",
):
    if token not in contract + mailbox_header + mailbox_source:
        fail(f"event-driven producer wake boundary is missing {token!r}")

for token in (
    "handled_queue_pressure_disconnect_sequence_",
    "closeClient(WifiCloseReason::QueuePressure)",
    "markQueuePressureDisconnectHandled",
):
    if token not in worker_header + worker:
        fail(f"worker QueuePressure close boundary is missing {token!r}")

for token in (
    "wifiQueuePressureReached(",
    "BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT",
    "requestQueuePressureDisconnect();",
):
    if token not in mailbox_source:
        fail(f"pre-full producer isolation boundary is missing {token!r}")

for token in (
    "AcceptExtraClient",
    "CloseExtraClient",
    "last_extra_accept_ms_",
):
    if token in worker:
        fail(f"active-client path must not poll the listening socket: found {token!r}")

for token in ("delete owned", "delete client_", "delete candidate"):
    if token in worker:
        fail(
            "accepted TCPSocket lifetime violates Mbed close-only ownership: "
            f"found {token!r}"
        )

for token in ("WiFi.beginAP(", "WiFi.config(", "WiFiServer", "acceptRaw("):
    if token in worker + worker_header:
        fail(f"Arduino STA+AP/server wrapper remains in product worker: {token!r}")

for token in ("config_.ap_sta_concur", "BOARD_WIFI_AP_STA_CONCUR"):
    if token not in worker + contract:
        fail(f"validated WHD compatibility mode is missing {token!r}")

for token in (
    "UplinkDeliveryClass::LatencyBounded",
    "batch_target_bytes",
    "latency_bound_max_ms",
):
    if token not in mailbox_source + worker + contract:
        fail(f"byte/latency batching contract is missing {token!r}")

linker = (ROOT / "linker" / "portenta_h7_m7_product.ld").read_text(
    encoding="utf-8"
)
main_source = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
for token in (
    ".wifi_tx_queue_dtcm (NOLOAD)",
    "__wifi_tx_queue_end__ - __wifi_tx_queue_start__ == 0x14FF0",
    "LENGTH(DTCMRAM) - 0x8000",
):
    if token not in linker:
        fail(f"DTCM queue ownership guard is missing {token!r}")
for token in ("WifiTcpSink::TxStorage wifi_tx_storage", "wifi_tcp_sink(wifi_tx_storage)"):
    if token not in main_source:
        fail(f"product Wi-Fi queue storage injection is missing {token!r}")

for token in (
    "enum class WifiRuntimeMode",
    "Disabled = 0",
    "AccessPointOnly = 1",
    "FullTcp = 2",
    "startup_attempt_limit = BOARD_WIFI_STARTUP_ATTEMPT_LIMIT",
    "worker_started",
    "ap_ready",
    "server_ready",
    "tcp_enabled",
):
    if token not in contract:
        fail(f"runtime-mode contract is missing {token!r}")

disabled_guard = "if (!wifiRuntimeModeStartsWorker(config_.runtime_mode)) return false;"
worker_construction = "static WifiSocketWorker socket_worker(mailbox_);"
if disabled_guard not in sink or sink.index(disabled_guard) > sink.index(worker_construction):
    fail("Disabled mode is not rejected before worker construction/start")

if "return enabled_ && wifiRuntimeModeEnablesTcp(config_.runtime_mode)" not in sink:
    fail("AccessPointOnly mode could be advertised as a connected TCP sink")
if "if (!wifiRuntimeModeEnablesTcp(config_.runtime_mode))" not in sink:
    fail("AccessPointOnly offers are not explicitly disabled")

ap_only_gate = "if (!wifiRuntimeModeEnablesTcp(config_.runtime_mode)) return true;"
server_start = "beginCall(WifiWorkerCallPhase::BeginServer)"
if ap_only_gate not in worker or worker.index(ap_only_gate) > worker.index(server_start):
    fail("AccessPointOnly mode does not stop before server startup")

for token in (
    "if (!state_.tcp_enabled)",
    "wifiStartupAttemptsExhausted(",
    "config_.startup_attempt_limit",
    "state_.startup_attempts_exhausted = true",
    "logical sink quarantine",
    "cannot cancel a",
):
    corpus = sink if token in ("logical sink quarantine", "cannot cancel a") else worker
    if token not in corpus:
        fail(f"bounded/logical isolation boundary is missing {token!r}")

if "retry_network_after_ms_" in worker:
    fail("legacy unbounded one-second startup retry remains")

for token in (
    "Watchdog",
    "NVIC_SystemReset",
    "emit_record(",
    "record_runtime_breadcrumb(",
    "runtime_diagnostic_commit",
    ".terminate(",
):
    if token in worker:
        fail(f"socket worker contains forbidden cross-boundary action {token!r}")

main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
if "BOARD_WIFI_MAIN_IDLE_SLICE_MS" in main:
    fail("main loop must not impose a fixed Wi-Fi idle delay")
if "rtos::ThisThread::yield();" not in main:
    fail("main loop does not yield non-blockingly to the Wi-Fi worker")

typed_frame = (ROOT / "include" / "protocol" / "TypedFrame.h").read_text(
    encoding="utf-8"
)
typed_records = (ROOT / "include" / "protocol" / "TypedRecords.h").read_text(
    encoding="utf-8"
)
priority_policy = (
    ROOT / "src" / "board" / "uplink" / "UplinkPriorityPolicy.cpp"
).read_text(encoding="utf-8")
transport_diagnostic = (
    ROOT / "src" / "board" / "uplink" / "WifiTransportDiagnostic.cpp"
).read_text(encoding="utf-8")
for token, corpus in (
    ("TransportDiagnostic = 20", typed_frame),
    ("kTransportDiagnosticPayloadLen = 128", typed_records),
    ("RecordType::TransportDiagnostic", priority_policy),
    ("BOARD_WIFI_TRANSPORT_DIAGNOSTIC_PERIOD_MS 1000", main),
    ("build_wifi_transport_diagnostic_payload", main + transport_diagnostic),
    ("if (type == RecordType::TransportDiagnostic)", main),
):
    if token not in corpus:
        fail(f"bounded canonical transport evidence is missing {token!r}")

print("Wi-Fi architecture guard PASS")
