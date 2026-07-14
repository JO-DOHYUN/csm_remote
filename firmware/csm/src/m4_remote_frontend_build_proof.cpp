#include <Arduino.h>

#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxContract.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"

namespace {

volatile bool g_m4_remote_build_proof_ok = false;
volatile uint32_t g_m4_remote_build_proof_sequence = 0;

bool runM4RemoteFrontendBuildProof() {
  using namespace csm::board::remote;

  CrsfParser parser;
  parser.reset();

  RcNormalizer normalizer;
  RcNormalizerConfig config;
  config.configured = true;
  if (!normalizer.configure(config)) {
    return false;
  }

  CrsfRcChannels channels;
  channels.count = kRcChannelCount;
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    channels.raw[i] = kCrsfRawDefaultMid;
  }

  const RcNormalizeResult normalized =
      normalizer.normalizeCrsfChannels(1u, 1u, channels, kRemoteMetricUnknown,
                                       kRemoteMetricUnknown, 0u);
  if (!normalized.accepted) {
    return false;
  }

  M4RemoteMailboxFrame frame;
  M4RemoteMailboxWriter writer;
  const M4RemoteMailboxWriteResult written =
      writer.publishSample(normalized.sample, &frame);
  if (!written.accepted) {
    return false;
  }

  g_m4_remote_build_proof_sequence = written.published_sequence;
  return frame.magic == kM4RemoteMailboxMagic &&
         frame.version == kM4RemoteMailboxVersion &&
         frame.frame_size == kM4RemoteMailboxFrameBytes &&
         frame.crc == computeM4RemoteMailboxCrc(frame);
}

}  // namespace

void setup() {
  g_m4_remote_build_proof_ok = runM4RemoteFrontendBuildProof();
}

void loop() {}
