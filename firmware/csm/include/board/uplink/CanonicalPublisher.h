#pragma once

#include <stdint.h>

#include "board/uplink/FrameSink.h"
#include "board/uplink/RecordAdmission.h"

namespace csm::board::uplink {

enum class SessionAnnouncementReason : uint8_t {
  Boot = 1,
  SinkEpochChanged = 2,
  SequenceWrap = 3,
  Periodic = 4,
};

struct PublisherCounters {
  uint32_t record_publish_total = 0;
  uint32_t record_no_sink_drop_total = 0;
  uint32_t frame_encode_fail_total = 0;
  uint32_t session_publish_total = 0;
  uint32_t sink_accept_total[2] = {};
  uint32_t sink_miss_total[2] = {};
};

struct PublishServiceResult {
  bool record_consumed = false;
  bool record_published = false;
  bool session_record = false;
  uint8_t sink_accept_count = 0;
  uint8_t connected_sink_mask = 0;
  uint8_t sink_accept_mask = 0;
  uint64_t publish_seq = 0;
};

class CanonicalPublisher {
 public:
  static constexpr uint8_t kMaxSinks = 2;

  void begin(uint64_t boot_session_id, IFrameSink* sink0, IFrameSink* sink1 = nullptr);
  bool enqueueRecord(csm::RecordType type, const uint8_t* payload, uint16_t length,
                     UplinkPriority priority, uint8_t flags = 0);
  PublishServiceResult service(uint64_t now_us);
  void requestSessionAnnouncement(SessionAnnouncementReason reason,
                                  uint8_t target_sink_mask = 0);
  void discardQueuedRecords();

  bool hasConnectedSink() const;
  bool hasQueuedRecords() const { return admission_.hasQueuedRecords(); }
  bool hasQueueSpace(UplinkPriority priority) const {
    return admission_.hasQueueSpace(priority);
  }
  bool pressureActive() const { return admission_.pressureActive(); }
  uint32_t queuedPayloadBytes() const { return admission_.queuedPayloadBytes(); }
  uint32_t queuedPayloadHighWaterBytes() const {
    return admission_.queuedPayloadHighWaterBytes();
  }
  uint32_t poolLargeUsed() const { return admission_.poolLargeUsed(); }
  uint32_t poolLargeCanReserveUsed() const {
    return admission_.poolLargeCanReserveUsed();
  }
  uint64_t bootSessionId() const { return boot_session_id_; }
  uint64_t nextPublishSeq() const { return publish_seq_next_; }
  const AdmissionCounters& admissionCounters() const { return admission_.counters(); }
  const PublisherCounters& counters() const { return counters_; }

 private:
  static constexpr uint16_t kStreamSessionPayloadLen = 32;

  RecordAdmission admission_;
  IFrameSink* sinks_[kMaxSinks] = {};
  uint8_t frame_[csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
  uint64_t boot_session_id_ = 0;
  uint64_t publish_seq_next_ = 0;
  bool session_pending_ = false;
  uint8_t session_target_sink_mask_ = 0;
  SessionAnnouncementReason session_reason_ = SessionAnnouncementReason::Boot;
  PublisherCounters counters_;

  PublishServiceResult publish(csm::RecordType type, const uint8_t* payload,
                               uint16_t length, UplinkPriority priority,
                               uint8_t flags, bool session_record);
  PublishServiceResult publishSession(uint64_t now_us);
  uint8_t connectedSinkMask() const;
};

}  // namespace csm::board::uplink
