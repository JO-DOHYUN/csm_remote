#include <Arduino.h>

#include <string.h>

#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"
#include "board/remote/RemoteSharedMemory.h"

#if !defined(BOARD_ENABLE_M4_REMOTE_FRONTEND) || BOARD_ENABLE_M4_REMOTE_FRONTEND != 1
#error "m4_remote_frontend.cpp requires BOARD_ENABLE_M4_REMOTE_FRONTEND=1"
#endif

#ifndef BOARD_M4_REMOTE_BAUD
#define BOARD_M4_REMOTE_BAUD 416666UL
#endif

#ifndef BOARD_M4_REMOTE_STALE_MS
#define BOARD_M4_REMOTE_STALE_MS 100UL
#endif

#ifndef BOARD_M4_REMOTE_PUBLISH_MS
#define BOARD_M4_REMOTE_PUBLISH_MS 20UL
#endif

#ifndef BOARD_M4_REMOTE_LINK_STATISTICS_STALE_MS
#define BOARD_M4_REMOTE_LINK_STATISTICS_STALE_MS 500UL
#endif

namespace {

using namespace csm::board::remote;

CrsfParser parser;
RcNormalizer normalizer;
M4RemoteMailboxWriter mailbox_writer;
M4RemoteMailboxFrame mailbox_frame;
RemoteFrontendDiagnostics diagnostics;
RcSample current_sample;
CrsfLinkStatistics link_statistics;

uint32_t m4_boot_id = 0;
uint32_t heartbeat_sequence = 0;
uint32_t last_byte_us = 0;
uint32_t last_rc_ms = 0;
uint32_t last_link_statistics_ms = 0;
uint32_t last_publish_ms = 0;
uint32_t last_heartbeat_tx_ms = 0;
uint32_t last_flight_mode_tx_ms = 0;
uint32_t telemetry_sequence = 0;
uint16_t rc_sequence = 0;
bool has_rc_sample = false;
bool has_link_statistics = false;
RemoteTelemetrySlot telemetry;
uint32_t malformed_total_at_last_rc = 0;

uint32_t frontendMalformedTotal() {
  return diagnostics.rejected_length + diagnostics.rejected_crc +
         diagnostics.inter_byte_resets;
}

void updateSampleState(uint32_t now_ms) {
  if (!has_rc_sample) current_sample.m4_time_ms = now_ms;
  current_sample.malformed_count = parser.malformedTotal();
  diagnostics.last_rc_age_ms = has_rc_sample ? now_ms - last_rc_ms : 0xFFFFFFFFu;
  diagnostics.last_link_statistics_age_ms = has_link_statistics
      ? now_ms - last_link_statistics_ms
      : 0xFFFFFFFFu;
  current_sample.link_quality = has_link_statistics
      ? link_statistics.uplink_link_quality : kRemoteMetricUnknown;
  current_sample.rssi_hint = has_link_statistics
      ? (link_statistics.uplink_rssi_ant1_dbm_magnitude <
                 link_statistics.uplink_rssi_ant2_dbm_magnitude
             ? link_statistics.uplink_rssi_ant1_dbm_magnitude
             : link_statistics.uplink_rssi_ant2_dbm_magnitude)
      : kRemoteMetricUnknown;

  if (has_link_statistics && link_statistics.uplink_link_quality == 0) {
    current_sample.sample_state = RcSampleState::Failsafe;
  } else if (!has_rc_sample) {
    current_sample.sample_state =
        (diagnostics.valid_frames != 0 || frontendMalformedTotal() != 0)
            ? RcSampleState::ProtocolFault
            : RcSampleState::Lost;
  } else if (now_ms - last_rc_ms > BOARD_M4_REMOTE_STALE_MS) {
    current_sample.sample_state =
        frontendMalformedTotal() != malformed_total_at_last_rc
            ? RcSampleState::ProtocolFault
            : RcSampleState::Stale;
  } else if (has_link_statistics &&
             now_ms - last_link_statistics_ms >
                 BOARD_M4_REMOTE_LINK_STATISTICS_STALE_MS) {
    // Fresh channel frames with a dead link-statistics lane are not a
    // trustworthy R16SM session. Keep authority reserved and fail closed.
    current_sample.sample_state = RcSampleState::ProtocolFault;
  } else {
    current_sample.sample_state = RcSampleState::Ok;
  }
}

void publishSample(uint32_t now_ms) {
  updateSampleState(now_ms);
  const auto result = mailbox_writer.publishSample(current_sample, &mailbox_frame);
  if (!result.accepted) return;
  ++heartbeat_sequence;
  ++diagnostics.mailbox_publishes;
  if (!publishRemoteSharedSample(m4_boot_id, heartbeat_sequence, mailbox_frame,
                                 diagnostics)) {
    ++diagnostics.shared_publish_failures;
  }
  last_publish_ms = now_ms;
}

void handleRcFrame(uint32_t now_ms, const CrsfFrame& frame) {
  CrsfRcChannels channels;
  if (decodeCrsfRcChannelsPacked(frame, &channels) != CrsfDecodeStatus::Ok) {
    return;
  }
  ++diagnostics.rc_frames;
  diagnostics.raw_ch2 = channels.raw[1];
  diagnostics.raw_ch4 = channels.raw[3];
  for (uint8_t index = 0; index < kRcChannelCount; ++index) {
    diagnostics.raw_channels[index] = channels.raw[index];
  }
  const auto normalized = normalizer.normalizeCrsfChannels(
      now_ms, ++rc_sequence, channels,
      has_link_statistics ? link_statistics.uplink_link_quality : kRemoteMetricUnknown,
      has_link_statistics
          ? (link_statistics.uplink_rssi_ant1_dbm_magnitude <
                     link_statistics.uplink_rssi_ant2_dbm_magnitude
                 ? link_statistics.uplink_rssi_ant1_dbm_magnitude
                 : link_statistics.uplink_rssi_ant2_dbm_magnitude)
          : kRemoteMetricUnknown,
      0);
  if (!normalized.accepted) {
    ++diagnostics.normalization_rejects;
    diagnostics.last_normalize_reject_detail = normalized.reject_detail;
    return;
  }
  ++diagnostics.accepted_rc_frames;
  diagnostics.last_normalize_reject_detail = 0;
  current_sample = normalized.sample;
  malformed_total_at_last_rc = frontendMalformedTotal();
  has_rc_sample = true;
  last_rc_ms = now_ms;
}

void handleFrame(uint32_t now_ms, const CrsfFrame& frame) {
  ++diagnostics.valid_frames;
  diagnostics.last_address = frame.address;
  diagnostics.last_type = frame.type;
  if (frame.type == kCrsfFrameTypeRcChannelsPacked) {
    handleRcFrame(now_ms, frame);
  } else if (frame.type == kCrsfFrameTypeLinkStatistics) {
    CrsfLinkStatistics decoded;
    if (decodeCrsfLinkStatistics(frame, &decoded) == CrsfDecodeStatus::Ok) {
      link_statistics = decoded;
      has_link_statistics = true;
      last_link_statistics_ms = now_ms;
      diagnostics.link_statistics_valid = 1;
      diagnostics.uplink_rssi_ant1 = decoded.uplink_rssi_ant1_dbm_magnitude;
      diagnostics.uplink_rssi_ant2 = decoded.uplink_rssi_ant2_dbm_magnitude;
      diagnostics.uplink_snr = decoded.uplink_snr_db;
      diagnostics.active_antenna = decoded.active_antenna;
      diagnostics.rf_profile = decoded.rf_profile;
      diagnostics.uplink_rf_power = decoded.uplink_rf_power;
      diagnostics.downlink_rssi = decoded.downlink_rssi_dbm_magnitude;
      diagnostics.downlink_link_quality = decoded.downlink_link_quality;
      diagnostics.downlink_snr = decoded.downlink_snr_db;
      ++diagnostics.link_frames;
    }
  }
}

bool sendTelemetryFrame(uint8_t type, const uint8_t* payload, uint8_t payload_len) {
  uint8_t frame[kCrsfMaxFrameBytes] = {};
  const uint8_t frame_len = buildCrsfBroadcastFrame(
      type, payload, payload_len, frame, sizeof(frame));
  if (frame_len == 0) {
    ++diagnostics.serial_write_failures;
    return false;
  }
  const size_t written = Serial3.write(frame, frame_len);
  if (written != frame_len) {
    ++diagnostics.serial_write_failures;
    return false;
  }
  ++diagnostics.telemetry_tx_frames;
  diagnostics.telemetry_tx_bytes += frame_len;
  return true;
}

void serviceTelemetry(uint32_t now_ms) {
  RemoteTelemetrySlot next;
  uint32_t next_sequence = 0;
  if (readRemoteTelemetry(telemetry_sequence, &next, &next_sequence)) {
    telemetry = next;
    telemetry_sequence = next_sequence;
  }

  if (now_ms - last_heartbeat_tx_ms >= 1000u) {
    last_heartbeat_tx_ms = now_ms;
    const uint8_t payload[2] = {0x00, 0xC8};
    sendTelemetryFrame(kCrsfFrameTypeHeartbeat, payload, sizeof(payload));
  }
  if (now_ms - last_flight_mode_tx_ms >= 500u) {
    last_flight_mode_tx_ms = now_ms;
    uint8_t payload[17] = {};
    size_t length = strnlen(telemetry.flight_mode, sizeof(telemetry.flight_mode));
    if (length == 0) {
      memcpy(payload, "CSM SAFE", 8);
      length = 8;
    } else {
      memcpy(payload, telemetry.flight_mode, length);
    }
    payload[length++] = 0;
    sendTelemetryFrame(kCrsfFrameTypeFlightMode, payload,
                       static_cast<uint8_t>(length));
  }
}

}  // namespace

void setup() {
  RcNormalizerConfig config;
  config.configured = true;
  normalizer.configure(config);
  parser.reset();
  mailbox_writer.reset();
  mailbox_writer.clearFrame(&mailbox_frame);
  current_sample = {};
  diagnostics = {};
  diagnostics.uart_baud = BOARD_M4_REMOTE_BAUD;
  m4_boot_id = initializeRemoteSharedMemoryForM4();
  Serial3.begin(BOARD_M4_REMOTE_BAUD, SERIAL_8N1);
  publishSample(millis());
}

void loop() {
  const uint32_t now_ms = millis();
  while (Serial3.available() > 0) {
    const int value = Serial3.read();
    if (value < 0) break;
    const uint32_t now_us = micros();
    if (parser.bufferedBytes() != 0 &&
        now_us - last_byte_us > kCrsfInterByteTimeoutUs) {
      parser.reset();
      ++diagnostics.inter_byte_resets;
    }
    last_byte_us = now_us;
    ++diagnostics.rx_bytes;
    const auto result = parser.ingest(static_cast<uint8_t>(value));
    if (result.status == CrsfParseStatus::FrameReady) {
      handleFrame(now_ms, result.frame);
    } else if (result.status == CrsfParseStatus::RejectedLength) {
      ++diagnostics.rejected_length;
    } else if (result.status == CrsfParseStatus::RejectedCrc) {
      ++diagnostics.rejected_crc;
    }
  }

  serviceTelemetry(now_ms);
  if (now_ms - last_publish_ms >= BOARD_M4_REMOTE_PUBLISH_MS) {
    publishSample(now_ms);
  }
}
