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
publisher_header = (
    ROOT / "include" / "board" / "uplink" / "CanonicalPublisher.h"
).read_text(encoding="utf-8")
publisher_source = (
    ROOT / "src" / "board" / "uplink" / "CanonicalPublisher.cpp"
).read_text(encoding="utf-8")
platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
product_envelope = (
    ROOT / "include" / "board" / "uplink" / "ProductUplinkEnvelope.h"
).read_text(encoding="utf-8")


def env_section(name: str) -> str:
    marker = f"[env:{name}]"
    start = platformio.find(marker)
    if start < 0:
        fail(f"PlatformIO environment is missing {name!r}")
    end = platformio.find("\n[env:", start + len(marker))
    return platformio[start:] if end < 0 else platformio[start:end]

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

if "#define BOARD_WIFI_TX_CHUNK_BYTES 2920" not in contract:
    fail("Wi-Fi TX chunk must match the measured 2920-byte MSS envelope")

if "FixedFrameByteQueue<" not in mailbox_header:
    fail("Wi-Fi mailbox must own the bounded live-only byte FIFO")
for token in (
    "BOARD_WIFI_SINK_QUEUE_RECORDS=128",
    "BOARD_WIFI_SINK_QUEUE_BYTES=8192",
    "BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES=2112",
    "BOARD_WIFI_STALL_TIMEOUT_MS=500",
    "BOARD_CAN_RX_SEGMENT_FLUSH_US=20000",
):
    if token not in platformio:
        fail(f"product throughput envelope is missing {token!r}")

feeder_product = env_section(
    "portenta_h7_m7_mid_feeder_uart_j4_remote_product_wifi"
)
if "BOARD_ENABLE_WIFI_DEEP_DIAGNOSTICS=1" not in feeder_product:
    fail("final feeder product must publish bounded 1 Hz TRANSPORT_DIAGNOSTIC")

if "WifiTxProgressTracker" not in contract:
    fail("TX progress tracker is missing from the Wi-Fi contract")
for token in (
    "BOARD_WIFI_STARTUP_ATTEMPT_LIMIT",
    "BOARD_WIFI_STARTUP_RETRY_MS",
    "wifiStartupAttemptsExhausted",
    "WifiStartupFailureBoundary",
    "wifiStartupRetryAllowed",
    "wifiObservedAgeMs",
    "bool coherent = false",
    "#define BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US 2000",
    "#define BOARD_WIFI_TX_MAX_WRITES_PER_PUMP 4",
    "#define BOARD_WIFI_TX_MAX_BYTES_PER_PUMP 11680",
    "#define BOARD_WIFI_TX_BATCH_TARGET_BYTES 1460",
    "#define BOARD_WIFI_CONNECTED_FALLBACK_MS 5",
    "#define BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS 20",
    "#define BOARD_WIFI_TX_LATENCY_BOUND_MAX_MS 2",
    "#define BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT 59",
):
    if token not in contract:
        fail(f"worker recovery/evidence contract is missing {token!r}")

for token in (
    "kProductEnabledWireBytesPerSecond == 111922",
    "kProductEnabledRecordsPerSecond == 686",
    "kProductUplinkMinimumBytesPerSecond = 120000",
    "kProductUplinkDesignBytesPerSecond = 135000",
    "productSegmentWireBytesPerSecond",
    "csm::kControlAckPayloadLen",
):
    if token not in product_envelope:
        fail(f"schema-derived product envelope is missing {token!r}")
if "BOARD_WIFI_PRODUCT_TARGET_BYTES_PER_SECOND" in contract + mailbox_header:
    fail("superseded hand-written Wi-Fi product rate remains in firmware")

for token in (
    "wifiStartupAttemptsExhausted(",
    "config_.startup_retry_ms",
    "WifiStartupFailureBoundary::OpaqueApStart",
    "WifiStartupFailureBoundary::AfterApStarted",
    "quarantineStartupFailure(",
    "bool WifiSocketWorker::rollbackNetwork()",
    "if (!startup_complete_ && !state_.startup_attempts_exhausted)",
    "return cleanup_confirmed && !server_opened_ && !ap_started_;",
):
    if token not in worker:
        fail(f"bounded AP startup recovery is missing {token!r}")

for token in (
    "state_revision_",
    "state_words_",
    "before == after && (after & 1u) == 0u",
):
    if token not in mailbox_header + mailbox_source:
        fail(f"coherent worker-state seqlock is missing {token!r}")
for token in ("state_lock_", "atomic_flag"):
    if token in mailbox_header + mailbox_source:
        fail(f"worker state publication may silently drop a write: found {token!r}")

for token in (
    "admission_revision_",
    "accepted_bytes_total_low_",
    "accepted_bytes_total_high_",
    "accepted_records_total_",
    "last_accepted_publish_seq_low_",
    "last_accepted_publish_seq_high_",
    "tryReadAdmissionSnapshot",
    "noteAccepted(frame)",
):
    if token not in mailbox_header + mailbox_source:
        fail(f"coherent admission ledger is missing {token!r}")
if "std::atomic<uint64_t>" in mailbox_header:
    fail("producer admission path must not require a non-native M7 64-bit atomic")
anchor_read = mailbox_source[
    mailbox_source.index("bool WifiWorkerMailbox::tryReadSessionAnchor(") :
    mailbox_source.index("bool WifiWorkerMailbox::tryStageTx(")
]
stage_tx = mailbox_source[
    mailbox_source.index("bool WifiWorkerMailbox::tryStageTx(") :
    mailbox_source.index("bool WifiWorkerMailbox::tryConsumeTx(")
]
if stage_tx.count(
    "producer_abort_gate_.load(std::memory_order_acquire)"
) < 2:
    fail("FIFO bytes can become stageable before admission commit")
if "producer_abort_gate_.load(std::memory_order_acquire) != 0u" not in stage_tx:
    fail("TX staging must reject every active producer/abort transition")
anchor_offer = mailbox_source[
    mailbox_source.index(
        "if (frame.type == csm::RecordType::StreamSession &&"
    ) :
    mailbox_source.index("const bool critical = frame.priority")
]
for token in (
    "memcpy(session_anchor_bytes_",
    "session_anchor_publish_seq_ = frame.publish_seq",
    "noteAccepted(frame)",
    "session_anchor_length_.store(frame.length, std::memory_order_release)",
):
    if token not in anchor_offer:
        fail(f"anchor publication contract is missing {token!r}")
if not (
    anchor_offer.index("memcpy(session_anchor_bytes_")
    < anchor_offer.index("noteAccepted(frame)")
    < anchor_offer.index(
        "session_anchor_length_.store(frame.length, std::memory_order_release)"
    )
):
    fail("anchor must publish bytes -> admission ledger -> release length")
if "session_anchor_length_.load(std::memory_order_acquire)" not in anchor_read:
    fail("anchor reader lacks release/acquire publication ordering")
if "producer_abort_gate_.load" in anchor_read:
    fail("anchor reader must rely on immutable release publication, not copy-then-reject")
for token in (
    "wifiPendingAcceptedBytes(",
    "state_.counters.socket_sent_bytes_total",
    "state_.counters.queue_aborted_bytes_total",
    "admission.accepted_records_total -",
    "state_.counters.frame_sent_total -",
    "state_.counters.queue_aborted_records_total",
):
    if token not in worker:
        fail(f"worker-owned conservation state is missing {token!r}")
for token in (
    "uint64_t accepted_bytes_total = 0",
    "uint64_t socket_sent_bytes_total = 0",
    "uint64_t queue_aborted_bytes_total = 0",
):
    if token not in contract + mailbox_header:
        fail(f"24-hour internal byte ledger is not 64-bit: missing {token!r}")

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
    "notifyWorker(WifiWakeTxData)",
    "notifyWorker(WifiWakeControl)",
    "live_session_generation_",
    "producer_abort_gate_",
    "compare_exchange_strong",
    "fetch_or(",
    "fetch_and(",
    "unaccepted_postcommit_bytes_",
    "unaccepted_postcommit_records_",
    "frame.type != csm::RecordType::StreamSession",
    "wifiShouldSignalOpaqueSendPressure(",
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
    "requestQueuePressureDisconnect();",
):
    if token not in mailbox_source:
        fail(f"live-FIFO producer isolation boundary is missing {token!r}")
for token in (
    "wifiQueuePressureReached(",
    "wifiShouldCloseQueuePressure(",
    "BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT",
    "kWifiPressureThresholdBytes",
    "kWifiFallbackIngressBytes",
    "pressure boundary cannot protect the critical byte reserve",
):
    if token not in worker + contract + mailbox_header:
        fail(f"worker batching pressure policy is missing {token!r}")

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
    "__wifi_tx_queue_end__ - __wifi_tx_queue_start__ == 0x2800",
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

if "mailbox_.liveSessionActive()" not in sink:
    fail("live session ownership is missing from the sink facade")
if "return connected() ? this : nullptr;" not in sink:
    fail("downlink must close at the logical live-epoch boundary")
if "return enabled_ && wifiRuntimeModeEnablesTcp(config_.runtime_mode)" not in sink:
    fail("AccessPointOnly mode could be advertised as a connected TCP sink")
if "if (!wifiRuntimeModeEnablesTcp(config_.runtime_mode))" not in sink:
    fail("AccessPointOnly offers are not explicitly disabled")

diagnostic = sink[
    sink.index("WifiTransportDiagnosticSnapshot WifiTcpSink::diagnosticSnapshot(") :
    sink.index("void WifiTcpSink::syncWorkerState(")
]
for token in (
    "worker_state_.accepted_bytes_total",
    "worker_state_.accepted_records_total",
    "worker_state_.queue_bytes",
    "worker_state_.queue_records",
    "worker.socket_sent_bytes_total",
    "worker.queue_aborted_bytes_total",
):
    if token not in diagnostic:
        fail(f"diagnostic conservation omits coherent worker field {token!r}")
for token in (
    "counters_.offer_accept_bytes_total",
    "counters_.offer_accept_total",
    "queue.queued_bytes +",
    "queue.queued_records +",
    "counters_.bytes_sent_total",
    "counters_.queue_aborted_bytes_total",
):
    if token in diagnostic:
        fail(f"diagnostic mixes independent conservation owners: {token!r}")

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
for token, corpus in (
    ("AppRxCommitAck = 21", typed_frame),
    ("LinkReliabilityDiagnostic = 22", typed_frame),
    ("kLinkReliabilityDiagnosticPayloadLen = 128", typed_records),
    ("RecordType::LinkReliabilityDiagnostic", priority_policy),
    ("BOARD_WIFI_TRANSPORT_DIAGNOSTIC_PERIOD_MS 1000", main),
):
    if token not in corpus:
        fail(f"reserved legacy link contract is missing {token!r}")

for token in (
    "configureSession(config_.boot_session_id)",
    "activateLiveSession()",
    "deactivateLiveSession()",
    "mailbox_.requestAbort()",
    "serviceSessionAnchor",
    "fresh_anchor_sent_total++",
    "state_.counters.frame_sent_total++",
    "state_.counters.last_sent_publish_seq = session_anchor_.publish_seq",
    "initial_queue.unsent_records",
    "socket_sent_bytes_total",
    "session_anchor_accounted_offset_",
    "fresh_anchor_pending_bytes",
    "fresh_anchor_pending_records",
    "current_queue.queued_bytes",
    "queue_abort_sequence_valid",
    "first_queue_aborted_publish_seq",
    "last_queue_aborted_publish_seq",
):
    if token not in worker:
        fail(f"live-only worker path is missing {token!r}")

for forbidden in (
    "build_link_reliability_diagnostic_payload",
    "type == RecordType::LinkReliabilityDiagnostic",
    "RecordType::AppRxCommitAck));",
):
    if forbidden in main:
        fail(f"legacy ACK/replay product runtime path remains {forbidden!r}")

for token in (
    "session_targets |= kWifiSessionSinkMask",
    "SessionAnnouncementReason::SinkEpochChanged",
    "publish_result.missed_required_sink_mask & kWifiSessionSinkMask",
    "wifi_tcp_sink.isolateMissedSessionAnchor(publish_result.publish_seq)",
):
    if token not in main:
        fail(f"fresh connection STREAM_SESSION path is missing {token!r}")
for token in (
    "uint8_t required_sink_mask = 0",
    "uint8_t missed_required_sink_mask = 0",
):
    if token not in publisher_header:
        fail(f"publisher anchor acceptance result is missing {token!r}")
for token in (
    "required_sink_mask & ~result.sink_accept_mask",
    "session_target_sink_mask_ = 0",
):
    if token not in publisher_source:
        fail(f"publisher anchor acceptance enforcement is missing {token!r}")
if "bool WifiTcpSink::isolateMissedSessionAnchor(" not in sink:
    fail("Wi-Fi facade lacks requested-anchor epoch isolation")

wifi_begin = main.index("wifi_tcp_sink.begin(wifi_sink_config)")
remote_begin = main.index("remote_control_runtime.begin(")
can_begin = main.index("builtin_can_tx_owner.begin(")
if wifi_begin < remote_begin or wifi_begin < can_begin:
    fail("Wi-Fi worker starts before RC/CAN control readiness")

close_client = worker[
    worker.index("void WifiSocketWorker::closeClient(") :
    worker.index("void WifiSocketWorker::closeSocket(")
]
if "mailbox_.requestAbort()" not in close_client:
    fail("normal TCP close must discard the closed epoch's unsent live copies")
for token in (
    "worker_state_.queue_bytes",
    "worker_state_.queue_records",
):
    if token not in sink:
        fail(f"transport diagnostic omits accepted-but-unsettled ledger {token!r}")
if "rollbackLastPush" in mailbox_header + mailbox_source:
    fail("epoch-close cleanup must use the producer/abort handshake, not an unsafe tail rollback")
if "if (mailbox_.queuePressureDisconnectLatched()) return;" not in worker:
    fail("worker may accept a new client before the old pressure epoch is acknowledged")
for begin, end in (
    ("WifiTransmitPumpResult WifiSocketWorker::serviceSessionAnchor(",
     "void WifiSocketWorker::serviceReceive("),
    ("WifiTransmitPumpResult WifiSocketWorker::serviceTransmit(",
     "void WifiSocketWorker::notePumpResult("),
):
    send_path = worker[worker.index(begin):worker.index(end)]
    if send_path.index("if (wifiSendResultHasPositiveProgress(sent))") > \
            send_path.index("if (disconnect_pending)"):
        fail("positive socket progress is discarded before isolation close")

for token in (
    "rx_epoch_generation_",
    "resetRxForNewEpoch()",
    "invalidateRxEpoch()",
    "rx_tail_.compare_exchange_weak(",
):
    if token not in mailbox_header + mailbox_source:
        fail(f"RX epoch fencing is missing {token!r}")
if "mailbox_.discardRx()" in sink:
    fail("facade must not reset the worker-owned RX epoch")
if "observed_rx_generation == current_rx_generation" not in sink:
    fail("facade opens downlink without observing the current RX generation")
accept_path = worker[
    worker.index("void WifiSocketWorker::serviceAccept(") :
    worker.index("WifiTransmitPumpResult WifiSocketWorker::serviceSessionAnchor(")
]
if accept_path.index("resetRxForNewEpoch()") > accept_path.index(
    "activateLiveSession()"
):
    fail("new RX epoch is exposed before its stale bytes are reset")
if close_client.index("deactivateLiveSession()") > close_client.index(
    "invalidateRxEpoch()"
):
    fail("closed RX epoch is invalidated before producer/downlink admission closes")

anchor_send = worker[
    worker.index("WifiTransmitPumpResult WifiSocketWorker::serviceSessionAnchor(") :
    worker.index("void WifiSocketWorker::serviceReceive(")
]
fifo_send = worker[
    worker.index("WifiTransmitPumpResult WifiSocketWorker::serviceTransmit(") :
    worker.index("void WifiSocketWorker::notePumpResult(")
]
for name, send_path in (("anchor", anchor_send), ("FIFO", fifo_send)):
    if send_path.index("closePendingIsolationBeforeSocketSend()") > \
            send_path.index("client_->send("):
        fail(f"{name} issues a vendor send before its fresh isolation gate")
    positive = send_path.index("if (wifiSendResultHasPositiveProgress(sent))")
    post = send_path.index("closePendingIsolationAfterPositiveSend()", positive)
    if post < positive:
        fail(f"{name} does not re-check isolation after positive accounting")
    if send_path.index("refreshAdmissionSnapshotForSettlement()") > \
            send_path.index("client_->send("):
        fail(f"{name} can settle bytes beyond its coherent admission cache")
for token in (
    "queuePressureDisconnectLatched()",
    "wifiSocketSendPermitted(",
    "WifiPostSendIsolation::QueuePressure",
    "WifiPostSendIsolation::IsolationRequest",
):
    if token not in worker + contract:
        fail(f"per-send isolation gate is missing {token!r}")

abort_path = worker[
    worker.index("void WifiSocketWorker::applyAbortRequest(") :
    worker.index("void WifiSocketWorker::noteSocketError(")
]
if abort_path.index("refreshAdmissionSnapshotForSettlement()") > \
        abort_path.index("tryApplyAbort("):
    fail("abort settlement can advance beyond its coherent admission cache")
publish_state = worker[
    worker.index("void WifiSocketWorker::publishState(") :
    worker.index("void WifiSocketWorker::sampleStack(")
]
for token in (
    "if (!force) return;",
    "admission = last_admission_snapshot_;",
    "mailbox_.requestAdmissionReconcile();",
    "mailbox_.publishState(state_);",
):
    if token not in publish_state:
        fail(f"forced state publication can be dropped: missing {token!r}")
for token in (
    "admission_reconcile_requested_",
    "requestAdmissionReconcile()",
    "notifyWorker(WifiWakeControl)",
):
    if token not in mailbox_header + mailbox_source:
        fail(f"producer-release admission reconciliation is missing {token!r}")

for token in (
    "kTransportDiagnosticSchema = 3",
    "kTransportDiagnosticAcceptedRecordsOffset",
    "kTransportDiagnosticRejectedRecordsOffset",
    "kTransportDiagnosticAbortedBytesOffset",
    "kTransportDiagnosticAbortedRecordsOffset",
    "kTransportDiagnosticFirstLostPublishSeqOffset",
    "kTransportDiagnosticLastLostPublishSeqOffset",
):
    if token not in typed_records:
        fail(f"schema-3 conservation evidence is missing {token!r}")

print("Wi-Fi architecture guard PASS")
