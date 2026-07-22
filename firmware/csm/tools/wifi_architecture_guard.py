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
    "WiFi.config(",
    "WiFi.beginAP(",
    "server_.begin(",
    "server_.acceptRaw(",
    "client_->send(",
    "client_->recv(",
    "owned->close(",
    "osPriorityNormal",
    "BOARD_WIFI_ACCEPT_POLL_MS",
):
    if token not in worker:
        fail(f"socket worker is missing required ownership marker {token!r}")

if "osPriorityBelowNormal" in worker:
    fail("socket worker may be starved by always-runnable normal-priority threads")

if "#define BOARD_WIFI_TX_CHUNK_BYTES 1024" not in worker_header:
    fail("Wi-Fi TX chunk must match the 1024-byte bounded pump budget")

if "FixedFrameByteQueue" not in mailbox_header or "FixedFrameQueue<" in mailbox_header:
    fail("Wi-Fi mailbox must use the bounded byte-pool queue")
for token in (
    "BOARD_WIFI_SINK_QUEUE_RECORDS=252",
    "BOARD_WIFI_SINK_QUEUE_BYTES=49152",
    "BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES=2048",
    "BOARD_CAN_RX_SEGMENT_FLUSH_US=20000",
):
    if token not in platformio:
        fail(f"product throughput envelope is missing {token!r}")

if "WifiTxProgressTracker" not in contract:
    fail("TX progress tracker is missing from the Wi-Fi contract")
for token in (
    "tx_progress_",
    "progress.close_no_progress",
    "WifiCloseReason::TransmitNoProgress",
):
    if token not in worker:
        fail(f"socket worker is missing deterministic TX progress policy {token!r}")

for token in (
    "queued.queued_records >= normal_limit",
    "queued.queued_records >= BOARD_WIFI",
):
    if token in worker:
        fail(f"queue occupancy must not close a live client: found {token!r}")

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

for token in (
    "enum class WifiRuntimeMode",
    "Disabled = 0",
    "AccessPointOnly = 1",
    "FullTcp = 2",
    "startup_attempt_limit = 1",
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
    "state_.counters.startup_attempt_total >=",
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
if "delay(BOARD_WIFI_MAIN_IDLE_SLICE_MS)" not in main:
    fail("main loop does not provide the bounded lower-priority Wi-Fi slice")

print("Wi-Fi architecture guard PASS")
