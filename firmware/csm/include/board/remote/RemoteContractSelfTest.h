#pragma once

#include <stdint.h>

namespace csm::board::remote {

enum class RemoteContractSelfTestDetail : uint16_t {
  None = 0,
  ParserDidNotComplete = 1,
  DecodeFailed = 2,
  NormalizeRejected = 3,
  WriterRejected = 4,
  ReaderRejected = 5,
  SnapshotMismatch = 6,
  CorruptCrcNotRejected = 7,
  TornWriteNotRejected = 8,
  StaleFrameNotRejected = 9,
  FailsafeNotRejected = 10,
};

struct RemoteContractSelfTestResult {
  bool passed = false;
  RemoteContractSelfTestDetail detail = RemoteContractSelfTestDetail::ParserDidNotComplete;
  uint16_t parser_malformed_total = 0;
  uint32_t published_sequence = 0;
};

RemoteContractSelfTestResult runRemoteContractSelfTest();

}  // namespace csm::board::remote
