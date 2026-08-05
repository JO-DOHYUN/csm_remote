#include "board/remote/RemoteContractSelfTest.h"

#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxReader.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"

namespace csm::board::remote {
namespace {

constexpr uint8_t kSelfTestCrsfAddress = 0xC8;
constexpr uint16_t kSelfTestRcSeq = 77;
constexpr uint32_t kSelfTestM4TimeMs = 1234;
constexpr uint32_t kSelfTestM7TimeMs = 1300;
constexpr uint8_t kSelfTestLinkQuality = 91;
constexpr uint8_t kSelfTestRssiHint = 82;
constexpr uint16_t kSelfTestFlags = 0x0055;

RemoteContractSelfTestResult fail(RemoteContractSelfTestDetail detail,
                                  uint16_t malformed_total = 0,
                                  uint32_t published_sequence = 0) {
  RemoteContractSelfTestResult result;
  result.detail = detail;
  result.parser_malformed_total = malformed_total;
  result.published_sequence = published_sequence;
  return result;
}

void packCrsfRawChannels(const uint16_t* raw, uint8_t* payload) {
  for (uint8_t i = 0; i < kCrsfRcChannelsPackedPayloadBytes; ++i) {
    payload[i] = 0;
  }

  for (uint8_t channel = 0; channel < kRcChannelCount; ++channel) {
    const uint16_t value = raw[channel] & kCrsfRawChannelMax;
    for (uint8_t bit = 0; bit < 11; ++bit) {
      if ((value & (1u << bit)) == 0) {
        continue;
      }
      const uint16_t target_bit = static_cast<uint16_t>(channel) * 11u + bit;
      payload[target_bit / 8u] |= static_cast<uint8_t>(1u << (target_bit % 8u));
    }
  }
}

uint8_t buildSelfTestCrsfFrame(uint8_t* frame, uint8_t frame_capacity) {
  const uint8_t length =
      static_cast<uint8_t>(kCrsfRcChannelsPackedPayloadBytes + 2u);
  const uint8_t total = static_cast<uint8_t>(length + 2u);
  if (frame == nullptr || frame_capacity < total) {
    return 0;
  }

  uint16_t raw[kRcChannelCount] = {};
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    raw[i] = kCrsfRawDefaultMid;
  }
  raw[0] = kCrsfRawDefaultMin;
  raw[1] = kCrsfRawDefaultMid;
  raw[2] = kCrsfRawDefaultMax;

  frame[0] = kSelfTestCrsfAddress;
  frame[1] = length;
  frame[2] = kCrsfFrameTypeRcChannelsPacked;
  packCrsfRawChannels(raw, &frame[3]);
  frame[total - 1u] = computeCrsfFrameCrc(&frame[2], length - 1u);
  return total;
}

bool matchesExpectedSnapshot(const M4RemoteMailboxSnapshot& snapshot,
                             uint32_t published_sequence) {
  return snapshot.sample_present &&
         snapshot.integrity_ok &&
         snapshot.link_state == RemoteLinkState::Valid &&
         snapshot.sample.sample_state == RcSampleState::Ok &&
         snapshot.sample.seq == kSelfTestRcSeq &&
         snapshot.sample.m4_time_ms == kSelfTestM4TimeMs &&
         snapshot.sample.ch[0] == kRcNormalizedChannelMin &&
         snapshot.sample.ch[1] == 0 &&
         snapshot.sample.ch[2] == kRcNormalizedChannelMax &&
         snapshot.sample.link_quality == kSelfTestLinkQuality &&
         snapshot.sample.rssi_hint == kSelfTestRssiHint &&
         snapshot.sample.flags == kSelfTestFlags &&
         published_sequence != 0;
}

bool corruptCrcIsRejected(const uint8_t* good_frame, uint8_t total) {
  uint8_t corrupt[kCrsfMaxFrameBytes] = {};
  for (uint8_t i = 0; i < total; ++i) {
    corrupt[i] = good_frame[i];
  }
  corrupt[total - 1u] ^= 0xFFu;

  CrsfParser parser;
  CrsfParseResult result;
  for (uint8_t i = 0; i < total; ++i) {
    result = parser.ingest(corrupt[i]);
  }
  return result.status == CrsfParseStatus::RejectedCrc;
}

bool tornMailboxWriteIsRejected(const M4RemoteMailboxFrame& good_frame) {
  M4RemoteMailboxFrame torn = good_frame;
  torn.sequence_begin |= 1u;

  const M4RemoteMailboxDecodeResult decoded =
      decodeM4RemoteMailboxFrame(torn);
  return !decoded.integrity_ok &&
         !decoded.sample_present &&
         decoded.link_state == RemoteLinkState::Malformed &&
         decoded.reject_detail ==
             static_cast<uint16_t>(M4RemoteMailboxRejectDetail::TornWrite);
}

bool staleMailboxSampleIsRejected(const M4RemoteMailboxFrame& good_frame) {
  M4RemoteMailboxReader reader;
  reader.begin(kSelfTestM7TimeMs);
  if (!reader.updateFromMailboxFrame(kSelfTestM7TimeMs, good_frame)) {
    return false;
  }

  reader.update(kSelfTestM7TimeMs + kDefaultRcSampleStaleMs + 1u,
                kDefaultRcSampleStaleMs);
  return !reader.hasFreshUsableSample() &&
         reader.snapshot().link_state == RemoteLinkState::Stale &&
         reader.snapshot().reject_detail ==
             static_cast<uint16_t>(M4RemoteMailboxRejectDetail::Stale);
}

bool failsafeMailboxSampleIsRejected(const M4RemoteMailboxFrame& good_frame) {
  M4RemoteMailboxFrame failsafe = good_frame;
  failsafe.sample_state = static_cast<uint8_t>(RcSampleState::Failsafe);
  failsafe.crc = computeM4RemoteMailboxCrc(failsafe);

  M4RemoteMailboxReader reader;
  reader.begin(kSelfTestM7TimeMs);
  if (reader.updateFromMailboxFrame(kSelfTestM7TimeMs, failsafe)) {
    return false;
  }

  return !reader.hasFreshUsableSample() &&
         reader.snapshot().link_state == RemoteLinkState::Failsafe &&
         reader.snapshot().reject_detail ==
             static_cast<uint16_t>(M4RemoteMailboxRejectDetail::SampleNotUsable);
}

}  // namespace

RemoteContractSelfTestResult runRemoteContractSelfTest() {
  uint8_t frame_bytes[kCrsfMaxFrameBytes] = {};
  const uint8_t total = buildSelfTestCrsfFrame(frame_bytes, sizeof(frame_bytes));

  CrsfParser parser;
  CrsfParseResult parse_result;
  for (uint8_t i = 0; i < total; ++i) {
    parse_result = parser.ingest(frame_bytes[i]);
  }
  if (parse_result.status != CrsfParseStatus::FrameReady) {
    return fail(RemoteContractSelfTestDetail::ParserDidNotComplete,
                parse_result.malformed_total);
  }

  CrsfRcChannels channels;
  if (decodeCrsfRcChannelsPacked(parse_result.frame, &channels) !=
      CrsfDecodeStatus::Ok) {
    return fail(RemoteContractSelfTestDetail::DecodeFailed,
                parse_result.malformed_total);
  }

  RcNormalizer normalizer;
  RcNormalizerConfig config;
  config.configured = true;
  config.required_channel_mask = kRemoteRequiredRcChannelMask;
  if (!normalizer.configure(config)) {
    return fail(RemoteContractSelfTestDetail::NormalizeRejected,
                parse_result.malformed_total);
  }

  const RcNormalizeResult normalized =
      normalizer.normalizeCrsfChannels(kSelfTestM4TimeMs,
                                       kSelfTestRcSeq,
                                       channels,
                                       kSelfTestLinkQuality,
                                       kSelfTestRssiHint,
                                       kSelfTestFlags);
  if (!normalized.accepted) {
    return fail(RemoteContractSelfTestDetail::NormalizeRejected,
                parse_result.malformed_total);
  }

  M4RemoteMailboxFrame mailbox_frame;
  M4RemoteMailboxWriter writer;
  const M4RemoteMailboxWriteResult write_result =
      writer.publishSample(normalized.sample, &mailbox_frame);
  if (!write_result.accepted) {
    return fail(RemoteContractSelfTestDetail::WriterRejected,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  M4RemoteMailboxReader reader;
  reader.begin(kSelfTestM7TimeMs);
  if (!reader.updateFromMailboxFrame(kSelfTestM7TimeMs, mailbox_frame)) {
    return fail(RemoteContractSelfTestDetail::ReaderRejected,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  if (!matchesExpectedSnapshot(reader.snapshot(), write_result.published_sequence)) {
    return fail(RemoteContractSelfTestDetail::SnapshotMismatch,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  if (!corruptCrcIsRejected(frame_bytes, total)) {
    return fail(RemoteContractSelfTestDetail::CorruptCrcNotRejected,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  if (!tornMailboxWriteIsRejected(mailbox_frame)) {
    return fail(RemoteContractSelfTestDetail::TornWriteNotRejected,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  if (!staleMailboxSampleIsRejected(mailbox_frame)) {
    return fail(RemoteContractSelfTestDetail::StaleFrameNotRejected,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  if (!failsafeMailboxSampleIsRejected(mailbox_frame)) {
    return fail(RemoteContractSelfTestDetail::FailsafeNotRejected,
                parse_result.malformed_total,
                write_result.published_sequence);
  }

  RemoteContractSelfTestResult result;
  result.passed = true;
  result.detail = RemoteContractSelfTestDetail::None;
  result.parser_malformed_total = parse_result.malformed_total;
  result.published_sequence = write_result.published_sequence;
  return result;
}

}  // namespace csm::board::remote
