#include <Arduino.h>

#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxContract.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"

#if !defined(BOARD_M4_REMOTE_SERIAL3_CAPTURE_PROBE) || \
    BOARD_M4_REMOTE_SERIAL3_CAPTURE_PROBE != 1
#error "m4_remote_serial3_capture_probe.cpp requires BOARD_M4_REMOTE_SERIAL3_CAPTURE_PROBE=1"
#endif

#if defined(BOARD_ENABLE_REMOTE_CONTROL) && BOARD_ENABLE_REMOTE_CONTROL != 0
#error "Serial3 capture probe must not enable product remote control"
#endif

#if defined(BOARD_ENABLE_M4_REMOTE_FRONTEND) && BOARD_ENABLE_M4_REMOTE_FRONTEND != 0
#error "Serial3 capture probe must not enable product M4 remote frontend"
#endif

#if defined(BOARD_ENABLE_REMOTE_AUTHORITY) && BOARD_ENABLE_REMOTE_AUTHORITY != 0
#error "Serial3 capture probe must not enable remote authority"
#endif

#if defined(BOARD_ENABLE_HOST_CAN_TX) && BOARD_ENABLE_HOST_CAN_TX != 0
#error "Serial3 capture probe must not enable host CAN TX"
#endif

#if defined(BOARD_ENABLE_HOST_DOWNLINK) && BOARD_ENABLE_HOST_DOWNLINK != 0
#error "Serial3 capture probe must not enable host downlink"
#endif

#ifndef BOARD_M4_REMOTE_CAPTURE_BAUD
#define BOARD_M4_REMOTE_CAPTURE_BAUD 420000UL
#endif

#if BOARD_M4_REMOTE_CAPTURE_BAUD < 9600
#error "BOARD_M4_REMOTE_CAPTURE_BAUD is below a valid UART test range"
#endif

namespace {

using csm::board::remote::CrsfDecodeStatus;
using csm::board::remote::CrsfParser;
using csm::board::remote::CrsfParseStatus;
using csm::board::remote::CrsfRcChannels;
using csm::board::remote::M4RemoteMailboxFrame;
using csm::board::remote::M4RemoteMailboxWriter;
using csm::board::remote::RcNormalizer;
using csm::board::remote::RcNormalizerConfig;
using csm::board::remote::decodeCrsfRcChannelsPacked;
using csm::board::remote::kRemoteMetricUnknown;

struct CaptureCounters {
  uint32_t bytes = 0;
  uint32_t frames = 0;
  uint32_t rc_frames = 0;
  uint32_t normalized_samples = 0;
  uint32_t mailbox_publishes = 0;
  uint32_t rejected_length = 0;
  uint32_t rejected_crc = 0;
  uint32_t decode_wrong_type = 0;
  uint32_t decode_bad_length = 0;
  uint32_t normalize_rejects = 0;
  uint32_t mailbox_rejects = 0;
};

CrsfParser g_parser;
RcNormalizer g_normalizer;
M4RemoteMailboxWriter g_mailbox_writer;
M4RemoteMailboxFrame g_last_mailbox_frame;
CaptureCounters g_counters;

volatile uint32_t g_remote_probe_bytes = 0;
volatile uint32_t g_remote_probe_frames = 0;
volatile uint32_t g_remote_probe_rc_frames = 0;
volatile uint32_t g_remote_probe_mailbox_publishes = 0;
volatile uint32_t g_remote_probe_rejected_length = 0;
volatile uint32_t g_remote_probe_rejected_crc = 0;
volatile uint32_t g_remote_probe_last_byte_ms = 0;
volatile uint32_t g_remote_probe_last_frame_ms = 0;
volatile uint32_t g_remote_probe_last_mailbox_sequence = 0;
volatile int16_t g_remote_probe_last_ch0 = 0;
volatile int16_t g_remote_probe_last_ch1 = 0;
volatile int16_t g_remote_probe_last_ch2 = 0;
volatile int16_t g_remote_probe_last_ch3 = 0;

void publishVolatileSnapshot(uint32_t now_ms) {
  g_remote_probe_bytes = g_counters.bytes;
  g_remote_probe_frames = g_counters.frames;
  g_remote_probe_rc_frames = g_counters.rc_frames;
  g_remote_probe_mailbox_publishes = g_counters.mailbox_publishes;
  g_remote_probe_rejected_length = g_counters.rejected_length;
  g_remote_probe_rejected_crc = g_counters.rejected_crc;
  if (g_counters.bytes > 0) {
    g_remote_probe_last_byte_ms = now_ms;
  }
}

void handleParseReject(CrsfParseStatus status) {
  if (status == CrsfParseStatus::RejectedLength) {
    ++g_counters.rejected_length;
  } else if (status == CrsfParseStatus::RejectedCrc) {
    ++g_counters.rejected_crc;
  }
}

void handleFrame(uint32_t now_ms,
                 const csm::board::remote::CrsfFrame& frame) {
  ++g_counters.frames;
  g_remote_probe_last_frame_ms = now_ms;

  CrsfRcChannels channels;
  const CrsfDecodeStatus decode_status =
      decodeCrsfRcChannelsPacked(frame, &channels);
  if (decode_status == CrsfDecodeStatus::WrongType) {
    ++g_counters.decode_wrong_type;
    return;
  }
  if (decode_status == CrsfDecodeStatus::BadLength) {
    ++g_counters.decode_bad_length;
    return;
  }
  if (decode_status != CrsfDecodeStatus::Ok) {
    ++g_counters.decode_bad_length;
    return;
  }

  ++g_counters.rc_frames;
  auto normalized = g_normalizer.normalizeCrsfChannels(
      now_ms,
      static_cast<uint16_t>(g_counters.rc_frames & 0xFFFFu),
      channels,
      kRemoteMetricUnknown,
      kRemoteMetricUnknown,
      0u);
  if (!normalized.accepted) {
    ++g_counters.normalize_rejects;
    return;
  }

  ++g_counters.normalized_samples;
  normalized.sample.malformed_count = g_parser.malformedTotal();
  const auto write_result =
      g_mailbox_writer.publishSample(normalized.sample, &g_last_mailbox_frame);
  if (!write_result.accepted) {
    ++g_counters.mailbox_rejects;
    return;
  }

  ++g_counters.mailbox_publishes;
  g_remote_probe_last_mailbox_sequence = write_result.published_sequence;
  g_remote_probe_last_ch0 = normalized.sample.ch[0];
  g_remote_probe_last_ch1 = normalized.sample.ch[1];
  g_remote_probe_last_ch2 = normalized.sample.ch[2];
  g_remote_probe_last_ch3 = normalized.sample.ch[3];
}

}  // namespace

void setup() {
  RcNormalizerConfig config;
  config.configured = true;
  config.required_channel_mask =
      csm::board::remote::kRemoteRequiredRcChannelMask;
  g_normalizer.configure(config);
  g_parser.reset();
  g_mailbox_writer.reset();
  g_mailbox_writer.clearFrame(&g_last_mailbox_frame);
  Serial3.begin(BOARD_M4_REMOTE_CAPTURE_BAUD, SERIAL_8N1);
}

void loop() {
  const uint32_t now_ms = millis();
  while (Serial3.available() > 0) {
    const int value = Serial3.read();
    if (value < 0) {
      break;
    }

    ++g_counters.bytes;
    const auto result = g_parser.ingest(static_cast<uint8_t>(value));
    if (result.status == CrsfParseStatus::FrameReady) {
      handleFrame(now_ms, result.frame);
    } else if (result.status == CrsfParseStatus::RejectedLength ||
               result.status == CrsfParseStatus::RejectedCrc) {
      handleParseReject(result.status);
    }
  }
  publishVolatileSnapshot(now_ms);
}
