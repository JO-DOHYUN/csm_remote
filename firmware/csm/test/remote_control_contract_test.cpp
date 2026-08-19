#include <cstdio>
#include <cstring>

#include "board/authority/AuthorityManager.h"
#include "board/HostDownlinkParser.h"
#include "board/SafetySupervisor.h"
#include "board/can/BuiltinCanTxOwner.h"
#include "board/control/CanTxGateway.h"
#include "board/control/CommandLimiter.h"
#include "board/control/ControlReleaseSchedule.h"
#include "board/control/HostCommandFreshness.h"
#include "board/control/HostControlAuthorityGate.h"
#include "board/control/RemoteControlOrchestrator.h"
#include "board/control/RemoteControlRuntime.h"
#include "board/control/VehicleCommandMapper.h"
#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxReader.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"
#include "board/remote/RemoteSharedMemory.h"
#include "protocol/ControlProtocol.h"
#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

struct FakeBuiltinCanDriver {
  int32_t next_result = 1;
  uint32_t next_request_mask = 1;
  bool override_driver_sequence = false;
  uint32_t next_driver_sequence = 0;
  uint32_t next_write_duration_us = 7;
  int32_t next_cancel_result = 0;
  bool next_cancel_accepted = true;
  uint32_t calls = 0;
  uint32_t cancel_calls = 0;
  uint32_t last_cancel_mask = 0;
  csm::board::can::BuiltinCanTxFrame last_frame = {};
  csm::board::can::BuiltinCanTxFrame frames[16] = {};
};

struct BuiltinCanCompletionCapture {
  csm::board::can::BuiltinCanTxCompletion items[16] = {};
  uint8_t count = 0;
};

csm::board::can::BuiltinCanTxDriverResult fakeBuiltinCanWrite(
    void* context, const csm::board::can::BuiltinCanTxFrame& frame,
    uint32_t submission_sequence) {
  FakeBuiltinCanDriver* driver =
      static_cast<FakeBuiltinCanDriver*>(context);
  csm::board::can::BuiltinCanTxDriverResult result;
  if (driver == nullptr) return result;
  ++driver->calls;
  driver->last_frame = frame;
  if (driver->calls <= 16) driver->frames[driver->calls - 1] = frame;
  result.driver_result = driver->next_result;
  result.request_mask = driver->next_request_mask;
  result.driver_sequence = driver->override_driver_sequence
      ? driver->next_driver_sequence
      : submission_sequence;
  result.write_duration_us = driver->next_write_duration_us;
  return result;
}

csm::board::can::BuiltinCanTxCancelResult fakeBuiltinCanCancel(
    void* context, uint32_t request_mask) {
  FakeBuiltinCanDriver* driver =
      static_cast<FakeBuiltinCanDriver*>(context);
  csm::board::can::BuiltinCanTxCancelResult result;
  if (driver == nullptr) return result;
  ++driver->cancel_calls;
  driver->last_cancel_mask = request_mask;
  result.driver_result = driver->next_cancel_result;
  result.request_accepted = driver->next_cancel_accepted;
  return result;
}

void captureBuiltinCanCompletion(
    void* context,
    const csm::board::can::BuiltinCanTxCompletion& completion) {
  BuiltinCanCompletionCapture* capture =
      static_cast<BuiltinCanCompletionCapture*>(context);
  if (capture == nullptr || capture->count >= 16) return;
  capture->items[capture->count++] = completion;
}

void builtinCanTxOwnerHasExplicitEnqueueOutcome() {
  using namespace csm::board::can;
  BuiltinCanTxOwner owner;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005u;
  frame.dlc = 8;
  for (uint8_t index = 0; index < 8; ++index) {
    frame.data[index] = static_cast<uint8_t>(0xA0u + index);
  }
  CanBackendState backend;
  backend.ready = true;

  BuiltinCanTxOutcome outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedNotConfigured);
  CHECK(outcome.terminalFailure());
  CHECK(!outcome.driver_called);

  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));

  backend.ready = false;
  outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedBackend);
  CHECK(outcome.terminalFailure());
  CHECK(driver.calls == 0);

  backend.ready = true;
  backend.tx_busy = true;
  outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedBackend);
  CHECK(outcome.transientAdmissionFailure());
  CHECK(driver.calls == 0);

  backend.tx_busy = false;
  frame.bus = 0;
  outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedBus);
  CHECK(driver.calls == 0);

  frame.bus = 1;
  frame.can_id_flags = 0x800u;
  outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedFrame);
  CHECK(driver.calls == 0);

  frame.can_id_flags = (1u << 30) | 0x005u;
  outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedFrame);
  CHECK(driver.calls == 0);

  frame.can_id_flags = 0x005u;
  driver.next_result = 0;
  outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::DriverRejected);
  CHECK(outcome.driver_called);
  CHECK(!outcome.fifoEnqueueAccepted());
  CHECK(outcome.transientAdmissionFailure());
  CHECK(driver.calls == 1);

  driver.next_result = 1;
  driver.next_request_mask = 1;
  outcome = owner.submit(frame, backend, 100);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::FifoEnqueueTracked);
  CHECK(outcome.driver_called);
  CHECK(outcome.fifoEnqueueAccepted());
  CHECK(outcome.completion_tracked);
  CHECK(!outcome.terminalFailure());
  CHECK(driver.calls == 2);
  CHECK(std::memcmp(driver.last_frame.data, frame.data,
                    sizeof(frame.data)) == 0);
  CHECK(owner.activeJournalSlots() == 1);
  CHECK(completions.count == 0);
  owner.serviceCompletions(101, true, 0, 1, 0);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.count == 1);
  CHECK(completions.items[0].code ==
        BuiltinCanTxCompletionCode::Transmitted);
  CHECK(!completions.items[0].failure_previously_reported);
  CHECK(completions.items[0].submission_sequence ==
        outcome.submission_sequence);
  CHECK(completions.items[0].driver_sequence ==
        outcome.submission_sequence);
  CHECK(completions.items[0].driver_result == 1);
  CHECK(completions.items[0].write_duration_us == 7);
  CHECK(std::memcmp(completions.items[0].frame.data, frame.data,
                    sizeof(frame.data)) == 0);

  const BuiltinCanTxOwnerCounters& counters = owner.counters();
  CHECK(counters.submissions == 7);
  CHECK(counters.backend_rejects == 2);
  CHECK(counters.contract_rejects == 3);
  CHECK(counters.driver_rejects == 1);
  CHECK(counters.fifo_enqueue_accepts == 1);
  CHECK(counters.tx_completed == 1);
}

void builtinCanTxOriginAccountingSeparatesRcFromHost() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));
  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005u;
  frame.dlc = 8;

  frame.origin = BuiltinCanTxOrigin::RemoteControl;
  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 100).completion_tracked);
  CHECK(owner.activeJournalSlots() == 1);
  CHECK(owner.activeJournalSlots(BuiltinCanTxOrigin::RemoteControl) == 1);
  CHECK(owner.activeJournalSlots(BuiltinCanTxOrigin::HostControl) == 0);

  frame.origin = BuiltinCanTxOrigin::HostControl;
  driver.next_request_mask = 2;
  CHECK(owner.submit(frame, backend, 101).completion_tracked);
  CHECK(owner.activeJournalSlots() == 2);
  CHECK(owner.activeJournalSlots(BuiltinCanTxOrigin::RemoteControl) == 1);
  CHECK(owner.activeJournalSlots(BuiltinCanTxOrigin::HostControl) == 1);

  owner.serviceCompletions(102, true, 0, 3, 0);
  CHECK(owner.activeJournalSlots() == 0);
}

void builtinCanTxOwnerUsesThreeRealSlotsWithoutHiddenRetry() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));
  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005u;
  frame.dlc = 8;
  frame.origin = BuiltinCanTxOrigin::HostControl;

  for (uint8_t index = 0; index < 3; ++index) {
    frame.command_id = 100u + index;
    frame.data[0] = static_cast<uint8_t>(0xA0u + index);
    driver.next_request_mask = 1u << index;
    const BuiltinCanTxOutcome accepted = owner.submit(frame, backend, 100 + index);
    CHECK(accepted.code == BuiltinCanTxOutcomeCode::FifoEnqueueTracked);
  }
  CHECK(driver.calls == 3);
  CHECK(owner.activeJournalSlots() == 3);
  for (uint8_t index = 0; index < 3; ++index) {
    CHECK(driver.frames[index].command_id == 100u + index);
    CHECK(driver.frames[index].data[0] == static_cast<uint8_t>(0xA0u + index));
  }

  frame.command_id = 103;
  frame.data[0] = 0xA3u;
  const BuiltinCanTxOutcome rejected = owner.submit(frame, backend, 104);
  CHECK(rejected.code == BuiltinCanTxOutcomeCode::RejectedJournalFull);
  CHECK(rejected.transientAdmissionFailure());
  CHECK(driver.calls == 3);
  CHECK(owner.activeJournalSlots() == 3);

  owner.serviceCompletions(105, true, 6, 1, 0);
  CHECK(owner.activeJournalSlots() == 2);
  frame.command_id = 104;
  frame.data[0] = 0xA4u;
  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 106).completion_tracked);
  CHECK(driver.calls == 4);
  CHECK(driver.frames[3].command_id == 104);
  CHECK(driver.frames[3].data[0] == 0xA4u);
  CHECK(owner.counters().submissions == 5);
  CHECK(owner.counters().journal_full_rejects == 1);
}

void hostFreshnessRequiresCoherentAnchorAndRejectsOldWork() {
  using csm::board::control::HostCommandFreshness;
  using csm::board::control::HostCommandFreshnessConfig;
  using csm::board::control::HostFreshnessResult;

  HostCommandFreshness freshness;
  HostCommandFreshnessConfig config;
  config.heartbeat_max_extra_lag_ms = 100;
  config.command_max_age_ms = 40;
  config.clock_future_tolerance_ms = 20;
  CHECK(freshness.begin(config));

  CHECK(freshness.acceptHeartbeat(10, 1000, 5000) ==
        HostFreshnessResult::AnchorEstablished);
  CHECK(!freshness.qualified());
  CHECK(freshness.acceptCommand(11, 1001, 5001) ==
        HostFreshnessResult::NotQualified);
  CHECK(freshness.acceptHeartbeat(11, 1100, 5100) ==
        HostFreshnessResult::Accepted);
  CHECK(freshness.qualified());

  CHECK(freshness.acceptCommand(12, 1110, 5110) ==
        HostFreshnessResult::Accepted);
  CHECK(freshness.acceptCommand(12, 1110, 5110) ==
        HostFreshnessResult::Replay);
  CHECK(freshness.acceptCommand(13, 1069, 5110) ==
        HostFreshnessResult::Stale);
  CHECK(freshness.acceptCommand(13, 1131, 5110) ==
        HostFreshnessResult::Future);
  CHECK(freshness.acceptCommand(13, 1090, 5110) ==
        HostFreshnessResult::Accepted);

  freshness.reset();
  CHECK(freshness.acceptHeartbeat(20, UINT32_MAX - 49u,
                                  UINT32_MAX - 49u) ==
        HostFreshnessResult::AnchorEstablished);
  CHECK(freshness.acceptHeartbeat(21, 50, 50) ==
        HostFreshnessResult::Accepted);
  CHECK(freshness.acceptCommand(22, 60, 60) ==
        HostFreshnessResult::Accepted);
}

void hostFreshnessLatchesHeartbeatTimelineFaultUntilReset() {
  using csm::board::control::HostCommandFreshness;
  using csm::board::control::HostCommandFreshnessConfig;
  using csm::board::control::HostFreshnessResult;

  HostCommandFreshness freshness;
  HostCommandFreshnessConfig config;
  config.heartbeat_max_extra_lag_ms = 100;
  config.command_max_age_ms = 40;
  config.clock_future_tolerance_ms = 20;
  CHECK(freshness.begin(config));
  CHECK(freshness.acceptHeartbeat(1, 100, 1000) ==
        HostFreshnessResult::AnchorEstablished);
  CHECK(freshness.acceptHeartbeat(2, 200, 1201) ==
        HostFreshnessResult::FaultLatched);
  CHECK(freshness.faultLatched());
  CHECK(freshness.acceptHeartbeat(3, 300, 1300) ==
        HostFreshnessResult::FaultLatched);
  CHECK(freshness.acceptCommand(4, 300, 1300) ==
        HostFreshnessResult::FaultLatched);

  freshness.reset();
  CHECK(!freshness.faultLatched());
  CHECK(freshness.acceptHeartbeat(5, 500, 2000) ==
        HostFreshnessResult::AnchorEstablished);
}

void exploratoryFreshnessMeasuresWithoutInventingThresholds() {
  using namespace csm::board::control;
  HostCommandFreshness freshness;
  HostCommandFreshnessConfig config;
  CHECK(freshness.begin(config));
  CHECK(!freshness.timingQualified());
  CHECK(freshness.acceptHeartbeat(1, 1000, 5000) ==
        HostFreshnessResult::AnchorEstablished);
  CHECK(freshness.acceptHeartbeat(2, 1100, 5200) ==
        HostFreshnessResult::Accepted);
  CHECK(freshness.observedHeartbeatExtraLagMs() == 100);
  CHECK(freshness.acceptCommand(3, 1090, 5210) ==
        HostFreshnessResult::Accepted);
  CHECK(freshness.observedCommandAgeMs() == 20);
  CHECK(freshness.acceptCommand(4, 1300, 5220) ==
        HostFreshnessResult::Accepted);
  CHECK(freshness.observedCommandFutureLeadMs() == 180);
}

void hostAuthorityGateHasNoRcOverlapOrArtificialDeadZone() {
  using namespace csm::board::control;
  HostControlAuthorityGate gate;
  gate.reset();
  CHECK(gate.rcAllowed());
  CHECK(gate.activate(true, true, 0));
  CHECK(gate.admissionOpen());
  CHECK(!gate.rcAllowed());
  CHECK(gate.beginClose(HostControlCloseReason::AuthorityPreempted, 2));
  CHECK(!gate.admissionOpen());
  CHECK(!gate.rcAllowed());
  gate.observeHostSlots(1);
  CHECK(!gate.rcAllowed());
  gate.observeHostSlots(0);
  CHECK(gate.rcAllowed());
  CHECK(!gate.admissionOpen());

  gate.reset();
  CHECK(gate.activate(true, true, 0));
  CHECK(gate.beginClose(HostControlCloseReason::LeaseExpired, 0));
  CHECK(gate.rcAllowed());
}

void builtinCanCancellationTargetsOriginWithoutErasingTruth() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));

  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005u;
  frame.dlc = 8;

  frame.origin = BuiltinCanTxOrigin::RemoteControl;
  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 100).completion_tracked);
  frame.origin = BuiltinCanTxOrigin::HostControl;
  driver.next_request_mask = 2;
  CHECK(owner.submit(frame, backend, 101).completion_tracked);

  owner.requestCancellation(BuiltinCanTxOrigin::HostControl,
                            BuiltinCanTxCancelReason::HostDisarm, 102);
  CHECK(driver.cancel_calls == 1);
  CHECK(driver.last_cancel_mask == 2);
  CHECK(owner.activeJournalSlots() == 2);
  // Hardware TX truth wins if TXBTO and TXBCF both appear after cancellation.
  owner.serviceCompletions(103, true, 0, 3, 2);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.count == 2);
  CHECK(completions.items[0].code == BuiltinCanTxCompletionCode::Transmitted);
  CHECK(completions.items[1].code == BuiltinCanTxCompletionCode::Transmitted);

  completions.count = 0;
  frame.origin = BuiltinCanTxOrigin::RemoteControl;
  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 200).completion_tracked);
  frame.origin = BuiltinCanTxOrigin::HostControl;
  driver.next_request_mask = 2;
  CHECK(owner.submit(frame, backend, 201).completion_tracked);
  owner.requestCancellationAll(BuiltinCanTxCancelReason::HardSafety, 202);
  CHECK(driver.cancel_calls == 3);
  CHECK(owner.activeJournalSlots() == 2);
  owner.serviceCompletions(203, true, 0, 0, 3);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.count == 2);
  CHECK(completions.items[0].code == BuiltinCanTxCompletionCode::Cancelled);
  CHECK(completions.items[1].code == BuiltinCanTxCompletionCode::Cancelled);
  CHECK(completions.items[0].cancel_reason ==
        BuiltinCanTxCancelReason::HardSafety);
  CHECK(completions.items[1].cancel_reason ==
        BuiltinCanTxCancelReason::HardSafety);

  completions.count = 0;
  frame.origin = BuiltinCanTxOrigin::HostControl;
  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 300).completion_tracked);
  owner.requestCancellation(BuiltinCanTxOrigin::HostControl,
                            BuiltinCanTxCancelReason::HostSessionFault, 301);
  owner.serviceCompletions(302, true, 0, 0, 1);
  CHECK(completions.count == 1);
  CHECK(completions.items[0].code == BuiltinCanTxCompletionCode::Cancelled);
  CHECK(completions.items[0].cancel_reason ==
        BuiltinCanTxCancelReason::HostSessionFault);
  CHECK(!owner.trackingFaultLatched());
}

struct FakeHostStream : Stream {
  uint8_t bytes[256] = {};
  uint16_t length = 0;
  uint16_t position = 0;

  int available() override {
    return static_cast<int>(length - position);
  }
  int read() override {
    return position < length ? bytes[position++] : -1;
  }
};

struct HostParserCapture {
  uint32_t command_ids[8] = {};
  uint8_t first_data[8] = {};
  uint8_t count = 0;
  uint8_t crc_failures = 0;
};

void captureHostParserFrame(void* context, uint8_t version,
                            uint8_t record_type, uint16_t,
                            const uint8_t* payload, uint16_t len) {
  HostParserCapture* capture = static_cast<HostParserCapture*>(context);
  CHECK(capture != nullptr);
  CHECK(version == csm::kProtocolVersion);
  CHECK(record_type ==
        static_cast<uint8_t>(csm::RecordType::HostCanTxRequest));
  CHECK(len == csm::kHostCanTxRequestPayloadLen);
  if (capture == nullptr || capture->count >= 8 ||
      len != csm::kHostCanTxRequestPayloadLen) return;
  capture->command_ids[capture->count] = csm::rd_u32_le(payload);
  capture->first_data[capture->count] =
      payload[csm::kHostCanTxRequestDataOffset];
  ++capture->count;
}

void captureHostParserCrcFailure(void* context) {
  HostParserCapture* capture = static_cast<HostParserCapture*>(context);
  if (capture != nullptr) ++capture->crc_failures;
}

void hostDownlinkParserPreservesCoalescedBurstOverBufferSize() {
  FakeHostStream stream;
  for (uint8_t index = 0; index < 7; ++index) {
    uint8_t payload[csm::kHostCanTxRequestPayloadLen] = {};
    csm::wr_u32_le(payload, 700u + index);
    payload[csm::kHostCanTxRequestBusOffset] = 1;
    csm::wr_u32_le(&payload[csm::kHostCanTxRequestCanIdOffset], 0x005u);
    payload[csm::kHostCanTxRequestDlcOffset] = 8;
    payload[csm::kHostCanTxRequestDataOffset] =
        static_cast<uint8_t>(0xA0u + index);
    csm::wr_u32_le(&payload[csm::kHostCanTxRequestMonoMsOffset],
                   1000u + index);
    size_t written = 0;
    CHECK(csm::encode_typed_frame(
        &stream.bytes[stream.length], sizeof(stream.bytes) - stream.length,
        csm::RecordType::HostCanTxRequest, payload, sizeof(payload), index,
        0, &written));
    stream.length = static_cast<uint16_t>(stream.length + written);
  }
  CHECK(stream.length > 192);
  CHECK(stream.length <= sizeof(stream.bytes));

  HostParserCapture capture;
  csm::board::HostDownlinkParser parser(
      captureHostParserFrame, captureHostParserCrcFailure, &capture);
  parser.service(stream, 256);
  CHECK(stream.available() == 0);
  CHECK(capture.crc_failures == 0);
  CHECK(capture.count == 7);
  for (uint8_t index = 0; index < 7; ++index) {
    CHECK(capture.command_ids[index] == 700u + index);
    CHECK(capture.first_data[index] == static_cast<uint8_t>(0xA0u + index));
  }
}

void builtinCanTxJournalCoversAllTerminalPaths() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));
  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005;
  frame.dlc = 8;

  // TXBCF is a terminal failure and frees its journal slot.
  driver.next_request_mask = 2;
  auto outcome = owner.submit(frame, backend, 20);
  CHECK(outcome.completion_tracked);
  owner.serviceCompletions(21, true, 0, 0, 2);
  CHECK(completions.count == 1);
  CHECK(completions.items[0].code == BuiltinCanTxCompletionCode::Cancelled);
  CHECK(completions.items[0].terminal);
  CHECK(!completions.items[0].failure_previously_reported);
  CHECK(owner.activeJournalSlots() == 0);

  // A deadline while TXBRP is still set is an intermediate failure. It is
  // reported once, retains the slot, and a late TXBTO still publishes the
  // authoritative transmitted terminal outcome.
  driver.next_request_mask = 4;
  outcome = owner.submit(frame, backend, 30);
  owner.serviceCompletions(39, true, 4, 0, 0);
  CHECK(completions.count == 1);
  owner.serviceCompletions(40, true, 4, 0, 0);
  CHECK(completions.count == 2);
  CHECK(completions.items[1].code ==
        BuiltinCanTxCompletionCode::DeadlineExceededPending);
  CHECK(!completions.items[1].terminal);
  CHECK(!completions.items[1].deadline_previously_reported);
  CHECK(!completions.items[1].failure_previously_reported);
  CHECK(driver.cancel_calls == 1);
  CHECK(driver.last_cancel_mask == 4);
  CHECK(owner.activeJournalSlots() == 1);
  owner.serviceCompletions(41, true, 4, 0, 0);
  CHECK(completions.count == 2);
  owner.serviceCompletions(42, true, 0, 4, 0);
  CHECK(completions.count == 3);
  CHECK(completions.items[2].code ==
        BuiltinCanTxCompletionCode::Transmitted);
  CHECK(completions.items[2].terminal);
  CHECK(completions.items[2].deadline_previously_reported);
  CHECK(completions.items[2].failure_previously_reported);
  CHECK(owner.activeJournalSlots() == 0);

  // A FIFO slot can be reused after the prior terminal TXBTO.
  driver.next_request_mask = 4;
  outcome = owner.submit(frame, backend, 50);
  CHECK(outcome.completion_tracked);
  owner.serviceCompletions(51, true, 0, 4, 4);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.items[completions.count - 1u].code ==
        BuiltinCanTxCompletionCode::Transmitted);

  // Deadline arithmetic remains correct across uint32_t microsecond wrap,
  // and a later TXBCF is the terminal cancellation.
  driver.next_request_mask = 2;
  outcome = owner.submit(frame, backend, UINT32_MAX - 5u);
  owner.serviceCompletions(3, true, 2, 0, 0);
  CHECK(owner.activeJournalSlots() == 1);
  owner.serviceCompletions(4, true, 2, 0, 0);
  CHECK(owner.activeJournalSlots() == 1);
  CHECK(completions.items[completions.count - 1u].code ==
        BuiltinCanTxCompletionCode::DeadlineExceededPending);
  CHECK(driver.cancel_calls == 2);
  CHECK(driver.last_cancel_mask == 2);
  owner.serviceCompletions(5, true, 0, 0, 2);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.items[completions.count - 1u].code ==
        BuiltinCanTxCompletionCode::Cancelled);
  CHECK(completions.items[completions.count - 1u].
        deadline_previously_reported);
  CHECK(completions.items[completions.count - 1u].
        failure_previously_reported);

  // Exactly three hardware FIFO requests can be pending. A fourth request is
  // rejected before the driver is called; completion then releases all three.
  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 100).completion_tracked);
  driver.next_request_mask = 2;
  CHECK(owner.submit(frame, backend, 100).completion_tracked);
  driver.next_request_mask = 4;
  CHECK(owner.submit(frame, backend, 100).completion_tracked);
  const uint32_t calls_before_full = driver.calls;
  driver.next_request_mask = 1;
  outcome = owner.submit(frame, backend, 100);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedJournalFull);
  CHECK(outcome.transientAdmissionFailure());
  CHECK(!outcome.driver_called);
  CHECK(driver.calls == calls_before_full);
  CHECK(owner.activeJournalSlots() == 3);
  owner.serviceCompletions(101, true, 0, 7, 0);
  CHECK(owner.activeJournalSlots() == 0);

  // At the deadline, a request absent from TXBRP/TXBTO/TXBCF is an explicit
  // terminal correlation fault. The owner remains fail-closed for this boot.
  driver.next_request_mask = 1;
  outcome = owner.submit(frame, backend, 110);
  CHECK(outcome.completion_tracked);
  owner.serviceCompletions(120, true, 0, 0, 0);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(owner.trackingFaultLatched());
  CHECK(completions.items[completions.count - 1u].code ==
        BuiltinCanTxCompletionCode::DisappearedWithoutOutcome);
  CHECK(completions.items[completions.count - 1u].terminal);

  const uint32_t calls_before_tracking_reject = driver.calls;
  outcome = owner.submit(frame, backend, 121);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedTrackingFault);
  CHECK(outcome.terminalFailure());
  CHECK(!outcome.driver_called);
  CHECK(!outcome.fifoEnqueueAccepted());
  CHECK(driver.calls == calls_before_tracking_reject);
  CHECK(owner.counters().invalid_request_masks == 0);
  CHECK(owner.counters().tracking_fault_rejects == 1);
  CHECK(owner.counters().journal_full_rejects == 1);
  CHECK(owner.counters().tx_cancelled == 2);
  CHECK(owner.counters().terminal_hardware_failures == 2);
  CHECK(owner.counters().intentional_cancellations == 0);
  CHECK(owner.counters().tx_deadline_exceeded == 2);
  CHECK(owner.counters().tx_disappeared == 1);
  CHECK(owner.counters().tx_completed == 5);
}

void builtinCanInvalidSnapshotHoldsJournalFailClosed() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));
  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005;
  frame.dlc = 8;

  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 0).completion_tracked);
  driver.next_request_mask = 2;
  CHECK(owner.submit(frame, backend, 0).completion_tracked);
  driver.next_request_mask = 4;
  CHECK(owner.submit(frame, backend, 0).completion_tracked);

  owner.serviceCompletions(100, false, 0, 0, 0);
  CHECK(owner.activeJournalSlots() == 3);
  CHECK(owner.trackingFaultLatched());
  CHECK(completions.count == 3);
  for (uint8_t i = 0; i < completions.count; ++i) {
    CHECK(completions.items[i].code ==
          BuiltinCanTxCompletionCode::TrackingCompromised);
    CHECK(!completions.items[i].terminal);
    CHECK(!completions.items[i].failure_previously_reported);
  }
  CHECK(owner.counters().snapshot_invalid_polls == 1);

  const uint32_t calls_before_reject = driver.calls;
  const auto outcome = owner.submit(frame, backend, 101);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedTrackingFault);
  CHECK(!outcome.driver_called);
  CHECK(driver.calls == calls_before_reject);
  CHECK(owner.activeJournalSlots() == 3);

  owner.serviceCompletions(102, true, 0, 0, 0);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.count == 6);
  for (uint8_t i = 3; i < completions.count; ++i) {
    CHECK(completions.items[i].code ==
          BuiltinCanTxCompletionCode::TrackingCompromised);
    CHECK(completions.items[i].terminal);
    CHECK(completions.items[i].failure_previously_reported);
  }
}

void builtinCanDuplicateMaskLatchesTrackingFault() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));
  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005;
  frame.dlc = 8;

  driver.next_request_mask = 1;
  CHECK(owner.submit(frame, backend, 0).completion_tracked);
  CHECK(owner.activeJournalSlots() == 1);

  // A second accepted write claiming the still-owned request bit destroys
  // correlation identity. The prior slot becomes compromised and the new
  // frame is untracked; the owner cannot be reopened during this boot.
  const auto duplicate = owner.submit(frame, backend, 1);
  CHECK(duplicate.fifoEnqueueAccepted());
  CHECK(!duplicate.completion_tracked);
  CHECK(owner.trackingFaultLatched());
  CHECK(owner.activeJournalSlots() == 1);
  CHECK(completions.count == 2);
  CHECK(completions.items[0].code ==
        BuiltinCanTxCompletionCode::TrackingCompromised);
  CHECK(!completions.items[0].terminal);
  CHECK(!completions.items[0].failure_previously_reported);
  CHECK(completions.items[1].code ==
        BuiltinCanTxCompletionCode::InvalidRequestMask);
  CHECK(completions.items[1].terminal);
  CHECK(!completions.items[1].failure_previously_reported);

  owner.serviceCompletions(2, true, 0, 0, 0);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.count == 3);
  CHECK(completions.items[2].code ==
        BuiltinCanTxCompletionCode::TrackingCompromised);
  CHECK(completions.items[2].terminal);
  CHECK(completions.items[2].failure_previously_reported);

  const uint32_t calls_before_reject = driver.calls;
  const auto rejected = owner.submit(frame, backend, 3);
  CHECK(rejected.code == BuiltinCanTxOutcomeCode::RejectedTrackingFault);
  CHECK(!rejected.driver_called);
  CHECK(driver.calls == calls_before_reject);
}

void builtinCanCancelFailureAndIdentityWrapFailClosed() {
  using namespace csm::board::can;
  FakeBuiltinCanDriver driver;
  BuiltinCanCompletionCapture completions;
  BuiltinCanTxOwner owner;
  CHECK(owner.begin(1, fakeBuiltinCanWrite, &driver, fakeBuiltinCanCancel,
                    &driver, 10, captureBuiltinCanCompletion, &completions));
  CanBackendState backend;
  backend.ready = true;
  BuiltinCanTxFrame frame;
  frame.bus = 1;
  frame.can_id_flags = 0x005;
  frame.dlc = 8;

  owner.setSubmissionSequenceForTest(UINT32_MAX - 1u);
  driver.next_request_mask = 1;
  auto outcome = owner.submit(frame, backend, 0);
  CHECK(outcome.submission_sequence == UINT32_MAX);
  owner.serviceCompletions(1, true, 0, 1, 0);
  CHECK(completions.items[0].submission_sequence == UINT32_MAX);
  CHECK(completions.items[0].driver_sequence == UINT32_MAX);

  driver.next_request_mask = 2;
  outcome = owner.submit(frame, backend, 2);
  CHECK(outcome.submission_sequence == 1u);
  owner.serviceCompletions(3, true, 0, 2, 0);
  CHECK(completions.items[1].submission_sequence == 1u);
  CHECK(owner.counters().submissions == 2u);
  CHECK(owner.counters().tx_completed == 2u);

  driver.next_request_mask = 4;
  driver.next_cancel_result = 1;
  driver.next_cancel_accepted = false;
  outcome = owner.submit(frame, backend, 10);
  CHECK(outcome.completion_tracked);
  owner.serviceCompletions(20, true, 4, 0, 0);
  CHECK(!owner.trackingFaultLatched());
  CHECK(owner.activeJournalSlots() == 1);
  CHECK(driver.cancel_calls == 1);
  CHECK(driver.last_cancel_mask == 4);
  CHECK(completions.items[2].code ==
        BuiltinCanTxCompletionCode::DeadlineExceededPending);
  CHECK(!completions.items[2].terminal);
  CHECK(!completions.items[2].failure_previously_reported);
  // First cancel rejection is not a terminal result. The next valid HW
  // snapshot observes TXBRP and performs one bounded retry.
  owner.serviceCompletions(21, true, 4, 0, 0);
  CHECK(driver.cancel_calls == 2);
  CHECK(!owner.trackingFaultLatched());
  owner.serviceCompletions(22, true, 4, 0, 0);
  CHECK(owner.trackingFaultLatched());
  CHECK(completions.items[3].code ==
        BuiltinCanTxCompletionCode::CancelRequestFailed);
  CHECK(!completions.items[3].terminal);
  CHECK(completions.items[3].failure_previously_reported);
  CHECK(completions.items[3].cancel_driver_result == 1);

  const uint32_t calls_before_reject = driver.calls;
  outcome = owner.submit(frame, backend, 23);
  CHECK(outcome.code == BuiltinCanTxOutcomeCode::RejectedTrackingFault);
  CHECK(driver.calls == calls_before_reject);

  owner.serviceCompletions(24, true, 0, 0, 4);
  CHECK(owner.activeJournalSlots() == 0);
  CHECK(completions.items[4].code == BuiltinCanTxCompletionCode::Cancelled);
  CHECK(completions.items[4].terminal);
  CHECK(completions.items[4].failure_previously_reported);
  CHECK(owner.counters().cancel_request_failures == 2);
}

void hostTransportEpochInvalidatesHeartbeatAndLease() {
  using csm::board::SafetyInputs;
  using csm::board::SafetyState;
  using csm::board::SafetySupervisor;

  SafetySupervisor supervisor;
  supervisor.begin(0);
  SafetyInputs inputs;
  inputs.field_power_ok = true;
  inputs.control_backend_ready = true;
  supervisor.update(0, inputs);
  CHECK(supervisor.heartbeat(1) == csm::ControlReasonOk);
  supervisor.update(1, inputs);
  CHECK(supervisor.arm(2, 500, true) == csm::ControlReasonOk);
  CHECK(supervisor.leaseAlive(3));

  supervisor.invalidateHostSession(10);
  CHECK(!supervisor.heartbeatAlive(10));
  CHECK(!supervisor.leaseAlive(10));
  CHECK(supervisor.state() == SafetyState::MonitorOnly);
  CHECK(supervisor.renewLease(11, 500) == csm::ControlReasonHostTimeout);
  CHECK(supervisor.arm(11, 500, true) == csm::ControlReasonHostTimeout);

  CHECK(supervisor.heartbeat(12) == csm::ControlReasonOk);
  supervisor.update(12, inputs);
  CHECK(supervisor.arm(13, 500, true) == csm::ControlReasonOk);

  inputs.estop_asserted = true;
  supervisor.update(14, inputs);
  supervisor.invalidateHostSession(15);
  CHECK(supervisor.state() == SafetyState::Estop);
}

void packChannels(const uint16_t channels[16], uint8_t payload[22]) {
  std::memset(payload, 0, 22);
  uint32_t bit_offset = 0;
  for (uint8_t channel = 0; channel < 16; ++channel) {
    for (uint8_t bit = 0; bit < 11; ++bit, ++bit_offset) {
      if ((channels[channel] & (1u << bit)) != 0) {
        payload[bit_offset / 8u] |= static_cast<uint8_t>(1u << (bit_offset % 8u));
      }
    }
  }
}

csm::board::remote::CrsfFrame parseFrame(const uint8_t* bytes, uint8_t length,
                                         csm::board::remote::CrsfParseStatus* status) {
  csm::board::remote::CrsfParser parser;
  parser.reset();
  csm::board::remote::CrsfParseResult result;
  for (uint8_t index = 0; index < length; ++index) result = parser.ingest(bytes[index]);
  *status = result.status;
  return result.frame;
}

void crsfChannelsDecodeAndNormalize() {
  using namespace csm::board::remote;
  uint16_t raw[16];
  for (uint8_t index = 0; index < 16; ++index) raw[index] = 992;
  raw[1] = 1811;  // CH2 drive.
  raw[3] = 172;   // CH4 steering.
  uint8_t payload[kCrsfRcChannelsPackedPayloadBytes];
  packChannels(raw, payload);
  uint8_t bytes[kCrsfMaxFrameBytes] = {};
  const uint8_t length = buildCrsfBroadcastFrame(
      kCrsfFrameTypeRcChannelsPacked, payload, sizeof(payload), bytes, sizeof(bytes));
  CHECK(length == 26);

  CrsfParseStatus parse_status = CrsfParseStatus::Waiting;
  const CrsfFrame frame = parseFrame(bytes, length, &parse_status);
  CHECK(parse_status == CrsfParseStatus::FrameReady);
  CrsfRcChannels decoded;
  CHECK(decodeCrsfRcChannelsPacked(frame, &decoded) == CrsfDecodeStatus::Ok);
  for (uint8_t index = 0; index < 16; ++index) CHECK(decoded.raw[index] == raw[index]);

  RcNormalizer normalizer;
  RcNormalizerConfig config;
  config.configured = true;
  config.required_channel_mask = kRemoteRequiredRcChannelMask;
  CHECK(normalizer.configure(config));
  const RcNormalizeResult normalized =
      normalizer.normalizeCrsfChannels(100, 7, decoded, 96, 42, 0);
  CHECK(normalized.accepted);
  CHECK(normalized.sample.ch[1] == 1000);
  CHECK(normalized.sample.ch[3] == -1000);
  CHECK(normalized.sample.ch[0] == 0);

  decoded.raw[15] = 0;
  const RcNormalizeResult unused_out_of_range =
      normalizer.normalizeCrsfChannels(101, 8, decoded, 96, 42, 0);
  CHECK(unused_out_of_range.accepted);
  CHECK(unused_out_of_range.sample.ch[15] == -1000);
  decoded.raw[4] = 0;
  CHECK(!normalizer.normalizeCrsfChannels(102, 9, decoded, 96, 42, 0)
             .accepted);

  RcNormalizer invalid_normalizer;
  RcNormalizerConfig invalid_config;
  invalid_config.configured = true;
  CHECK(!invalid_normalizer.configure(invalid_config));

  bytes[length - 1u] ^= 0x01u;
  parseFrame(bytes, length, &parse_status);
  CHECK(parse_status == CrsfParseStatus::RejectedCrc);

  const uint8_t heartbeat_payload[2] = {0x00, 0xC8};
  const uint8_t heartbeat_len = buildCrsfBroadcastFrame(
      kCrsfFrameTypeHeartbeat, heartbeat_payload, sizeof(heartbeat_payload),
      bytes, sizeof(bytes));
  CHECK(heartbeat_len == 6);
  const CrsfFrame heartbeat = parseFrame(bytes, heartbeat_len, &parse_status);
  CHECK(parse_status == CrsfParseStatus::FrameReady);
  CHECK(heartbeat.type == kCrsfFrameTypeHeartbeat);
  CHECK(heartbeat.payload_len == 2);
  CHECK(heartbeat.payload[0] == 0x00 && heartbeat.payload[1] == 0xC8);
}

void upstreamAutonomyPrecedesRemoteReservation() {
  using namespace csm::board;
  authority::AuthorityManager manager;
  manager.begin(0);
  authority::AuthorityInputs inputs;
  inputs.autonomy_state = authority::AutonomyAuthorityState::ActiveConfirmed;
  inputs.host_service_enabled = true;
  inputs.host_service_request = true;
  inputs.safety_supervisor_allows = true;
  inputs.remote_source_present = true;
  inputs.remote_source_valid = true;
  inputs.remote_takeover_request = true;

  auto decision = manager.update(10, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedAutonomyActive);
  CHECK(manager.activeSource() == authority::ControlSourceId::None);

  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  decision = manager.update(15, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedNotNeutral);
  CHECK(decision.source == authority::ControlSourceId::Remote);

  inputs.remote_handoff_qualified = true;
  decision = manager.update(20, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::Accepted);
  CHECK(manager.activeSource() == authority::ControlSourceId::Remote);

  inputs.estop_asserted = true;
  inputs.autonomy_state = authority::AutonomyAuthorityState::ActiveConfirmed;
  decision = manager.update(30, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedSafetySupervisor);
  CHECK(manager.activeSource() == authority::ControlSourceId::None);

  inputs.estop_asserted = false;
  inputs.fault_lockout = true;
  decision = manager.update(31, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedFaultLockout);

  inputs.fault_lockout = false;
  inputs.local_tx_inhibit_latched = true;
  decision = manager.update(32, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedLocalTxInhibit);

  inputs.local_tx_inhibit_latched = false;
  inputs.safety_supervisor_allows = false;
  decision = manager.update(33, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedSafetySupervisor);

  control::OperatorCommand command;
  command.source = authority::ControlSourceId::Remote;
  authority::AuthorityInputs evaluation = inputs;
  evaluation.safety_supervisor_allows = true;
  evaluation.estop_asserted = true;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedSafetySupervisor);
  evaluation.estop_asserted = false;
  evaluation.fault_lockout = true;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedFaultLockout);
  evaluation.fault_lockout = false;
  evaluation.local_tx_inhibit_latched = true;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedLocalTxInhibit);
  evaluation.local_tx_inhibit_latched = false;
  evaluation.safety_supervisor_allows = false;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedSafetySupervisor);
}

void frozenMailboxCannotRemainFresh() {
  using namespace csm::board::remote;
  RcSample sample;
  sample.sample_state = RcSampleState::Ok;
  sample.seq = 1;
  M4RemoteMailboxWriter writer;
  M4RemoteMailboxFrame frame;
  writer.reset();
  writer.clearFrame(&frame);
  CHECK(writer.publishSample(sample, &frame).accepted);

  M4RemoteMailboxReader reader;
  reader.begin(0);
  CHECK(reader.updateFromMailboxFrame(10, frame));
  CHECK(reader.hasFreshUsableSample());
  CHECK(!reader.updateFromMailboxFrame(111, frame));
  CHECK(reader.snapshot().link_state == RemoteLinkState::Stale);
  CHECK(reader.snapshot().age_ms == 101);
}

void drivePayloadMatchesVehicleBenchGoldenFrames() {
  using namespace csm::board;
  control::VehicleCommandMapper mapper;
  mapper.begin(0);
  control::VehicleCommandProfile profile;
  profile.configured = true;
  profile.output_enabled = true;
  profile.mapping = control::VehicleCommandMapping::Vehicle0x005And0x007;
  profile.bus = 1;
  profile.policy_id = 0x5243;
  profile.throttle_limit_permille = 1000;
  profile.steer_limit_permille = 1000;
  profile.brake_limit_permille = 1000;
  CHECK(mapper.configure(profile));

  auto check = [&](int16_t throttle, const uint8_t expected[8]) {
    control::OperatorCommand command;
    command.source = authority::ControlSourceId::Remote;
    command.throttle_permille = throttle;
    const control::VehicleCommandMapResult mapped = mapper.map(command);
    CHECK(mapped.mapped);
    CHECK(mapped.frame_count == 2);
    CHECK(mapped.frames[0].can_id_flags == control::kRemoteDriveCanId);
    CHECK(mapped.frames[0].dlc == 8);
    CHECK(std::memcmp(mapped.frames[0].data, expected, 8) == 0);
  };

  const uint8_t stop[8] =
      {0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t forward_80[8] =
      {0xAA, 0x52, 0x20, 0x03, 0x50, 0x00, 0x00, 0x00};
  const uint8_t forward_100[8] =
      {0xAA, 0x52, 0xE8, 0x03, 0x50, 0x00, 0x00, 0x00};
  const uint8_t reverse_80[8] =
      {0xAA, 0x52, 0x20, 0x03, 0x60, 0x00, 0x00, 0x00};
  const uint8_t reverse_100[8] =
      {0xAA, 0x52, 0xE8, 0x03, 0x60, 0x00, 0x00, 0x00};
  const uint8_t forward_minimum[8] =
      {0xAA, 0x52, 0xC8, 0x00, 0x50, 0x00, 0x00, 0x00};
  const uint8_t forward_25[8] =
      {0xAA, 0x52, 0xFA, 0x00, 0x50, 0x00, 0x00, 0x00};
  const uint8_t reverse_minimum[8] =
      {0xAA, 0x52, 0xC8, 0x00, 0x60, 0x00, 0x00, 0x00};
  check(0, stop);
  check(50, stop);
  check(51, forward_minimum);
  check(224, forward_minimum);
  check(225, forward_25);
  check(-51, reverse_minimum);
  check(799, forward_80);
  check(800, forward_80);
  check(1000, forward_100);
  check(-800, reverse_80);
  check(-1000, reverse_100);

  const control::VehicleCommandMapResult full_stop = mapper.mapSafetyStop(40);
  CHECK(full_stop.mapped);
  CHECK(full_stop.frame_count == 2);
  CHECK(full_stop.frames[0].can_id_flags == control::kRemoteDriveCanId);
  CHECK(full_stop.frames[1].can_id_flags == control::kRemoteSteeringCanId);
  CHECK(full_stop.frames[1].data[0] == control::kRemoteSteeringCenter);
  CHECK(full_stop.frames[1].data[7] == 0);

  profile.mapping = control::VehicleCommandMapping::VehicleMdps0x007Only;
  CHECK(mapper.configure(profile));
  control::OperatorCommand mdps_command;
  mdps_command.source = authority::ControlSourceId::Remote;
  mdps_command.throttle_permille = 1000;
  mdps_command.steer_permille = -1000;
  const control::VehicleCommandMapResult mdps = mapper.map(mdps_command);
  CHECK(mdps.mapped);
  CHECK(mdps.frame_count == 1);
  CHECK(mdps.frames[0].can_id_flags == control::kRemoteSteeringCanId);
  const control::VehicleCommandMapResult mdps_stop = mapper.mapSafetyStop(41);
  CHECK(mdps_stop.mapped);
  CHECK(mdps_stop.frame_count == 1);
  CHECK(mdps_stop.frames[0].can_id_flags == control::kRemoteSteeringCanId);
  CHECK(mdps_stop.frames[0].data[0] == control::kRemoteSteeringCenter);
  CHECK(mdps_stop.frames[0].data[7] == 0);
}

void remotePreemptsAutonomyAndMapsCh4Ch5Ch10Ch11() {
  using namespace csm::board;
  authority::AuthorityManager authority_manager;
  control::CommandLimiter limiter;
  control::VehicleCommandMapper mapper;
  control::CanTxGateway gateway;
  control::RemoteControlOrchestrator orchestrator;
  authority_manager.begin(0);
  limiter.begin(0);
  mapper.begin(0);
  gateway.begin(0);
  orchestrator.begin(0);

  control::CommandLimiterConfig limiter_config;
  limiter_config.configured = true;
  limiter_config.throttle_min_permille = -1000;
  limiter_config.throttle_max_permille = 1000;
  limiter_config.steer_min_permille = -1000;
  limiter_config.steer_max_permille = 1000;
  limiter_config.brake_min_permille = 0;
  limiter_config.brake_max_permille = 1000;
  limiter_config.throttle_rise_step_permille = 1000;
  limiter_config.throttle_fall_step_permille = 1000;
  limiter_config.steer_step_permille = 1000;
  limiter_config.steer_return_step_permille = 1000;
  CHECK(limiter.configure(limiter_config));

  control::VehicleCommandProfile vehicle_profile;
  vehicle_profile.configured = true;
  vehicle_profile.output_enabled = true;
  vehicle_profile.mapping = control::VehicleCommandMapping::VehicleBench0x005And0x007;
  vehicle_profile.bus = 1;
  vehicle_profile.policy_id = 0x5243;
  vehicle_profile.throttle_limit_permille = 1000;
  vehicle_profile.steer_limit_permille = 1000;
  vehicle_profile.brake_limit_permille = 1000;
  CHECK(mapper.configure(vehicle_profile));

  control::CanTxGatewayPolicy gateway_policy;
  gateway_policy.configured = true;
  gateway_policy.build_profile_allows_local_tx = true;
  gateway_policy.bus = 1;
  gateway_policy.policy_id = 0x5243;
  gateway_policy.allowlist_count = 2;
  gateway_policy.allowlist_ids[0] = control::kRemoteDriveCanId;
  gateway_policy.allowlist_ids[1] = control::kRemoteSteeringCanId;
  CHECK(gateway.configure(gateway_policy));

  remote::RemoteControlSourceConfig source_config;
  source_config.drive_channel_index = 1;
  source_config.steering_channel_index = 3;
  source_config.auxiliary_channel_index = 4;
  source_config.steering_overlay_channel_index = 9;
  source_config.momentary_overlay_channel_index = 10;
  source_config.drive_deadband_permille =
      control::kRemoteDriveDeadbandPermille;
  source_config.steering_deadband_permille = 20;
  source_config.auxiliary_threshold_permille = 500;
  CHECK(orchestrator.configureRemoteSource(source_config));

  control::RemoteControlOrchestratorInputs inputs;
  inputs.mailbox_snapshot.sample_present = true;
  inputs.mailbox_snapshot.integrity_ok = true;
  inputs.mailbox_snapshot.link_state = remote::RemoteLinkState::Valid;
  inputs.mailbox_snapshot.sample.sample_state = remote::RcSampleState::Ok;
  inputs.mailbox_snapshot.sample.seq = 99;
  inputs.mailbox_snapshot.sample.ch[1] = 10;
  inputs.mailbox_snapshot.sample.ch[3] = 0;
  inputs.mailbox_snapshot.sample.ch[4] = 0;
  inputs.mailbox_snapshot.sample.ch[9] = 0;
  inputs.mailbox_snapshot.sample.ch[10] = -1000;
  inputs.output_sequence = 6;
  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.local_tx_inhibit_latched = false;
  inputs.safety_supervisor_allows = true;
  inputs.remote_source_present = true;
  inputs.remote_handoff_qualified = true;
  inputs.remote_takeover_request = true;
  inputs.backend_state.ready = true;

  control::RemoteControlOrchestratorDeps deps;
  deps.authority_manager = &authority_manager;
  deps.command_limiter = &limiter;
  deps.vehicle_mapper = &mapper;
  deps.can_tx_gateway = &gateway;
  const auto drive_deadband = orchestrator.tick(10, inputs, deps);
  CHECK(drive_deadband.accepted);
  CHECK(drive_deadband.command.throttle_permille == 0);
  CHECK(drive_deadband.frames[0].data[1] == control::kRemoteDriveStopMode);

  inputs.mailbox_snapshot.sample.ch[1] = 1000;
  inputs.mailbox_snapshot.sample.ch[3] = -1000;
  inputs.output_sequence = 7;
  const auto result = orchestrator.tick(20, inputs, deps);
  CHECK(result.accepted);
  CHECK(result.command.command_seq == 7);
  CHECK(result.command.throttle_permille == 1000);
  CHECK(result.command.steer_permille == -1000);
  CHECK(result.authority_decision.source == authority::ControlSourceId::Remote);
  CHECK(result.command.auxiliary_permille == 0);
  CHECK(result.frame_count == 2);
  CHECK(result.frames[0].can_id_flags == control::kRemoteDriveCanId);
  CHECK(result.frames[0].data[0] == control::kRemoteDriveHeader);
  CHECK(result.frames[0].data[1] == control::kRemoteDriveMode);
  CHECK(result.frames[0].data[2] == 0xE8);
  CHECK(result.frames[0].data[3] == 0x03);
  CHECK(result.frames[0].data[4] == control::kRemoteDriveForward);
  CHECK(result.frames[1].can_id_flags == control::kRemoteSteeringCanId);
  CHECK(result.frames[1].data[0] == control::kRemoteSteeringMinimum);
  for (uint8_t index = 1; index < 8; ++index) {
    CHECK(result.frames[1].data[index] == 0);
  }

  inputs.output_sequence = 8;
  inputs.mailbox_snapshot.sample.ch[3] = 10;
  auto deadband = orchestrator.tick(40, inputs, deps);
  CHECK(deadband.accepted);
  CHECK(deadband.command.steer_permille == 0);
  CHECK(deadband.frames[1].data[0] == control::kRemoteSteeringCenter);

  inputs.output_sequence = 9;
  inputs.mailbox_snapshot.sample.ch[3] = 1000;
  inputs.mailbox_snapshot.sample.ch[4] = -1000;
  auto auxiliary_negative = orchestrator.tick(60, inputs, deps);
  CHECK(auxiliary_negative.accepted);
  CHECK(auxiliary_negative.command.throttle_permille == 0);
  CHECK(auxiliary_negative.command.steer_permille == 0);
  CHECK(auxiliary_negative.command.auxiliary_permille == -1000);
  CHECK(auxiliary_negative.frames[0].data[1] == control::kRemoteDriveStopMode);
  CHECK(auxiliary_negative.frames[1].data[0] ==
        control::kRemoteSteeringCenter);
  for (uint8_t index = 1; index < 7; ++index) {
    CHECK(auxiliary_negative.frames[1].data[index] == 0);
  }
  CHECK(auxiliary_negative.frames[1].data[7] == control::kRemoteAuxiliaryNegative);

  inputs.output_sequence = 10;
  inputs.mailbox_snapshot.sample.ch[4] = 1000;
  auto auxiliary_positive = orchestrator.tick(80, inputs, deps);
  CHECK(auxiliary_positive.accepted);
  CHECK(auxiliary_positive.command.auxiliary_permille == 1000);
  CHECK(auxiliary_positive.frames[1].data[0] ==
        control::kRemoteSteeringCenter);
  for (uint8_t index = 1; index < 7; ++index) {
    CHECK(auxiliary_positive.frames[1].data[index] == 0);
  }
  CHECK(auxiliary_positive.frames[1].data[7] == control::kRemoteAuxiliaryPositive);

  inputs.output_sequence = 11;
  inputs.mailbox_snapshot.sample.ch[4] = 0;
  auto steering_positive = orchestrator.tick(100, inputs, deps);
  CHECK(steering_positive.accepted);
  CHECK(steering_positive.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(steering_positive.frames[1].data[7] == 0);

  inputs.output_sequence = 12;
  inputs.mailbox_snapshot.sample.ch[9] = 1000;
  auto steering_overlay_positive = orchestrator.tick(120, inputs, deps);
  CHECK(steering_overlay_positive.accepted);
  CHECK(steering_overlay_positive.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(steering_overlay_positive.frames[1].data[7] == control::kRemoteAuxiliaryPositive);

  inputs.output_sequence = 13;
  inputs.mailbox_snapshot.sample.ch[9] = -1000;
  auto steering_overlay_negative = orchestrator.tick(140, inputs, deps);
  CHECK(steering_overlay_negative.accepted);
  CHECK(steering_overlay_negative.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(steering_overlay_negative.frames[1].data[7] == control::kRemoteAuxiliaryNegative);

  inputs.output_sequence = 14;
  inputs.mailbox_snapshot.sample.ch[9] = 1000;
  inputs.mailbox_snapshot.sample.ch[10] = 1000;
  auto momentary_overlay_positive = orchestrator.tick(160, inputs, deps);
  CHECK(momentary_overlay_positive.accepted);
  CHECK(momentary_overlay_positive.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(momentary_overlay_positive.frames[1].data[7] == control::kRemoteAuxiliaryNegative);

  inputs.output_sequence = 15;
  inputs.mailbox_snapshot.sample.ch[4] = 1000;
  auto auxiliary_precedence = orchestrator.tick(180, inputs, deps);
  CHECK(auxiliary_precedence.accepted);
  CHECK(auxiliary_precedence.frames[1].data[0] ==
        control::kRemoteSteeringCenter);
  for (uint8_t index = 1; index < 7; ++index) {
    CHECK(auxiliary_precedence.frames[1].data[index] == 0);
  }
  CHECK(auxiliary_precedence.frames[1].data[7] == control::kRemoteAuxiliaryPositive);
}

void absoluteReleaseSchedulePreservesPhaseAndCountsMisses() {
  using namespace csm::board::control;

  ControlReleaseSchedule schedule;
  CHECK(!schedule.begin(100, 0, 20));
  CHECK(!schedule.poll(100).drive.due);
  CHECK(schedule.begin(100, 5, 20));

  auto releases = schedule.poll(99);
  CHECK(!releases.drive.due);
  CHECK(!releases.steering.due);

  releases = schedule.poll(100);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 100);
  CHECK(releases.drive.sequence == 1);
  CHECK(releases.drive.missed_releases == 0);
  CHECK(releases.steering.due);
  CHECK(releases.steering.scheduled_ms == 100);
  CHECK(releases.steering.sequence == 1);
  CHECK(releases.steering.missed_releases == 0);
  CHECK(!schedule.poll(100).drive.due);

  releases = schedule.poll(106);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 105);
  CHECK(releases.drive.sequence == 2);
  CHECK(releases.drive.missed_releases == 0);
  CHECK(!releases.steering.due);

  releases = schedule.poll(117);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 115);
  CHECK(releases.drive.sequence == 4);
  CHECK(releases.drive.missed_releases == 1);
  CHECK(!releases.steering.due);
  CHECK(!schedule.poll(117).drive.due);

  releases = schedule.poll(121);
  CHECK(!releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 120);
  CHECK(releases.drive.sequence == 5);
  CHECK(releases.drive.missed_releases == 1);
  CHECK(releases.steering.due);
  CHECK(releases.steering.scheduled_ms == 120);
  CHECK(releases.steering.sequence == 2);
  CHECK(releases.steering.missed_releases == 0);

  releases = schedule.poll(125);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 125);
  CHECK(releases.drive.sequence == 6);
  CHECK(releases.drive.missed_releases == 0);
  CHECK(!releases.steering.due);

  releases = schedule.poll(141);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 140);
  CHECK(releases.drive.sequence == 9);
  CHECK(releases.drive.missed_releases == 2);
  CHECK(releases.steering.due);
  CHECK(releases.steering.scheduled_ms == 140);
  CHECK(releases.steering.sequence == 3);
  CHECK(releases.steering.missed_releases == 0);
  CHECK(!schedule.poll(141).drive.due);
  CHECK(!schedule.poll(141).steering.due);
}

void absoluteReleaseSchedulePreventsLateCatchupBurst() {
  using namespace csm::board::control;

  ControlReleaseSchedule schedule;
  CHECK(schedule.begin(0, 5, 20));

  auto releases = schedule.poll(0);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 0);

  releases = schedule.poll(9);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 5);
  CHECK(releases.drive.sequence == 2);
  CHECK(releases.drive.missed_releases == 0);

  // The absolute 10 ms deadline is consumed as missed rather than producing
  // a 1 ms catch-up burst after the late 5 ms release.
  releases = schedule.poll(10);
  CHECK(!releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 10);
  CHECK(releases.drive.sequence == 3);
  CHECK(releases.drive.missed_releases == 1);

  releases = schedule.poll(15);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 15);
  CHECK(releases.drive.sequence == 4);
  CHECK(releases.drive.missed_releases == 0);

  // Reconfiguration is a new schedule epoch; prior dispatch spacing cannot
  // suppress its first absolute release.
  CHECK(schedule.begin(16, 5, 20));
  releases = schedule.poll(16);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 16);
  CHECK(releases.drive.sequence == 1);
  CHECK(releases.drive.missed_releases == 0);
}

void absoluteReleaseScheduleSurvivesWraparound() {
  using namespace csm::board::control;

  const uint32_t phase_ms = UINT32_MAX - 3u;
  ControlReleaseSchedule schedule;
  CHECK(schedule.begin(phase_ms, 5, 20));

  auto releases = schedule.poll(phase_ms);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == phase_ms);
  CHECK(releases.steering.due);
  CHECK(releases.steering.scheduled_ms == phase_ms);

  releases = schedule.poll(UINT32_MAX);
  CHECK(!releases.drive.due);
  CHECK(!releases.steering.due);
  releases = schedule.poll(0);
  CHECK(!releases.drive.due);
  CHECK(!releases.steering.due);

  releases = schedule.poll(1);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 1);
  CHECK(releases.drive.sequence == 2);
  CHECK(releases.drive.missed_releases == 0);
  CHECK(!releases.steering.due);

  releases = schedule.poll(16);
  CHECK(releases.drive.due);
  CHECK(releases.drive.scheduled_ms == 16);
  CHECK(releases.drive.sequence == 5);
  CHECK(releases.drive.missed_releases == 2);
  CHECK(releases.steering.due);
  CHECK(releases.steering.scheduled_ms == 16);
  CHECK(releases.steering.sequence == 2);
  CHECK(releases.steering.missed_releases == 0);
  CHECK(!schedule.poll(16).drive.due);
}

void runtimeReleasePhasesSurviveCooperativeLoopGap() {
  using namespace csm::board;

  control::RemoteControlRuntime runtime;
  control::RemoteControlRuntimeConfig config;
  config.configured = true;
  config.local_can_tx_enabled = true;
  config.mapping = control::VehicleCommandMapping::VehicleBench0x005And0x007;
  config.bus = 1;
  config.policy_id = 0x5243;
  config.cycle_period_ms = 5;
  config.steering_period_ms = 20;
  config.frame_gap_ms = 0;
  config.m4_heartbeat_timeout_ms = 100;
  config.neutral_qualification_ms = 0;
  config.release_qualification_ms = 0;
  config.neutral_deadband_permille = 50;
  config.drive_deadband_permille = control::kRemoteDriveDeadbandPermille;
  config.steering_deadband_permille = 20;
  config.auxiliary_threshold_permille = 500;
  config.steering_step_permille = 30;
  config.steering_return_step_permille = 50;
  config.drive_channel_index = 3;
  config.steering_channel_index = 1;
  CHECK(runtime.begin(0, 0xABCD, config));

  control::RemoteControlRuntimeInputs inputs;
  inputs.hard_safety_allows = true;
  inputs.local_tx_inhibit_latched = false;
  inputs.autonomy_state =
      authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.backend_state.ready = true;

  remote::M4RemoteMailboxWriter writer;
  remote::M4RemoteMailboxFrame mailbox;
  remote::RemoteFrontendDiagnostics diagnostics;
  writer.reset();
  writer.clearFrame(&mailbox);
  const uint32_t m4_boot_id = remote::initializeRemoteSharedMemoryForM4();
  uint32_t heartbeat = 0;
  remote::RcSample sample;
  sample.sample_state = remote::RcSampleState::Ok;
  sample.seq = 1;
  auto publish = [&](uint32_t now_ms) {
    sample.m4_time_ms = now_ms;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
  };

  auto drain = [&](uint32_t now_ms, uint32_t* drive_frames,
                   uint32_t* steering_frames) {
    *drive_frames = 0;
    *steering_frames = 0;
    for (uint8_t budget = 0;
         budget < control::kVehicleCommandMapperMaxFrames; ++budget) {
      const auto output = runtime.service(now_ms, inputs);
      if (!output.frame_ready) break;
      const uint32_t can_id = output.frame.can_id_flags & 0x7FFu;
      if (can_id == control::kRemoteDriveCanId) {
        ++*drive_frames;
      } else if (can_id == control::kRemoteSteeringCanId) {
        ++*steering_frames;
      }
      const uint32_t completed_before = runtime.status().can_tx_success;
      runtime.noteCanTxEnqueueResult(now_ms, true);
      CHECK(runtime.status().can_tx_success == completed_before);
      runtime.noteCanTxCompletion(now_ms, true);
      CHECK(runtime.status().can_tx_success == completed_before + 1u);
    }
    CHECK(!runtime.service(now_ms, inputs).frame_ready);
  };

  uint32_t drive_frames = 0;
  uint32_t steering_frames = 0;
  publish(0);
  drain(0, &drive_frames, &steering_frames);
  CHECK(drive_frames == 1);
  CHECK(steering_frames == 1);
  CHECK(runtime.status().handoff_qualified);

  sample.ch[1] = -1000;
  sample.ch[3] = 1000;
  ++sample.seq;
  publish(1);
  CHECK(!runtime.service(1, inputs).frame_ready);
  CHECK(runtime.status().drive_permille == 1000);
  CHECK(runtime.status().steering_permille == -1000);

  inputs.host_output_reserved = true;
  CHECK(!runtime.service(5, inputs).frame_ready);
  inputs.host_output_reserved = false;
  drain(5, &drive_frames, &steering_frames);
  CHECK(drive_frames == 1);
  CHECK(steering_frames == 0);

  // The cooperative loop skipped the 10 ms and 15 ms drive releases. It must
  // execute only the latest 20 ms drive slot and the independently due 20 ms
  // steering slot; it must neither catch up nor shift either absolute phase.
  drain(21, &drive_frames, &steering_frames);
  CHECK(drive_frames == 1);
  CHECK(steering_frames == 1);
  CHECK(runtime.status().drive_release_misses == 2);
  CHECK(runtime.status().steering_release_misses == 0);
  CHECK(runtime.status().cycle_deadline_misses == 3);

  drain(41, &drive_frames, &steering_frames);
  CHECK(drive_frames == 1);
  CHECK(steering_frames == 1);
  CHECK(runtime.status().drive_release_misses == 5);
  CHECK(runtime.status().steering_release_misses == 0);
  CHECK(runtime.status().cycle_deadline_misses == 7);

  // A valid-to-stale edge prepares the exact 0x005 stop in this same service
  // call. It cannot re-anchor the periodic drive lane.
  sample.sample_state = remote::RcSampleState::Stale;
  sample.ch[1] = 0;
  sample.ch[3] = 0;
  ++sample.seq;
  publish(42);
  const auto immediate_stop = runtime.service(42, inputs);
  CHECK(immediate_stop.frame_ready);
  CHECK((immediate_stop.frame.can_id_flags & 0x7FFu) ==
        control::kRemoteDriveCanId);
  CHECK(immediate_stop.frame.dlc == 8);
  const uint8_t expected_stop[8] =
      {0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CHECK(std::memcmp(immediate_stop.frame.data, expected_stop,
                    sizeof(expected_stop)) == 0);
  runtime.noteCanTxEnqueueResult(42, true);
  runtime.noteCanTxCompletion(42, true);
  const auto immediate_steering_stop = runtime.service(42, inputs);
  CHECK(immediate_steering_stop.frame_ready);
  CHECK((immediate_steering_stop.frame.can_id_flags & 0x7FFu) ==
        control::kRemoteSteeringCanId);
  CHECK(immediate_steering_stop.frame.data[0] ==
        control::kRemoteSteeringCenter);
  CHECK(immediate_steering_stop.frame.data[7] == 0);
  runtime.noteCanTxEnqueueResult(42, true);
  runtime.noteCanTxCompletion(42, true);
  CHECK(!runtime.service(42, inputs).frame_ready);

  // The adjacent absolute 45 ms slot is consumed as missed because the urgent
  // stop was prepared at 42 ms. The unshifted 50 ms phase remains live.
  CHECK(!runtime.service(45, inputs).frame_ready);
  CHECK(runtime.status().drive_release_misses == 6);
  CHECK(runtime.status().cycle_deadline_misses == 8);

  // FIFO rejection is a control fault, not a retry hint. It advances no
  // success evidence and inhibits local TX until a deliberate, disarmed
  // Service/HIL retry clears the runtime side of the fault.
  const auto rejected = runtime.service(50, inputs);
  CHECK(rejected.frame_ready);
  const uint32_t completed_before_reject = runtime.status().can_tx_success;
  runtime.noteCanTxEnqueueResult(50, false);
  CHECK(runtime.status().can_tx_success == completed_before_reject);
  CHECK(runtime.status().can_tx_failed == 1);
  CHECK(runtime.status().can_tx_inhibit_latched);
  CHECK(!runtime.service(55, inputs).frame_ready);

  // A terminal hardware outcome is independently counted as failure and
  // never promoted to CAN TX success.
  runtime.noteCanTxCompletion(56, false);
  CHECK(runtime.status().can_tx_success == completed_before_reject);
  CHECK(runtime.status().can_tx_failed == 2);
  CHECK(runtime.status().can_tx_inhibit_latched);
  runtime.clearCanTxInhibitForService(57);
  CHECK(!runtime.status().can_tx_inhibit_latched);
}

void runtimeImmediateStopRespectsSafetyAndWraparound() {
  using namespace csm::board;

  auto configuredRuntime = [](uint32_t phase_ms,
                              control::RemoteControlRuntime* runtime) {
    control::RemoteControlRuntimeConfig config;
    config.configured = true;
    config.local_can_tx_enabled = true;
    config.mapping =
        control::VehicleCommandMapping::VehicleBench0x005And0x007;
    config.bus = 1;
    config.policy_id = 0x5243;
    config.cycle_period_ms = 5;
    config.steering_period_ms = 20;
    config.frame_gap_ms = 0;
    config.m4_heartbeat_timeout_ms = 100;
    config.neutral_qualification_ms = 0;
    config.release_qualification_ms = 0;
    config.neutral_deadband_permille = 50;
    config.drive_deadband_permille = control::kRemoteDriveDeadbandPermille;
    config.steering_deadband_permille = 20;
    config.auxiliary_threshold_permille = 500;
    config.steering_step_permille = 30;
    config.steering_return_step_permille = 50;
    CHECK(runtime->begin(phase_ms, 0xDCBA, config));
  };
  auto safeInputs = [] {
    control::RemoteControlRuntimeInputs inputs;
    inputs.hard_safety_allows = true;
    inputs.local_tx_inhibit_latched = false;
    inputs.autonomy_state =
        authority::AutonomyAuthorityState::InactiveConfirmed;
    inputs.backend_state.ready = true;
    return inputs;
  };
  auto exactStop = [](const control::RemoteControlRuntimeOutput& output) {
    const uint8_t expected[8] =
        {0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return output.frame_ready &&
        (output.frame.can_id_flags & 0x7FFu) == control::kRemoteDriveCanId &&
        output.frame.dlc == 8 &&
        std::memcmp(output.frame.data, expected, sizeof(expected)) == 0;
  };

  // A blocked safety boundary must produce no CAN frame. The pending urgent
  // stop is retained and becomes ready immediately when the same boundary is
  // permitted, without waiting for another periodic phase.
  {
    control::RemoteControlRuntime runtime;
    configuredRuntime(0, &runtime);
    auto inputs = safeInputs();
    remote::M4RemoteMailboxWriter writer;
    remote::M4RemoteMailboxFrame mailbox;
    remote::RemoteFrontendDiagnostics diagnostics;
    writer.reset();
    writer.clearFrame(&mailbox);
    const uint32_t m4_boot_id = remote::initializeRemoteSharedMemoryForM4();
    uint32_t heartbeat = 0;
    remote::RcSample sample;
    sample.sample_state = remote::RcSampleState::Ok;
    sample.seq = 1;
    sample.m4_time_ms = 0;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
    CHECK(exactStop(runtime.service(0, inputs)));
    runtime.noteCanTxEnqueueResult(0, true);
    const auto initial_steering_stop = runtime.service(0, inputs);
    CHECK(initial_steering_stop.frame_ready);
    CHECK((initial_steering_stop.frame.can_id_flags & 0x7FFu) ==
          control::kRemoteSteeringCanId);
    runtime.noteCanTxEnqueueResult(0, true);

    sample.sample_state = remote::RcSampleState::Stale;
    ++sample.seq;
    sample.m4_time_ms = 42;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
    inputs.hard_safety_allows = false;
    CHECK(!runtime.service(42, inputs).frame_ready);
    inputs.hard_safety_allows = true;
    CHECK(exactStop(runtime.service(42, inputs)));
  }

  // Ready timestamps and spacing comparisons remain correct across uint32
  // wrap. The urgent stop at UINT32_MAX is ready in that same service call.
  {
    const uint32_t phase_ms = UINT32_MAX - 3u;
    control::RemoteControlRuntime runtime;
    configuredRuntime(phase_ms, &runtime);
    auto inputs = safeInputs();
    remote::M4RemoteMailboxWriter writer;
    remote::M4RemoteMailboxFrame mailbox;
    remote::RemoteFrontendDiagnostics diagnostics;
    writer.reset();
    writer.clearFrame(&mailbox);
    const uint32_t m4_boot_id = remote::initializeRemoteSharedMemoryForM4();
    uint32_t heartbeat = 0;
    remote::RcSample sample;
    sample.sample_state = remote::RcSampleState::Ok;
    sample.seq = 1;
    sample.m4_time_ms = phase_ms;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
    CHECK(exactStop(runtime.service(phase_ms, inputs)));
    runtime.noteCanTxEnqueueResult(phase_ms, true);
    const auto initial_steering_stop = runtime.service(phase_ms, inputs);
    CHECK(initial_steering_stop.frame_ready);
    CHECK((initial_steering_stop.frame.can_id_flags & 0x7FFu) ==
          control::kRemoteSteeringCanId);
    runtime.noteCanTxEnqueueResult(phase_ms, true);

    sample.sample_state = remote::RcSampleState::Stale;
    ++sample.seq;
    sample.m4_time_ms = UINT32_MAX;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
    CHECK(exactStop(runtime.service(UINT32_MAX, inputs)));
    runtime.noteCanTxEnqueueResult(UINT32_MAX, true);
    const auto wrapped_steering_stop = runtime.service(UINT32_MAX, inputs);
    CHECK(wrapped_steering_stop.frame_ready);
    CHECK((wrapped_steering_stop.frame.can_id_flags & 0x7FFu) ==
          control::kRemoteSteeringCanId);
    runtime.noteCanTxEnqueueResult(UINT32_MAX, true);
    CHECK(!runtime.service(1, inputs).frame_ready);
    CHECK(exactStop(runtime.service(6, inputs)));
  }
}

void runtimeHandoffLossAndFaultPolicy() {
  using namespace csm::board;
  control::RemoteControlRuntime runtime;
  control::RemoteControlRuntimeConfig config;
  config.configured = true;
  config.local_can_tx_enabled = true;
  config.mapping = control::VehicleCommandMapping::VehicleBench0x005And0x007;
  config.bus = 1;
  config.policy_id = 0x5243;
  config.cycle_period_ms = 5;
  config.steering_period_ms = 20;
  config.frame_gap_ms = 0;
  config.m4_heartbeat_timeout_ms = 100;
  config.neutral_qualification_ms = 500;
  config.release_qualification_ms = 1000;
  config.neutral_deadband_permille = 50;
  config.drive_deadband_permille = control::kRemoteDriveDeadbandPermille;
  config.steering_deadband_permille = 20;
  config.auxiliary_threshold_permille = 500;
  config.steering_step_permille = 30;
  config.steering_return_step_permille = 50;
  config.max_forward_rpm = 500;
  config.max_reverse_rpm = 500;
  config.max_steering_deci_degree = 450;
  CHECK(runtime.begin(0, 0x1234, config));

  control::RemoteControlRuntimeInputs inputs;
  inputs.hard_safety_allows = true;
  inputs.local_tx_inhibit_latched = false;
  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.backend_state.ready = true;

  remote::M4RemoteMailboxWriter writer;
  remote::M4RemoteMailboxFrame mailbox;
  remote::RemoteFrontendDiagnostics diagnostics;
  writer.reset();
  writer.clearFrame(&mailbox);
  const uint32_t m4_boot_id = remote::initializeRemoteSharedMemoryForM4();
  uint32_t heartbeat = 0;

  remote::RcSample sample;
  sample.sample_state = remote::RcSampleState::Ok;
  sample.seq = 1;
  auto publish = [&](uint32_t now_ms) {
    sample.m4_time_ms = now_ms;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
  };

  control::CanFrameRequest first_motion_frame;
  bool saw_first_motion_frame = false;
  bool drive_period_ok = true;
  bool steering_period_ok = true;
  bool has_previous_drive = false;
  bool has_previous_steering = false;
  uint32_t previous_drive_ms = 0;
  uint32_t previous_steering_ms = 0;
  auto serviceRange = [&](uint32_t begin_ms, uint32_t end_ms,
                          bool refresh_frontend) {
    uint32_t emitted_frames = 0;
    for (uint32_t now_ms = begin_ms; now_ms <= end_ms; ++now_ms) {
      if (refresh_frontend && (now_ms % 20u) == 0u) publish(now_ms);
      bool drive_emitted_this_tick = false;
      for (uint8_t frame_budget = 0;
           frame_budget < control::kVehicleCommandMapperMaxFrames;
           ++frame_budget) {
        const auto output = runtime.service(now_ms, inputs);
        if (!output.frame_ready) break;
        ++emitted_frames;
        const uint32_t can_id = output.frame.can_id_flags & 0x7FFu;
        if (can_id == control::kRemoteDriveCanId) {
          if (has_previous_drive && now_ms - previous_drive_ms != 5u) {
            drive_period_ok = false;
          }
          previous_drive_ms = now_ms;
          has_previous_drive = true;
          drive_emitted_this_tick = true;
        } else if (can_id == control::kRemoteSteeringCanId) {
          CHECK(drive_emitted_this_tick);
          if (has_previous_steering && now_ms - previous_steering_ms != 20u) {
            steering_period_ok = false;
          }
          previous_steering_ms = now_ms;
          has_previous_steering = true;
        }
        if (!saw_first_motion_frame &&
            output.frame.can_id_flags == control::kRemoteSteeringCanId &&
            output.frame.data[0] != control::kRemoteSteeringCenter) {
          first_motion_frame = output.frame;
          saw_first_motion_frame = true;
        }
        const uint32_t completed_before = runtime.status().can_tx_success;
        runtime.noteCanTxEnqueueResult(now_ms, true);
        CHECK(runtime.status().can_tx_success == completed_before);
        runtime.noteCanTxCompletion(now_ms, true);
        CHECK(runtime.status().can_tx_success == completed_before + 1u);
      }
      CHECK(!runtime.service(now_ms, inputs).frame_ready);
    }
    return emitted_frames;
  };

  // Until RC is qualified, drive neutral is released at 200 Hz and steering
  // neutral at its independent 50 Hz phase.
  CHECK(serviceRange(0, 499, true) == 125);
  CHECK(serviceRange(500, 500, true) == 2);
  CHECK(runtime.status().frontend_alive);
  CHECK(runtime.status().remote_reserved);
  CHECK(runtime.status().remote_valid);
  CHECK(runtime.status().handoff_qualified);
  CHECK(runtime.status().active_source == authority::ControlSourceId::Remote);
  CHECK(runtime.status().neutral_cycles >= 50);

  sample.ch[1] = 1000;
  sample.ch[3] = -1000;
  ++sample.seq;
  serviceRange(501, 540, true);
  CHECK(saw_first_motion_frame);
  CHECK(first_motion_frame.can_id_flags == control::kRemoteSteeringCanId);
  CHECK(first_motion_frame.data[0] < control::kRemoteSteeringCenter);
  CHECK(first_motion_frame.data[0] > control::kRemoteSteeringMinimum);
  CHECK(first_motion_frame.data[7] == 0);
  CHECK(drive_period_ok);
  CHECK(steering_period_ok);
  CHECK(runtime.status().cycle_deadline_misses == 0);
  CHECK(runtime.status().control_cycles > 0);

  // Upstream autonomy states fail closed even with a fresh, qualified RC
  // source and motion command. No neutral fallback may be synthesized.
  const authority::AutonomyAuthorityState blocked_states[] = {
      authority::AutonomyAuthorityState::Unknown,
      authority::AutonomyAuthorityState::ActiveConfirmed,
      authority::AutonomyAuthorityState::RecentlyActive,
      authority::AutonomyAuthorityState::ProtocolFault,
  };
  uint32_t blocked_begin_ms = 541;
  for (const auto state : blocked_states) {
    has_previous_drive = false;
    has_previous_steering = false;
    inputs.autonomy_state = state;
    CHECK(serviceRange(blocked_begin_ms, blocked_begin_ms + 39u, true) == 0);
    blocked_begin_ms += 40u;
  }

  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.local_tx_inhibit_latched = true;
  CHECK(serviceRange(701, 740, true) == 0);
  inputs.local_tx_inhibit_latched = false;
  has_previous_drive = false;
  has_previous_steering = false;
  CHECK(serviceRange(741, 780, true) > 0);

  sample.sample_state = remote::RcSampleState::Stale;
  sample.ch[1] = 0;
  sample.ch[3] = 0;
  ++sample.seq;
  const uint32_t tx_before_link_loss = runtime.status().can_tx_success;
  CHECK(serviceRange(781, 800, true) >= 2);
  CHECK(!runtime.status().remote_valid);
  CHECK(runtime.status().remote_reserved);
  CHECK(!runtime.status().release_qualified);
  CHECK(runtime.status().neutral_cycles > 50);
  CHECK(runtime.status().can_tx_success > tx_before_link_loss);

  CHECK(serviceRange(801, 1800, true) >= 99);
  CHECK(runtime.status().release_qualified);
  CHECK(!runtime.status().remote_reserved);
  CHECK(runtime.status().host_control_allowed);

  sample.sample_state = remote::RcSampleState::ProtocolFault;
  ++sample.seq;
  CHECK(serviceRange(1801, 2940, true) >= 113);
  CHECK(!runtime.status().release_qualified);
  CHECK(runtime.status().remote_reserved);
  CHECK(!runtime.status().host_control_allowed);

  CHECK(serviceRange(2941, 3060, false) >= 11);
  CHECK(!runtime.status().frontend_alive);
  CHECK(runtime.status().remote_reserved);
  CHECK(runtime.status().ipc_rejects == 0);
  CHECK(runtime.status().last_ipc_reject_detail == 0);
  CHECK(runtime.status().cycle_deadline_misses == 0);
}

}  // namespace

int main() {
  builtinCanTxOwnerHasExplicitEnqueueOutcome();
  builtinCanTxOriginAccountingSeparatesRcFromHost();
  builtinCanTxOwnerUsesThreeRealSlotsWithoutHiddenRetry();
  hostFreshnessRequiresCoherentAnchorAndRejectsOldWork();
  hostFreshnessLatchesHeartbeatTimelineFaultUntilReset();
  exploratoryFreshnessMeasuresWithoutInventingThresholds();
  hostAuthorityGateHasNoRcOverlapOrArtificialDeadZone();
  builtinCanCancellationTargetsOriginWithoutErasingTruth();
  hostDownlinkParserPreservesCoalescedBurstOverBufferSize();
  builtinCanTxJournalCoversAllTerminalPaths();
  builtinCanInvalidSnapshotHoldsJournalFailClosed();
  builtinCanDuplicateMaskLatchesTrackingFault();
  builtinCanCancelFailureAndIdentityWrapFailClosed();
  hostTransportEpochInvalidatesHeartbeatAndLease();
  crsfChannelsDecodeAndNormalize();
  upstreamAutonomyPrecedesRemoteReservation();
  frozenMailboxCannotRemainFresh();
  drivePayloadMatchesVehicleBenchGoldenFrames();
  remotePreemptsAutonomyAndMapsCh4Ch5Ch10Ch11();
  absoluteReleaseSchedulePreservesPhaseAndCountsMisses();
  absoluteReleaseSchedulePreventsLateCatchupBurst();
  absoluteReleaseScheduleSurvivesWraparound();
  runtimeReleasePhasesSurviveCooperativeLoopGap();
  runtimeImmediateStopRespectsSafetyAndWraparound();
  runtimeHandoffLossAndFaultPolicy();
  if (failures != 0) {
    std::fprintf(stderr, "%d remote control contract checks failed\n", failures);
    return 1;
  }
  std::puts("remote control contract checks passed");
  return 0;
}
