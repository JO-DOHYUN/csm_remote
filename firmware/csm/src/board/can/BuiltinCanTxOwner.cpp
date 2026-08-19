#include "board/can/BuiltinCanTxOwner.h"

#include <limits.h>

namespace csm::board::can {
namespace {

constexpr uint32_t kExtendedFlag = 1u << 29;
constexpr uint32_t kSupportedFlags = kExtendedFlag;
constexpr uint32_t kHardwareTxSlotMask = 0x07u;

BuiltinCanTxOutcome makeOutcome(BuiltinCanTxOutcomeCode code,
                                uint32_t submission_sequence,
                                BuiltinCanTxDisposition disposition,
                                bool driver_called = false,
                                int32_t driver_result = 0,
                                bool fifo_enqueue_accepted = false,
                                bool completion_tracked = false) {
  BuiltinCanTxOutcome outcome;
  outcome.code = code;
  outcome.submission_sequence = submission_sequence;
  outcome.driver_called = driver_called;
  outcome.driver_result = driver_result;
  outcome.fifo_enqueue_accepted = fifo_enqueue_accepted;
  outcome.completion_tracked = completion_tracked;
  outcome.disposition = disposition;
  return outcome;
}

}  // namespace

bool BuiltinCanTxOwner::begin(uint8_t owned_bus,
                              BuiltinCanTxWriteFn write_fn,
                              void* write_context,
                              BuiltinCanTxCancelFn cancel_fn,
                              void* cancel_context,
                              uint32_t completion_timeout_us,
                              BuiltinCanTxCompletionFn completion_fn,
                              void* completion_context) {
  owned_bus_ = owned_bus;
  write_fn_ = write_fn;
  write_context_ = write_context;
  cancel_fn_ = cancel_fn;
  cancel_context_ = cancel_context;
  completion_timeout_us_ = completion_timeout_us;
  completion_fn_ = completion_fn;
  completion_context_ = completion_context;
  counters_ = {};
  for (JournalSlot& slot : journal_) slot = {};
  tracking_fault_latched_ = false;
  submission_sequence_ = 0;
  configured_ = owned_bus != 0xFFu && write_fn != nullptr &&
                 cancel_fn != nullptr &&
                 completion_timeout_us > 0 &&
                completion_timeout_us < 0x80000000u &&
                completion_fn != nullptr;
  if (!configured_) {
    owned_bus_ = 0xFFu;
    write_fn_ = nullptr;
    write_context_ = nullptr;
    cancel_fn_ = nullptr;
    cancel_context_ = nullptr;
    completion_timeout_us_ = 0;
    completion_fn_ = nullptr;
    completion_context_ = nullptr;
  }
  return configured_;
}

BuiltinCanTxOutcome BuiltinCanTxOwner::submit(
    const BuiltinCanTxFrame& frame, const CanBackendState& backend,
    uint32_t now_us) {
  increment(&counters_.submissions);
  const uint32_t sequence = nextSubmissionSequence();
  if (!configured_) {
    increment(&counters_.contract_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedNotConfigured,
                       sequence, BuiltinCanTxDisposition::TerminalRejected);
  }
  if (tracking_fault_latched_) {
    increment(&counters_.tracking_fault_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedTrackingFault,
                       sequence, BuiltinCanTxDisposition::TerminalRejected);
  }
  if (!backend.ready || backend.bus_off || backend.error_passive) {
    increment(&counters_.backend_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedBackend, sequence,
                       BuiltinCanTxDisposition::TerminalRejected);
  }
  if (backend.tx_busy) {
    increment(&counters_.backend_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedBackend, sequence,
                       BuiltinCanTxDisposition::TransientRejected);
  }
  if (frame.bus != owned_bus_) {
    increment(&counters_.contract_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedBus, sequence,
                       BuiltinCanTxDisposition::TerminalRejected);
  }
  if (!validFrame(frame)) {
    increment(&counters_.contract_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedFrame, sequence,
                       BuiltinCanTxDisposition::TerminalRejected);
  }
  JournalSlot* const journal_slot = freeJournalSlot();
  if (journal_slot == nullptr) {
    increment(&counters_.journal_full_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::RejectedJournalFull, sequence,
                       BuiltinCanTxDisposition::TransientRejected);
  }

  const BuiltinCanTxDriverResult write_result =
      write_fn_(write_context_, frame, sequence);
  if (write_result.driver_result <= 0) {
    increment(&counters_.driver_rejects);
    return makeOutcome(BuiltinCanTxOutcomeCode::DriverRejected, sequence,
                       BuiltinCanTxDisposition::TransientRejected, true,
                       write_result.driver_result);
  }
  increment(&counters_.fifo_enqueue_accepts);
  if (!validRequestMask(write_result.request_mask) ||
      requestMaskAlreadyTracked(write_result.request_mask) ||
      write_result.driver_sequence != sequence) {
    increment(&counters_.invalid_request_masks);
    latchTrackingFault(now_us, true);
    reportUntracked(frame, sequence, write_result.request_mask,
                    BuiltinCanTxCompletionCode::InvalidRequestMask, now_us,
                    true, false, false, write_result.driver_sequence,
                    write_result.driver_result, 0,
                    write_result.write_duration_us);
    return makeOutcome(BuiltinCanTxOutcomeCode::FifoEnqueueUntracked, sequence,
                       BuiltinCanTxDisposition::TerminalRejected, true,
                       write_result.driver_result, true, false);
  }

  journal_slot->active = true;
  journal_slot->submission_sequence = sequence;
  journal_slot->driver_sequence = write_result.driver_sequence;
  journal_slot->request_mask = write_result.request_mask;
  journal_slot->deadline_us = now_us + completion_timeout_us_;
  journal_slot->driver_result = write_result.driver_result;
  journal_slot->write_duration_us = write_result.write_duration_us;
  journal_slot->deadline_reported = false;
  journal_slot->failure_reported = false;
  journal_slot->cancel_requested = false;
  journal_slot->identity_compromised = false;
  journal_slot->frame = frame;
  return makeOutcome(BuiltinCanTxOutcomeCode::FifoEnqueueTracked, sequence,
                     BuiltinCanTxDisposition::Accepted, true,
                     write_result.driver_result, true, true);
}

void BuiltinCanTxOwner::serviceCompletions(
    uint32_t now_us, bool snapshot_valid, uint32_t txbrp,
    uint32_t txbto, uint32_t txbcf) {
  if (!snapshot_valid) {
    increment(&counters_.snapshot_invalid_polls);
    if (activeJournalSlots() != 0) {
      latchTrackingFault(now_us, true);
    }
    return;
  }
  for (JournalSlot& slot : journal_) {
    if (!slot.active) continue;
    const bool transmitted = (txbto & slot.request_mask) != 0;
    const bool cancelled = (txbcf & slot.request_mask) != 0;
    const bool pending = (txbrp & slot.request_mask) != 0;
    if (slot.identity_compromised) {
      if (transmitted || cancelled || !pending) {
        complete(&slot, BuiltinCanTxCompletionCode::TrackingCompromised,
                 now_us);
      } else {
        requestSlotCancellation(&slot, now_us);
      }
    } else if (transmitted) {
      // M_CAN defines TXBTO+TXBCF as a successful transmission in spite of a
      // cancellation request. TXBTO is therefore authoritative and wins.
      complete(&slot, BuiltinCanTxCompletionCode::Transmitted, now_us);
    } else if (cancelled) {
      complete(&slot, BuiltinCanTxCompletionCode::Cancelled, now_us);
    } else if (timeReached(now_us, slot.deadline_us)) {
      if (pending) {
        if (!slot.deadline_reported) {
          reportIntermediate(
              &slot, BuiltinCanTxCompletionCode::DeadlineExceededPending,
              now_us);
        }
        requestSlotCancellation(&slot, now_us);
      } else {
        complete(&slot,
                  BuiltinCanTxCompletionCode::DisappearedWithoutOutcome,
                  now_us);
        latchTrackingFault(now_us, true);
      }
    }
  }
}

uint8_t BuiltinCanTxOwner::activeJournalSlots() const {
  uint8_t count = 0;
  for (const JournalSlot& slot : journal_) {
    if (slot.active) ++count;
  }
  return count;
}

uint8_t BuiltinCanTxOwner::activeJournalSlots(
    BuiltinCanTxOrigin origin) const {
  uint8_t count = 0;
  for (const JournalSlot& slot : journal_) {
    if (slot.active && slot.frame.origin == origin) ++count;
  }
  return count;
}

void BuiltinCanTxOwner::requestCancellation(
    BuiltinCanTxOrigin origin, uint32_t now_us) {
  for (JournalSlot& slot : journal_) {
    if (slot.active && slot.frame.origin == origin) {
      requestSlotCancellation(&slot, now_us);
    }
  }
}

void BuiltinCanTxOwner::requestCancellationAll(uint32_t now_us) {
  for (JournalSlot& slot : journal_) {
    if (slot.active) requestSlotCancellation(&slot, now_us);
  }
}

bool BuiltinCanTxOwner::validRequestMask(uint32_t request_mask) {
  return request_mask != 0 &&
         (request_mask & ~kHardwareTxSlotMask) == 0 &&
         (request_mask & (request_mask - 1u)) == 0;
}

bool BuiltinCanTxOwner::timeReached(uint32_t now_us,
                                    uint32_t deadline_us) {
  return (now_us - deadline_us) < 0x80000000u;
}

BuiltinCanTxOwner::JournalSlot* BuiltinCanTxOwner::freeJournalSlot() {
  for (JournalSlot& slot : journal_) {
    if (!slot.active) return &slot;
  }
  return nullptr;
}

bool BuiltinCanTxOwner::requestMaskAlreadyTracked(
    uint32_t request_mask) const {
  for (const JournalSlot& slot : journal_) {
    if (slot.active && slot.request_mask == request_mask) return true;
  }
  return false;
}

void BuiltinCanTxOwner::complete(JournalSlot* slot,
                                 BuiltinCanTxCompletionCode code,
                                 uint32_t now_us) {
  if (slot == nullptr || !slot->active) return;
  switch (code) {
    case BuiltinCanTxCompletionCode::Transmitted:
      increment(&counters_.tx_completed);
      break;
    case BuiltinCanTxCompletionCode::Cancelled:
      increment(&counters_.tx_cancelled);
      break;
    case BuiltinCanTxCompletionCode::DeadlineExceededPending:
      break;
    case BuiltinCanTxCompletionCode::DisappearedWithoutOutcome:
      increment(&counters_.tx_disappeared);
      break;
    case BuiltinCanTxCompletionCode::AmbiguousHardwareStatus:
    case BuiltinCanTxCompletionCode::TrackingCompromised:
    case BuiltinCanTxCompletionCode::CancelRequestFailed:
      increment(&counters_.tx_ambiguous);
      break;
    case BuiltinCanTxCompletionCode::InvalidRequestMask:
      break;
  }
  const BuiltinCanTxFrame frame = slot->frame;
  const uint32_t sequence = slot->submission_sequence;
  const uint32_t driver_sequence = slot->driver_sequence;
  const uint32_t request_mask = slot->request_mask;
  const int32_t driver_result = slot->driver_result;
  const int32_t cancel_driver_result = slot->cancel_driver_result;
  const uint32_t write_duration_us = slot->write_duration_us;
  const bool deadline_reported = slot->deadline_reported;
  const bool failure_reported = slot->failure_reported;
  *slot = {};
  if (completion_fn_ != nullptr) {
    BuiltinCanTxCompletion completion;
    completion.code = code;
    completion.frame = frame;
    completion.submission_sequence = sequence;
    completion.driver_sequence = driver_sequence;
    completion.request_mask = request_mask;
    completion.terminal_us = now_us;
    completion.driver_result = driver_result;
    completion.cancel_driver_result = cancel_driver_result;
    completion.write_duration_us = write_duration_us;
    completion.terminal = true;
    completion.deadline_previously_reported = deadline_reported;
    completion.failure_previously_reported = failure_reported;
    completion_fn_(completion_context_, completion);
  }
}

void BuiltinCanTxOwner::reportIntermediate(
    JournalSlot* slot, BuiltinCanTxCompletionCode code, uint32_t now_us) {
  if (slot == nullptr || !slot->active || slot->deadline_reported) return;
  increment(&counters_.tx_deadline_exceeded);
  reportUntracked(slot->frame, slot->submission_sequence, slot->request_mask,
                  code, now_us, false, slot->deadline_reported,
                  slot->failure_reported,
                  slot->driver_sequence, slot->driver_result,
                  slot->cancel_driver_result, slot->write_duration_us);
  slot->deadline_reported = true;
  slot->failure_reported = true;
}

void BuiltinCanTxOwner::reportUntracked(
    const BuiltinCanTxFrame& frame, uint32_t submission_sequence,
    uint32_t request_mask, BuiltinCanTxCompletionCode code,
    uint32_t now_us, bool terminal,
    bool deadline_previously_reported,
    bool failure_previously_reported, uint32_t driver_sequence,
    int32_t driver_result, int32_t cancel_driver_result,
    uint32_t write_duration_us) {
  if (completion_fn_ == nullptr) return;
  BuiltinCanTxCompletion completion;
  completion.code = code;
  completion.frame = frame;
  completion.submission_sequence = submission_sequence;
  completion.driver_sequence = driver_sequence;
  completion.request_mask = request_mask;
  completion.terminal_us = now_us;
  completion.driver_result = driver_result;
  completion.cancel_driver_result = cancel_driver_result;
  completion.write_duration_us = write_duration_us;
  completion.terminal = terminal;
  completion.deadline_previously_reported =
      deadline_previously_reported;
  completion.failure_previously_reported =
      failure_previously_reported;
  completion_fn_(completion_context_, completion);
}

void BuiltinCanTxOwner::latchTrackingFault(
    uint32_t now_us, bool compromise_active_slots) {
  tracking_fault_latched_ = true;
  if (compromise_active_slots) {
    for (JournalSlot& slot : journal_) {
      if (slot.active && !slot.identity_compromised) {
        slot.identity_compromised = true;
        reportUntracked(
            slot.frame, slot.submission_sequence, slot.request_mask,
            BuiltinCanTxCompletionCode::TrackingCompromised, now_us, false,
            slot.deadline_reported, slot.failure_reported,
            slot.driver_sequence, slot.driver_result,
            slot.cancel_driver_result, slot.write_duration_us);
        slot.failure_reported = true;
      }
    }
  }
}

void BuiltinCanTxOwner::requestSlotCancellation(JournalSlot* slot,
                                                 uint32_t now_us) {
  if (slot == nullptr || !slot->active || slot->cancel_requested) return;
  slot->cancel_requested = true;
  increment(&counters_.cancel_requests);
  const BuiltinCanTxCancelResult cancel_result =
      cancel_fn_(cancel_context_, slot->request_mask);
  slot->cancel_driver_result = cancel_result.driver_result;
  if (cancel_result.request_accepted) {
    increment(&counters_.cancel_request_accepts);
    return;
  }

  increment(&counters_.cancel_request_failures);
  reportUntracked(
      slot->frame, slot->submission_sequence, slot->request_mask,
      BuiltinCanTxCompletionCode::CancelRequestFailed, now_us, false,
      slot->deadline_reported, slot->failure_reported,
      slot->driver_sequence, slot->driver_result,
      slot->cancel_driver_result, slot->write_duration_us);
  slot->failure_reported = true;
  latchTrackingFault(now_us, false);
}

bool BuiltinCanTxOwner::validFrame(const BuiltinCanTxFrame& frame) {
  if (frame.dlc > 8u ||
      (frame.can_id_flags & ~(0x1FFFFFFFu | kSupportedFlags)) != 0) {
    return false;
  }
  const bool extended = (frame.can_id_flags & kExtendedFlag) != 0;
  const uint32_t raw_id = frame.can_id_flags & 0x1FFFFFFFu;
  return extended || raw_id <= 0x7FFu;
}

void BuiltinCanTxOwner::increment(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}

uint32_t BuiltinCanTxOwner::nextSubmissionSequence() {
  // Submission identity is allowed to wrap; zero remains reserved as
  // "unassigned". Diagnostic counters use saturating arithmetic separately.
  submission_sequence_ += 1u;
  if (submission_sequence_ == 0u) submission_sequence_ = 1u;
  return submission_sequence_;
}

}  // namespace csm::board::can
