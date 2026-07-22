#include <array>
#include <cstdint>
#include <iostream>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                        \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

void write_v13_extension(std::array<uint8_t, csm::kBoardHealthV13PayloadLen>& payload) {
  payload[csm::kBoardHealthVersionOffset] = 13;
  payload[csm::kBoardHealthVersionOffset + 1] =
      static_cast<uint8_t>(csm::kBoardHealthV13PayloadLen);

  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbValidOffset], 1);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbStageOffset], 0x00001234u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbUptimeMsOffset], 23456u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbSequenceOffset], 77u);

  const uint32_t recovery_flags =
      csm::kBoardHealthRecoveryFlagReady |
      csm::kBoardHealthRecoveryFlagPreviousValid |
      csm::kBoardHealthRecoveryFlagWifiQuarantined |
      csm::kBoardHealthRecoveryFlagSourceChanged |
      csm::kBoardHealthRecoveryFlagRetryActive;
  csm::wr_u32_le(&payload[csm::kBoardHealthRecoveryFlagsOffset], recovery_flags);
  csm::wr_u32_le(&payload[csm::kBoardHealthFirmwareSourceId32Offset], 0x5A17C0DEu);
  csm::wr_u32_le(&payload[csm::kBoardHealthBootSequenceOffset], 101u);
  csm::wr_u32_le(&payload[csm::kBoardHealthConsecutiveEarlyResetsOffset], 2u);
  csm::wr_u32_le(&payload[csm::kBoardHealthEarlyResetTotalOffset], 7u);
  csm::wr_u32_le(&payload[csm::kBoardHealthWifiQuarantineTotalOffset], 3u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousBootSequenceOffset], 100u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousLastProgressIdOffset], 0x1101u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousLastProgressDetailOffset], 0x2202u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousLastProgressUptimeMsOffset], 19999u);
  csm::wr_u32_le(&payload[csm::kBoardHealthCurrentLastProgressIdOffset], 0x3303u);
  csm::wr_u32_le(&payload[csm::kBoardHealthCurrentLastProgressDetailOffset], 0x4404u);
  csm::wr_u32_le(&payload[csm::kBoardHealthCurrentLastProgressUptimeMsOffset], 2345u);
  csm::wr_u32_le(&payload[csm::kBoardHealthRetainedEventSequenceOffset], 0x10203040u);
  csm::wr_u32_le(&payload[csm::kBoardHealthResetExperimentProfileWordOffset],
                 0x7F010103u);
  csm::wr_u32_le(&payload[csm::kBoardHealthRetainedIntegrityWordOffset], 0x67450123u);
  csm::wr_u32_le(&payload[csm::kBoardHealthRuntimeContractId32Offset], 0xCAFEBABEu);
  csm::wr_u32_le(&payload[csm::kBoardHealthRecoveryIdentityId32Offset], 0x5A17C0DEu);
  csm::wr_u32_le(&payload[csm::kBoardHealthWatchdogObservedTimeoutMsOffset], 3000u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallFlagsOffset], 0x00020103u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallBootSequenceOffset], 100u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallSequenceOffset], 55u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallStartedMsOffset], 1234u);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallDurationUsOffset], 5678u);
  csm::wr_i32_le(&payload[csm::kBoardHealthPreviousWifiCallResultOffset], -7);
}

void verify_v13_golden_frame() {
  static_assert(csm::kBoardHealthV11PayloadLen == 408);
  static_assert(csm::kBoardHealthV12PayloadLen == 472);
  static_assert(csm::kBoardHealthV13PayloadLen == 508);
  static_assert(csm::kBoardHealthRecoveryFlagsOffset == 408);
  static_assert(csm::kBoardHealthFirmwareSourceId32Offset == 412);
  static_assert(csm::kBoardHealthBootSequenceOffset == 416);
  static_assert(csm::kBoardHealthConsecutiveEarlyResetsOffset == 420);
  static_assert(csm::kBoardHealthEarlyResetTotalOffset == 424);
  static_assert(csm::kBoardHealthWifiQuarantineTotalOffset == 428);
  static_assert(csm::kBoardHealthPreviousBootSequenceOffset == 432);
  static_assert(csm::kBoardHealthPreviousLastProgressIdOffset == 436);
  static_assert(csm::kBoardHealthPreviousLastProgressDetailOffset == 440);
  static_assert(csm::kBoardHealthPreviousLastProgressUptimeMsOffset == 444);
  static_assert(csm::kBoardHealthCurrentLastProgressIdOffset == 448);
  static_assert(csm::kBoardHealthCurrentLastProgressDetailOffset == 452);
  static_assert(csm::kBoardHealthCurrentLastProgressUptimeMsOffset == 456);
  static_assert(csm::kBoardHealthRetainedEventSequenceOffset == 460);
  static_assert(csm::kBoardHealthResetExperimentProfileWordOffset == 464);
  static_assert(csm::kBoardHealthRetainedIntegrityWordOffset == 468);
  static_assert(csm::kBoardHealthRuntimeContractId32Offset == 472);
  static_assert(csm::kBoardHealthRecoveryIdentityId32Offset == 476);
  static_assert(csm::kBoardHealthWatchdogObservedTimeoutMsOffset == 480);
  static_assert(csm::kBoardHealthPreviousWifiCallFlagsOffset == 484);
  static_assert(csm::kBoardHealthPreviousWifiCallBootSequenceOffset == 488);
  static_assert(csm::kBoardHealthPreviousWifiCallSequenceOffset == 492);
  static_assert(csm::kBoardHealthPreviousWifiCallStartedMsOffset == 496);
  static_assert(csm::kBoardHealthPreviousWifiCallDurationUsOffset == 500);
  static_assert(csm::kBoardHealthPreviousWifiCallResultOffset == 504);
  static_assert(csm::kBoardHealthResetExperimentFlagWatchdogEffective == 0x01u);
  static_assert(csm::kBoardHealthResetExperimentFlagWatchdogRequested == 0x08u);
  static_assert(csm::kBoardHealthResetExperimentFlagWatchdogStartCalled == 0x10u);
  static_assert(csm::kBoardHealthResetExperimentFlagWatchdogStartSucceeded == 0x20u);
  static_assert(csm::kBoardHealthResetExperimentFlagWatchdogTimeoutMatches == 0x40u);

  std::array<uint8_t, csm::kBoardHealthV13PayloadLen> payload{};
  write_v13_extension(payload);

  std::array<uint8_t, csm::encoded_typed_frame_len(csm::kBoardHealthV13PayloadLen)> frame{};
  size_t written = 0;
  CHECK(csm::encode_typed_frame(frame.data(), frame.size(), csm::RecordType::BoardHealth,
                                payload.data(), csm::kBoardHealthV13PayloadLen, 0xBEEFu,
                                0xA1u, &written));
  CHECK(written == 519u);
  CHECK(frame[0] == csm::kFrameSof0);
  CHECK(frame[1] == csm::kFrameSof1);
  CHECK(frame[2] == csm::kProtocolVersion);
  CHECK(frame[3] == static_cast<uint8_t>(csm::RecordType::BoardHealth));
  CHECK(frame[4] == 0xA1u);
  CHECK(csm::rd_u16_le(&frame[5]) == 0xBEEFu);
  CHECK(csm::rd_u16_le(&frame[7]) == csm::kBoardHealthV13PayloadLen);

  const uint8_t* decoded = &frame[9];
  CHECK(decoded[csm::kBoardHealthVersionOffset] == 13u);
  CHECK(decoded[csm::kBoardHealthVersionOffset + 1] == 252u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousBreadcrumbSequenceOffset]) == 77u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthRecoveryFlagsOffset]) == 0x00000293u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthFirmwareSourceId32Offset]) == 0x5A17C0DEu);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthBootSequenceOffset]) == 101u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthConsecutiveEarlyResetsOffset]) == 2u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthEarlyResetTotalOffset]) == 7u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthWifiQuarantineTotalOffset]) == 3u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousBootSequenceOffset]) == 100u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousLastProgressIdOffset]) == 0x1101u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousLastProgressDetailOffset]) == 0x2202u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousLastProgressUptimeMsOffset]) == 19999u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthCurrentLastProgressIdOffset]) == 0x3303u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthCurrentLastProgressDetailOffset]) == 0x4404u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthCurrentLastProgressUptimeMsOffset]) == 2345u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthRetainedEventSequenceOffset]) == 0x10203040u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthResetExperimentProfileWordOffset]) ==
        0x7F010103u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthRetainedIntegrityWordOffset]) == 0x67450123u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthRuntimeContractId32Offset]) == 0xCAFEBABEu);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthRecoveryIdentityId32Offset]) == 0x5A17C0DEu);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthWatchdogObservedTimeoutMsOffset]) == 3000u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousWifiCallFlagsOffset]) == 0x00020103u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousWifiCallBootSequenceOffset]) == 100u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousWifiCallSequenceOffset]) == 55u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousWifiCallStartedMsOffset]) == 1234u);
  CHECK(csm::rd_u32_le(&decoded[csm::kBoardHealthPreviousWifiCallDurationUsOffset]) == 5678u);
  CHECK(static_cast<int32_t>(csm::rd_u32_le(
            &decoded[csm::kBoardHealthPreviousWifiCallResultOffset])) == -7);

  const uint16_t encoded_crc = csm::rd_u16_le(&frame[written - 2]);
  CHECK(encoded_crc == 0x31F3u);
  CHECK(encoded_crc == csm::crc16_ccitt(&frame[2], written - 4));
}

void verify_v11_prefix_remains_encodable() {
  std::array<uint8_t, csm::kBoardHealthV11PayloadLen> payload{};
  payload[csm::kBoardHealthVersionOffset] = 11;
  payload[csm::kBoardHealthVersionOffset + 1] =
      static_cast<uint8_t>(csm::kBoardHealthV11PayloadLen);
  csm::wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbSequenceOffset], 0xAABBCCDDu);

  std::array<uint8_t, csm::encoded_typed_frame_len(csm::kBoardHealthV11PayloadLen)> frame{};
  size_t written = 0;
  CHECK(csm::encode_typed_frame(frame.data(), frame.size(), csm::RecordType::BoardHealth,
                                payload.data(), csm::kBoardHealthV11PayloadLen, 9u, 0u,
                                &written));
  CHECK(written == csm::encoded_typed_frame_len(csm::kBoardHealthV11PayloadLen));
  CHECK(csm::rd_u16_le(&frame[7]) == csm::kBoardHealthV11PayloadLen);
  CHECK(csm::rd_u32_le(&frame[9 + csm::kBoardHealthPreviousBreadcrumbSequenceOffset]) ==
        0xAABBCCDDu);
}

void verify_v12_prefix_remains_encodable() {
  std::array<uint8_t, csm::kBoardHealthV12PayloadLen> payload{};
  payload[csm::kBoardHealthVersionOffset] = 12;
  payload[csm::kBoardHealthVersionOffset + 1] =
      static_cast<uint8_t>(csm::kBoardHealthV12PayloadLen);
  csm::wr_u32_le(&payload[csm::kBoardHealthFirmwareSourceId32Offset], 0xAABBCCDDu);
  csm::wr_u32_le(&payload[csm::kBoardHealthRetainedIntegrityWordOffset], 0x00000012u);

  std::array<uint8_t, csm::encoded_typed_frame_len(csm::kBoardHealthV12PayloadLen)> frame{};
  size_t written = 0;
  CHECK(csm::encode_typed_frame(frame.data(), frame.size(), csm::RecordType::BoardHealth,
                                payload.data(), csm::kBoardHealthV12PayloadLen, 10u, 0u,
                                &written));
  CHECK(written == csm::encoded_typed_frame_len(csm::kBoardHealthV12PayloadLen));
  CHECK(csm::rd_u16_le(&frame[7]) == csm::kBoardHealthV12PayloadLen);
  CHECK(csm::rd_u32_le(&frame[9 + csm::kBoardHealthFirmwareSourceId32Offset]) ==
        0xAABBCCDDu);
}

}  // namespace

int main() {
  verify_v13_golden_frame();
  verify_v11_prefix_remains_encodable();
  verify_v12_prefix_remains_encodable();
  if (failures != 0) {
    std::cerr << failures << " BOARD_HEALTH v13 contract check(s) failed\n";
    return 1;
  }
  std::cout << "BOARD_HEALTH v13 contract tests passed\n";
  return 0;
}
