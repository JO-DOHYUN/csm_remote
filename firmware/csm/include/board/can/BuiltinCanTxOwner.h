#pragma once

#include <stdint.h>

#include "board/can/CanTypes.h"

namespace csm::board::can {

enum class BuiltinCanTxOrigin : uint8_t {
  Unspecified = 0,
  RemoteControl = 1,
  HostControl = 2,
  BuiltinTest = 3,
};

struct BuiltinCanTxFrame {
  uint32_t command_id = 0;
  uint8_t bus = 0xFFu;
  uint32_t can_id_flags = 0;
  uint8_t dlc = 0;
  uint8_t data[8] = {};
  BuiltinCanTxOrigin origin = BuiltinCanTxOrigin::Unspecified;
};

enum class BuiltinCanTxOutcomeCode : uint8_t {
  RejectedNotConfigured = 0,
  RejectedBackend = 1,
  RejectedBus = 2,
  RejectedFrame = 3,
  RejectedJournalFull = 4,
  RejectedTrackingFault = 5,
  DriverRejected = 6,
  FifoEnqueueTracked = 7,
  FifoEnqueueUntracked = 8,
};

enum class BuiltinCanTxDisposition : uint8_t {
  TerminalRejected = 0,
  TransientRejected = 1,
  Accepted = 2,
};

struct BuiltinCanTxOutcome {
  BuiltinCanTxOutcomeCode code =
      BuiltinCanTxOutcomeCode::RejectedNotConfigured;
  uint32_t submission_sequence = 0;
  int32_t driver_result = 0;
  bool driver_called = false;
  bool fifo_enqueue_accepted = false;
  bool completion_tracked = false;
  BuiltinCanTxDisposition disposition =
      BuiltinCanTxDisposition::TerminalRejected;

  bool fifoEnqueueAccepted() const { return fifo_enqueue_accepted; }
  bool transientAdmissionFailure() const {
    return disposition == BuiltinCanTxDisposition::TransientRejected;
  }
  bool terminalFailure() const {
    return disposition == BuiltinCanTxDisposition::TerminalRejected;
  }
};

struct BuiltinCanTxOwnerCounters {
  uint32_t submissions = 0;
  uint32_t contract_rejects = 0;
  uint32_t backend_rejects = 0;
  uint32_t driver_rejects = 0;
  uint32_t fifo_enqueue_accepts = 0;
  uint32_t journal_full_rejects = 0;
  uint32_t tracking_fault_rejects = 0;
  uint32_t invalid_request_masks = 0;
  uint32_t cancel_requests = 0;
  uint32_t cancel_request_accepts = 0;
  uint32_t cancel_request_failures = 0;
  uint32_t tx_completed = 0;
  uint32_t tx_cancelled = 0;
  uint32_t tx_deadline_exceeded = 0;
  uint32_t tx_disappeared = 0;
  uint32_t tx_ambiguous = 0;
  uint32_t snapshot_invalid_polls = 0;
};

struct BuiltinCanTxDriverResult {
  int32_t driver_result = 0;
  uint32_t request_mask = 0;
  uint32_t driver_sequence = 0;
  uint32_t write_duration_us = 0;
};

using BuiltinCanTxWriteFn =
    BuiltinCanTxDriverResult (*)(void* context,
                                 const BuiltinCanTxFrame& frame,
                                 uint32_t submission_sequence);

struct BuiltinCanTxCancelResult {
  int32_t driver_result = 0;
  bool request_accepted = false;
};

using BuiltinCanTxCancelFn =
    BuiltinCanTxCancelResult (*)(void* context, uint32_t request_mask);

enum class BuiltinCanTxCompletionCode : uint8_t {
  Transmitted = 0,
  Cancelled = 1,
  DeadlineExceededPending = 2,
  DisappearedWithoutOutcome = 3,
  InvalidRequestMask = 4,
  AmbiguousHardwareStatus = 5,
  TrackingCompromised = 6,
  CancelRequestFailed = 7,
};

struct BuiltinCanTxCompletion {
  BuiltinCanTxCompletionCode code =
      BuiltinCanTxCompletionCode::DisappearedWithoutOutcome;
  BuiltinCanTxFrame frame = {};
  uint32_t submission_sequence = 0;
  uint32_t driver_sequence = 0;
  uint32_t request_mask = 0;
  uint32_t terminal_us = 0;
  int32_t driver_result = 0;
  int32_t cancel_driver_result = 0;
  uint32_t write_duration_us = 0;
  bool terminal = true;
  bool deadline_previously_reported = false;
  bool failure_previously_reported = false;

  bool transmitted() const {
    return code == BuiltinCanTxCompletionCode::Transmitted;
  }
};

using BuiltinCanTxCompletionFn =
    void (*)(void* context, const BuiltinCanTxCompletion& completion);

// This is the sole software submission point for the built-in CAN TX FIFO.
// A positive driver result is intentionally named FIFO enqueue acceptance; it
// is not hardware-on-bus completion evidence.
class BuiltinCanTxOwner {
 public:
  static constexpr uint8_t kJournalSlots = 3;

  bool begin(uint8_t owned_bus, BuiltinCanTxWriteFn write_fn,
             void* write_context, BuiltinCanTxCancelFn cancel_fn,
             void* cancel_context, uint32_t completion_timeout_us,
             BuiltinCanTxCompletionFn completion_fn,
             void* completion_context);
  BuiltinCanTxOutcome submit(const BuiltinCanTxFrame& frame,
                             const CanBackendState& backend,
                             uint32_t now_us);
  void serviceCompletions(uint32_t now_us, bool snapshot_valid,
                          uint32_t txbrp, uint32_t txbto, uint32_t txbcf);

  bool configured() const { return configured_; }
  uint8_t ownedBus() const { return owned_bus_; }
  uint8_t activeJournalSlots() const;
  uint8_t activeJournalSlots(BuiltinCanTxOrigin origin) const;
  bool trackingFaultLatched() const { return tracking_fault_latched_; }
  const BuiltinCanTxOwnerCounters& counters() const { return counters_; }
#if defined(CSM_REMOTE_SHARED_MEMORY_TEST)
  void setSubmissionSequenceForTest(uint32_t value) {
    submission_sequence_ = value;
  }
#endif

 private:
  struct JournalSlot {
    bool active = false;
    uint32_t submission_sequence = 0;
    uint32_t driver_sequence = 0;
    uint32_t request_mask = 0;
    uint32_t deadline_us = 0;
    int32_t driver_result = 0;
    int32_t cancel_driver_result = 0;
    uint32_t write_duration_us = 0;
    bool deadline_reported = false;
    bool failure_reported = false;
    bool cancel_requested = false;
    bool identity_compromised = false;
    BuiltinCanTxFrame frame = {};
  };

  static bool validFrame(const BuiltinCanTxFrame& frame);
  static bool validRequestMask(uint32_t request_mask);
  static bool timeReached(uint32_t now_us, uint32_t deadline_us);
  static void increment(uint32_t* value);
  uint32_t nextSubmissionSequence();
  JournalSlot* freeJournalSlot();
  bool requestMaskAlreadyTracked(uint32_t request_mask) const;
  void complete(JournalSlot* slot, BuiltinCanTxCompletionCode code,
                uint32_t now_us);
  void reportIntermediate(JournalSlot* slot,
                          BuiltinCanTxCompletionCode code,
                          uint32_t now_us);
  void reportUntracked(const BuiltinCanTxFrame& frame,
                       uint32_t submission_sequence,
                       uint32_t request_mask,
                       BuiltinCanTxCompletionCode code,
                       uint32_t now_us, bool terminal,
                       bool deadline_previously_reported,
                       bool failure_previously_reported,
                       uint32_t driver_sequence,
                       int32_t driver_result,
                       int32_t cancel_driver_result,
                       uint32_t write_duration_us);
  void requestCancellation(JournalSlot* slot, uint32_t now_us);
  void latchTrackingFault(uint32_t now_us, bool compromise_active_slots);

  uint8_t owned_bus_ = 0xFFu;
  BuiltinCanTxWriteFn write_fn_ = nullptr;
  void* write_context_ = nullptr;
  BuiltinCanTxCancelFn cancel_fn_ = nullptr;
  void* cancel_context_ = nullptr;
  uint32_t completion_timeout_us_ = 0;
  BuiltinCanTxCompletionFn completion_fn_ = nullptr;
  void* completion_context_ = nullptr;
  BuiltinCanTxOwnerCounters counters_ = {};
  JournalSlot journal_[kJournalSlots] = {};
  bool tracking_fault_latched_ = false;
  bool configured_ = false;
  uint32_t submission_sequence_ = 0;
};

}  // namespace csm::board::can
