#include <Arduino.h>
#include <TCPSocket.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <atomic>
#include <chrono>
#include <mbed_config.h>
#include <rtos/EventFlags.h>

#ifndef BOARD_WIFI_RAW_BLOCKING
#define BOARD_WIFI_RAW_BLOCKING 0
#endif

#ifndef BOARD_WIFI_RAW_AP_SSID
#define BOARD_WIFI_RAW_AP_SSID "VSM-CSM-DEV"
#endif

#ifndef BOARD_WIFI_RAW_AP_PASSPHRASE
#define BOARD_WIFI_RAW_AP_PASSPHRASE "vsm-csm-dev"
#endif

#ifndef BOARD_WIFI_RAW_AP_CHANNEL
#define BOARD_WIFI_RAW_AP_CHANNEL 1
#endif

#ifndef BOARD_WIFI_RAW_TCP_PORT
#define BOARD_WIFI_RAW_TCP_PORT 3333
#endif

#ifndef BOARD_WIFI_RAW_CHUNK_BYTES
#define BOARD_WIFI_RAW_CHUNK_BYTES 4096
#endif

#ifndef BOARD_WIFI_RAW_MAX_WRITES_PER_WAKE
#define BOARD_WIFI_RAW_MAX_WRITES_PER_WAKE 64
#endif

#ifndef BOARD_WIFI_RAW_MAX_BYTES_PER_WAKE
#define BOARD_WIFI_RAW_MAX_BYTES_PER_WAKE 65536
#endif

#ifndef BOARD_WIFI_RAW_DRAIN_BUDGET_US
#define BOARD_WIFI_RAW_DRAIN_BUDGET_US 2000
#endif

#ifndef BOARD_WIFI_RAW_FALLBACK_MS
#define BOARD_WIFI_RAW_FALLBACK_MS 10
#endif

#ifndef BOARD_WIFI_RAW_BLOCKING_TIMEOUT_MS
#define BOARD_WIFI_RAW_BLOCKING_TIMEOUT_MS 1000
#endif

#ifndef CSM_FW_BUILD_ID
#define CSM_FW_BUILD_ID 0
#endif

#ifndef CSM_FW_SOURCE_ID32
#define CSM_FW_SOURCE_ID32 0
#endif

#ifndef CSM_FW_RUNTIME_CONTRACT_ID32
#define CSM_FW_RUNTIME_CONTRACT_ID32 0
#endif

namespace {

constexpr uint32_t kWakeSocket = 1u << 0;
constexpr uint32_t kWakeStart = 1u << 1;
#ifndef TCP_MSS
#define CSM_WIFI_RAW_LOCAL_TCP_MSS 1
#define TCP_MSS MBED_CONF_LWIP_TCP_MSS
#endif
constexpr uint32_t kEffectiveTcpSendBuffer = MBED_CONF_LWIP_TCP_SND_BUF;
constexpr uint32_t kEffectiveTcpWindow = MBED_CONF_LWIP_TCP_WND;
#ifdef CSM_WIFI_RAW_LOCAL_TCP_MSS
#undef TCP_MSS
#undef CSM_WIFI_RAW_LOCAL_TCP_MSS
#endif

class RawWifiServer final : public arduino::WiFiServer {
 public:
  TCPSocket* acceptRaw(nsapi_error_t* error) {
    if (sock == nullptr) {
      if (error != nullptr) *error = NSAPI_ERROR_NO_SOCKET;
      return nullptr;
    }
    return sock->accept(error);
  }
};

struct BenchCounters {
  uint64_t bytes_sent = 0;
  uint32_t send_calls = 0;
  uint32_t positive_calls = 0;
  uint32_t partial_calls = 0;
  uint32_t would_block_calls = 0;
  uint32_t zero_calls = 0;
  uint32_t error_calls = 0;
  uint32_t connect_total = 0;
  uint32_t disconnect_total = 0;
  uint32_t max_send_call_us = 0;
  uint32_t max_no_progress_ms = 0;
};

RawWifiServer server;
TCPSocket* client = nullptr;
rtos::EventFlags wake_flags;
std::atomic<uint32_t> sigio_total{0};
BenchCounters counters;
uint8_t tx_buffer[BOARD_WIFI_RAW_CHUNK_BYTES] = {};
uint16_t pending_offset = 0;
uint16_t pending_length = 0;
uint64_t stream_offset = 0;
uint32_t connection_epoch = 0;
uint32_t no_progress_started_ms = 0;
uint32_t last_accept_poll_ms = 0;
uint32_t last_report_ms = 0;
uint64_t last_report_bytes = 0;
uint32_t last_report_send_calls = 0;
uint32_t last_report_positive_calls = 0;
uint32_t last_report_partial_calls = 0;
uint32_t last_report_would_block_calls = 0;
uint32_t last_report_zero_calls = 0;
uint32_t last_report_error_calls = 0;
uint32_t last_report_sigio = 0;

void onSocketStateChanged() {
  sigio_total.fetch_add(1, std::memory_order_relaxed);
  wake_flags.set(kWakeSocket);
}

void fillPendingChunk() {
  if (pending_offset < pending_length) return;
  pending_offset = 0;
  pending_length = BOARD_WIFI_RAW_CHUNK_BYTES;
  for (uint16_t index = 0; index < pending_length; ++index) {
    tx_buffer[index] =
        static_cast<uint8_t>((stream_offset + index) & 0xFFu);
  }
}

void noteProgress(uint32_t now_ms) {
  if (no_progress_started_ms != 0) {
    const uint32_t duration_ms = now_ms - no_progress_started_ms;
    if (duration_ms > counters.max_no_progress_ms) {
      counters.max_no_progress_ms = duration_ms;
    }
    no_progress_started_ms = 0;
  }
}

void noteNoProgress(uint32_t now_ms) {
  if (no_progress_started_ms == 0) no_progress_started_ms = now_ms;
  const uint32_t duration_ms = now_ms - no_progress_started_ms;
  if (duration_ms > counters.max_no_progress_ms) {
    counters.max_no_progress_ms = duration_ms;
  }
}

void closeClient() {
  TCPSocket* closing = client;
  client = nullptr;
  pending_offset = 0;
  pending_length = 0;
  no_progress_started_ms = 0;
  if (closing != nullptr) {
    closing->sigio(nullptr);
    closing->close();
    counters.disconnect_total++;
    connection_epoch++;
  }
}

void serviceAccept(uint32_t now_ms) {
  if (client != nullptr ||
      static_cast<uint32_t>(now_ms - last_accept_poll_ms) < 25u) {
    return;
  }
  last_accept_poll_ms = now_ms;
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  TCPSocket* candidate = server.acceptRaw(&error);
  if (candidate == nullptr) return;

#if BOARD_WIFI_RAW_BLOCKING
  candidate->set_timeout(BOARD_WIFI_RAW_BLOCKING_TIMEOUT_MS);
#else
  candidate->set_blocking(false);
  candidate->sigio(mbed::callback(onSocketStateChanged));
#endif
  client = candidate;
  counters.connect_total++;
  connection_epoch++;
  stream_offset = 0;
  pending_offset = 0;
  pending_length = 0;
  no_progress_started_ms = 0;
  wake_flags.set(kWakeStart);
}

void pumpTransmit() {
  if (client == nullptr) return;
  const uint32_t started_us = micros();
  uint32_t writes = 0;
  uint32_t bytes = 0;
  while (client != nullptr &&
         writes < BOARD_WIFI_RAW_MAX_WRITES_PER_WAKE &&
         bytes < BOARD_WIFI_RAW_MAX_BYTES_PER_WAKE &&
         static_cast<uint32_t>(micros() - started_us) <
             BOARD_WIFI_RAW_DRAIN_BUDGET_US) {
    fillPendingChunk();
    const uint16_t remaining =
        static_cast<uint16_t>(pending_length - pending_offset);
    const uint32_t call_started_us = micros();
    const nsapi_size_or_error_t sent =
        client->send(tx_buffer + pending_offset, remaining);
    const uint32_t duration_us = micros() - call_started_us;
    counters.send_calls++;
    writes++;
    if (duration_us > counters.max_send_call_us) {
      counters.max_send_call_us = duration_us;
    }

    if (sent > 0) {
      const uint16_t progressed =
          sent > remaining ? remaining : static_cast<uint16_t>(sent);
      counters.positive_calls++;
      if (progressed < remaining) counters.partial_calls++;
      pending_offset = static_cast<uint16_t>(pending_offset + progressed);
      stream_offset += progressed;
      counters.bytes_sent += progressed;
      bytes += progressed;
      noteProgress(millis());
      continue;
    }

    if (sent == NSAPI_ERROR_WOULD_BLOCK) {
      counters.would_block_calls++;
      noteNoProgress(millis());
      break;
    }
    if (sent == 0) {
      counters.zero_calls++;
      noteNoProgress(millis());
      break;
    }
    counters.error_calls++;
    closeClient();
    break;
  }
}

void reportStats(uint32_t now_ms) {
  if (static_cast<uint32_t>(now_ms - last_report_ms) < 1000u) return;
  const uint32_t interval_ms = now_ms - last_report_ms;
  const uint64_t interval_bytes = counters.bytes_sent - last_report_bytes;
  const uint32_t current_sigio =
      sigio_total.load(std::memory_order_relaxed);
  Serial.print("WIFI_RAW_STAT mode=");
  Serial.print(BOARD_WIFI_RAW_BLOCKING ? "blocking" : "sigio");
  Serial.print(" epoch=");
  Serial.print(connection_epoch);
  Serial.print(" interval_ms=");
  Serial.print(interval_ms);
  Serial.print(" interval_bytes=");
  Serial.print(static_cast<unsigned long>(interval_bytes));
  Serial.print(" total_bytes=");
  Serial.print(static_cast<unsigned long>(counters.bytes_sent));
  Serial.print(" send_calls=");
  Serial.print(counters.send_calls - last_report_send_calls);
  Serial.print(" positive=");
  Serial.print(counters.positive_calls - last_report_positive_calls);
  Serial.print(" partial=");
  Serial.print(counters.partial_calls - last_report_partial_calls);
  Serial.print(" would_block=");
  Serial.print(counters.would_block_calls -
               last_report_would_block_calls);
  Serial.print(" zero=");
  Serial.print(counters.zero_calls - last_report_zero_calls);
  Serial.print(" errors=");
  Serial.print(counters.error_calls - last_report_error_calls);
  Serial.print(" sigio=");
  Serial.print(current_sigio - last_report_sigio);
  Serial.print(" connect=");
  Serial.print(counters.connect_total);
  Serial.print(" disconnect=");
  Serial.print(counters.disconnect_total);
  Serial.print(" max_send_us=");
  Serial.print(counters.max_send_call_us);
  Serial.print(" max_no_progress_ms=");
  Serial.print(counters.max_no_progress_ms);
  Serial.print(" tcp_mss=");
  Serial.print(MBED_CONF_LWIP_TCP_MSS);
  Serial.print(" tcp_snd_buf=");
  Serial.print(kEffectiveTcpSendBuffer);
  Serial.print(" tcp_wnd=");
  Serial.print(kEffectiveTcpWindow);
  Serial.print(" tcp_seg=");
  Serial.print(MBED_CONF_LWIP_MEMP_NUM_TCP_SEG);
  Serial.print(" pbuf_pool=");
  Serial.print(MBED_CONF_LWIP_PBUF_POOL_SIZE);
  Serial.print(" mem_size=");
  Serial.print(MBED_CONF_LWIP_MEM_SIZE);
  Serial.print(" tcpip_stack=");
  Serial.println(MBED_CONF_LWIP_TCPIP_THREAD_STACKSIZE);

  last_report_ms = now_ms;
  last_report_bytes = counters.bytes_sent;
  last_report_send_calls = counters.send_calls;
  last_report_positive_calls = counters.positive_calls;
  last_report_partial_calls = counters.partial_calls;
  last_report_would_block_calls = counters.would_block_calls;
  last_report_zero_calls = counters.zero_calls;
  last_report_error_calls = counters.error_calls;
  last_report_sigio = current_sigio;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const char* wifi_firmware = WiFi.firmwareVersion();
  WiFi.config(IPAddress(192, 168, 4, 1));
  const int status =
      WiFi.beginAP(BOARD_WIFI_RAW_AP_SSID,
                   BOARD_WIFI_RAW_AP_PASSPHRASE,
                   BOARD_WIFI_RAW_AP_CHANNEL);
  server.begin(BOARD_WIFI_RAW_TCP_PORT);

  Serial.print("WIFI_RAW_BOOT mode=");
  Serial.print(BOARD_WIFI_RAW_BLOCKING ? "blocking" : "sigio");
  Serial.print(" ap_status=");
  Serial.print(status);
  Serial.print(" server_ready=");
  Serial.print(static_cast<bool>(server) ? 1 : 0);
  Serial.print(" build_id=");
  Serial.print(static_cast<uint32_t>(CSM_FW_BUILD_ID), HEX);
  Serial.print(" source_id=");
  Serial.print(static_cast<uint32_t>(CSM_FW_SOURCE_ID32), HEX);
  Serial.print(" contract_id=");
  Serial.print(static_cast<uint32_t>(CSM_FW_RUNTIME_CONTRACT_ID32), HEX);
  Serial.print(" wifi_fw=");
  Serial.print(wifi_firmware == nullptr ? "unknown" : wifi_firmware);
  Serial.print(" tcp_mss=");
  Serial.print(MBED_CONF_LWIP_TCP_MSS);
  Serial.print(" tcp_snd_buf=");
  Serial.print(kEffectiveTcpSendBuffer);
  Serial.print(" tcp_wnd=");
  Serial.print(kEffectiveTcpWindow);
  Serial.print(" tcp_seg=");
  Serial.print(MBED_CONF_LWIP_MEMP_NUM_TCP_SEG);
  Serial.print(" pbuf_pool=");
  Serial.print(MBED_CONF_LWIP_PBUF_POOL_SIZE);
  Serial.print(" mem_size=");
  Serial.print(MBED_CONF_LWIP_MEM_SIZE);
  Serial.print(" tcpip_stack=");
  Serial.println(MBED_CONF_LWIP_TCPIP_THREAD_STACKSIZE);

  last_report_ms = millis();
}

void loop() {
  const uint32_t now_ms = millis();
  if (client == nullptr) {
    serviceAccept(now_ms);
    reportStats(now_ms);
    rtos::ThisThread::sleep_for(std::chrono::milliseconds(1));
    return;
  }

#if BOARD_WIFI_RAW_BLOCKING
  pumpTransmit();
#else
  wake_flags.wait_any(kWakeSocket | kWakeStart,
                      BOARD_WIFI_RAW_FALLBACK_MS,
                      true);
  pumpTransmit();
#endif
  reportStats(millis());
}
