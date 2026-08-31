#include <Arduino.h>

#include <string.h>

#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"
#include "board/remote/ReceiverAdmission.h"
#include "board/remote/R16smReceiverProfile.h"
#include "board/remote/RemoteSharedMemory.h"
#include "board/control_island/ControlIslandSharedMemory.h"
#include "board/control_island/M4Fdcan1Owner.h"
#include "board/control_island/M4StaticCyclicExecutor.h"

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

#ifndef BOARD_M4_M7_PUBLISH_TIMEOUT_US
#define BOARD_M4_M7_PUBLISH_TIMEOUT_US 0UL
#endif

#ifndef CSM_FW_SOURCE_ID64
#define CSM_FW_SOURCE_ID64 0ULL
#endif
#ifndef CSM_FW_RUNTIME_CONTRACT_ID64
#define CSM_FW_RUNTIME_CONTRACT_ID64 0ULL
#endif
#ifndef CSM_FW_BUILD_ID
#define CSM_FW_BUILD_ID 0
#endif

static_assert(BOARD_M4_REMOTE_BAUD ==
                  csm::board::remote::kR16smConfiguredBaud,
              "M4 UART baud drifted from the R16SM product profile");

namespace {

using namespace csm::board::remote;
using namespace csm::board::control_island;

CrsfParser parser;
RcNormalizer normalizer;
ReceiverAdmission receiver_admission;
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
bool frontend_configured = false;
RemoteTelemetrySlot telemetry;
uint32_t malformed_total_at_last_rc = 0;
M4StaticCyclicExecutor control_executor;
M4Fdcan1Owner fdcan1_owner;
M4ControlTimebase control_timebase;
uint32_t control_m4_boot_id = 0;
uint32_t last_control_sequence = 0;
uint32_t last_control_health_ms = 0;
bool fdcan1_owner_initialized = false;
bool control_timebase_initialized = false;
BringupTracePayload bringup_trace;
uint32_t health_snapshot_reject_total = 0;
bool foreground_loop_entered = false;
bool first_tick_reported = false;

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
  const bool link_statistics_fresh = has_link_statistics &&
      diagnostics.last_link_statistics_age_ms <=
          BOARD_M4_REMOTE_LINK_STATISTICS_STALE_MS;
  diagnostics.link_statistics_valid = link_statistics_fresh ? 1u : 0u;
  current_sample.link_quality = has_link_statistics
      ? link_statistics.uplink_link_quality : kRemoteMetricUnknown;
  current_sample.rssi_hint = has_link_statistics
      ? (link_statistics.uplink_rssi_ant1_dbm_magnitude <
                 link_statistics.uplink_rssi_ant2_dbm_magnitude
             ? link_statistics.uplink_rssi_ant1_dbm_magnitude
             : link_statistics.uplink_rssi_ant2_dbm_magnitude)
      : kRemoteMetricUnknown;

  const bool receiver_usable = receiver_admission.usable(
      now_ms, has_link_statistics, current_sample.link_quality,
      diagnostics.last_link_statistics_age_ms);
  const ReceiverAdmissionState& admission = receiver_admission.state();
  diagnostics.admission_resets = admission.reset_count;
  diagnostics.admission_streak = admission.consecutive_frames;
  diagnostics.receiver_qualified = admission.receiver_qualified ? 1u : 0u;
  diagnostics.channel_valid_mask = current_sample.channel_valid_mask;

  if (!frontend_configured) {
    current_sample.sample_state = RcSampleState::ProtocolFault;
  } else if (has_link_statistics && link_statistics.uplink_link_quality == 0u) {
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
  } else if (has_link_statistics && !link_statistics_fresh) {
    current_sample.sample_state = RcSampleState::ProtocolFault;
  } else if (!receiver_usable) {
    current_sample.sample_state = RcSampleState::Lost;
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
  if (decodeCrsfRcChannels(frame, &channels) != CrsfDecodeStatus::Ok) {
    receiver_admission.reset(
        ReceiverAdmissionRejectDetail::MissingControlChannels);
    return;
  }
  ++diagnostics.rc_frames;
  if (frame.type == kCrsfFrameTypeSubsetRcChannelsPacked) {
    ++diagnostics.subset_rc_frames;
  }
  diagnostics.raw_ch2 = (channels.valid_mask & (1u << 1)) != 0u
      ? channels.raw[1] : 0u;
  diagnostics.raw_ch4 = (channels.valid_mask & (1u << 3)) != 0u
      ? channels.raw[3] : 0u;
  for (uint8_t index = 0; index < kRcChannelCount; ++index) {
    diagnostics.raw_channels[index] =
        (channels.valid_mask & (1u << index)) != 0u
            ? channels.raw[index] : 0u;
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
    receiver_admission.reset(
        ReceiverAdmissionRejectDetail::MissingControlChannels);
    return;
  }
  ++diagnostics.accepted_rc_frames;
  diagnostics.last_normalize_reject_detail = 0;
  current_sample = normalized.sample;
  (void)receiver_admission.observeRcFrame(
      now_ms, frame.address, current_sample.channel_valid_mask);
  malformed_total_at_last_rc = frontendMalformedTotal();
  has_rc_sample = true;
  last_rc_ms = now_ms;
}

void handleFrame(uint32_t now_ms, const CrsfFrame& frame) {
  ++diagnostics.valid_frames;
  diagnostics.last_address = frame.address;
  diagnostics.last_type = frame.type;
  if (frame.type == kCrsfFrameTypeRcChannelsPacked ||
      frame.type == kCrsfFrameTypeSubsetRcChannelsPacked) {
    handleRcFrame(now_ms, frame);
  } else if (frame.type == kCrsfFrameTypeLinkStatistics ||
             frame.type == kCrsfFrameTypeLinkStatisticsRx ||
             frame.type == kCrsfFrameTypeLinkStatisticsTx) {
    CrsfLinkStatistics decoded;
    if (decodeCrsfLinkStatistics(frame, &decoded) == CrsfDecodeStatus::Ok) {
      link_statistics = decoded;
      has_link_statistics = true;
      last_link_statistics_ms = now_ms;
      diagnostics.link_statistics_type = static_cast<uint8_t>(decoded.kind);
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

void serviceControlIngress() {
  const ControlReadResult read =
      readFinalControlSnapshot(last_control_sequence);
  if (!read.accepted) {
    if (read.detail != 0u) {
      control_executor.stageIpcIntegrityFailure();
    }
    return;
  }
  if (!read.new_snapshot) return;
  // The shared CRC/copy work remains in foreground. Mask only TIM4 for the
  // bounded local-store swap so the ISR never observes a partially copied
  // coherent image; FDCAN/CRSF interrupts remain independent.
  NVIC_DisableIRQ(TIM4_IRQn);
  const bool accepted = control_executor.stageSnapshot(read.payload, micros());
  NVIC_EnableIRQ(TIM4_IRQn);
  if (accepted) last_control_sequence = read.sequence;
}

void publishControlIslandHealth(uint32_t now_ms) {
  if (now_ms - last_control_health_ms < 20u) return;
  last_control_health_ms = now_ms;
  ControlHealthPayload health;
  if (!control_executor.healthSnapshot(micros(), &health)) {
    if (health_snapshot_reject_total != UINT32_MAX) {
      ++health_snapshot_reject_total;
    }
    return;
  }
  health.health_snapshot_reject_total = health_snapshot_reject_total;
  fdcan1_owner.augmentHealth(&health);
  ControlIpcRegion* region = controlIpcRegion();
  health.raw_ring_fill = rawCanRingFill();
  health.raw_ring_high_water = region->raw_high_water;
  const uint32_t hardware_drop = fdcan1_owner.rawCaptureDropCount();
  health.raw_ring_drop = UINT32_MAX - region->raw_drop_count < hardware_drop
      ? UINT32_MAX
      : region->raw_drop_count + hardware_drop;
  (void)publishControlHealth(health);
}

void recordBringup(BringupStage stage,
                   BringupFailure failure = BringupFailure::None,
                   uint32_t detail = 0u) {
  if (advanceBringupTrace(&bringup_trace, stage, failure, detail)) {
    (void)publishBringupTrace(bringup_trace);
  }
}

}  // namespace

void setup() {
  bringup_trace.source_id = static_cast<uint64_t>(CSM_FW_SOURCE_ID64);
  bringup_trace.runtime_contract_id =
      static_cast<uint64_t>(CSM_FW_RUNTIME_CONTRACT_ID64);
  bringup_trace.build_id = static_cast<uint32_t>(CSM_FW_BUILD_ID);
  recordBringup(BringupStage::M4Entered);
  RcNormalizerConfig config;
  config.configured = true;
  config.required_channel_mask = kR16smRequiredControlChannelMask;
  const bool normalizer_configured = normalizer.configure(config);
  ReceiverAdmissionConfig admission_config;
  admission_config.configured = true;
  admission_config.receiver_address = kR16smCrsfAddress;
  admission_config.consecutive_frames_required =
      kR16smAdmissionConsecutiveFrames;
  admission_config.required_channel_mask = kR16smRequiredControlChannelMask;
  admission_config.rc_freshness_ms = BOARD_M4_REMOTE_STALE_MS;
  admission_config.link_statistics_freshness_ms =
      BOARD_M4_REMOTE_LINK_STATISTICS_STALE_MS;
  const bool admission_configured =
      receiver_admission.configure(admission_config);
  frontend_configured = normalizer_configured && admission_configured;
  parser.reset();
  mailbox_writer.reset();
  mailbox_writer.clearFrame(&mailbox_frame);
  current_sample = {};
  diagnostics = {};
  diagnostics.uart_baud = BOARD_M4_REMOTE_BAUD;
  if (!frontend_configured) {
    current_sample.sample_state = RcSampleState::ProtocolFault;
  }
  control_m4_boot_id = initializeControlIpcForM4();
  bringup_trace.m4_boot_id = control_m4_boot_id;
  recordBringup(BringupStage::ControlIpcValidated,
                control_m4_boot_id == 0u ? BringupFailure::ControlIpc
                                         : BringupFailure::None);
  m4_boot_id = initializeRemoteSharedMemoryForM4();
  recordBringup(BringupStage::RemoteIpcValidated,
                m4_boot_id == 0u ? BringupFailure::RemoteIpc
                                  : BringupFailure::None);
  control_executor.begin(control_m4_boot_id,
                         BOARD_M4_M7_PUBLISH_TIMEOUT_US, &fdcan1_owner);
  recordBringup(BringupStage::ExecutorInitialized);
  control_timebase_initialized = control_timebase.begin(&control_executor);
  recordBringup(BringupStage::Tim4Configured,
                control_timebase_initialized ? BringupFailure::None
                                             : BringupFailure::Tim4);
  fdcan1_owner_initialized =
      fdcan1_owner.begin(control_m4_boot_id, &control_executor, &bringup_trace);
  Serial3.begin(BOARD_M4_REMOTE_BAUD, SERIAL_8N1);
  publishSample(millis());
  publishControlIslandHealth(millis());
}

void loop() {
  if (!foreground_loop_entered) {
    foreground_loop_entered = true;
    recordBringup(BringupStage::ForegroundLoopEntered);
  }
  if (!first_tick_reported && control_timebase.hasTicked()) {
    first_tick_reported = true;
    recordBringup(BringupStage::FirstTim4Tick);
  }
  const uint32_t now_ms = millis();
  while (Serial3.available() > 0) {
    const int value = Serial3.read();
    if (value < 0) break;
    const uint32_t now_us = micros();
    if (parser.bufferedBytes() != 0 &&
        now_us - last_byte_us > kCrsfInterByteTimeoutUs) {
      parser.reset();
      ++diagnostics.inter_byte_resets;
      receiver_admission.reset(ReceiverAdmissionRejectDetail::SequenceBroken);
    }
    last_byte_us = now_us;
    ++diagnostics.rx_bytes;
    const auto result = parser.ingest(static_cast<uint8_t>(value));
    diagnostics.rejected_address = parser.rejectedAddressTotal();
    diagnostics.rejected_length = parser.rejectedLengthTotal();
    diagnostics.rejected_crc = parser.rejectedCrcTotal();
    if (result.status == CrsfParseStatus::FrameReady) {
      handleFrame(now_ms, result.frame);
    } else if (result.status == CrsfParseStatus::RejectedAddress ||
               result.status == CrsfParseStatus::RejectedLength ||
               result.status == CrsfParseStatus::RejectedCrc) {
      receiver_admission.reset(ReceiverAdmissionRejectDetail::SequenceBroken);
    }
  }

  serviceTelemetry(now_ms);
  serviceControlIngress();
  publishControlIslandHealth(now_ms);
  if (now_ms - last_publish_ms >= BOARD_M4_REMOTE_PUBLISH_MS) {
    publishSample(now_ms);
  }
}
