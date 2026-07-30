#include "board/uplink/CanonicalPublisher.h"

#include <string.h>

#include "board/uplink/UplinkPriorityPolicy.h"
#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

void CanonicalPublisher::begin(uint64_t boot_session_id, IFrameSink* sink0,
                               IFrameSink* sink1) {
  admission_.begin();
  sinks_[0] = sink0;
  sinks_[1] = sink1;
  boot_session_id_ = boot_session_id;
  publish_seq_next_ = 0;
  counters_ = {};
  session_pending_ = true;
  session_target_sink_mask_ = 0;
  session_reason_ = SessionAnnouncementReason::Boot;
}

bool CanonicalPublisher::enqueueRecord(csm::RecordType type, const uint8_t* payload,
                                       uint16_t length, UplinkPriority priority,
                                       uint8_t flags) {
  return admission_.enqueue(type, payload, length, priority, flags);
}

PublishServiceResult CanonicalPublisher::service(uint64_t now_us) {
  if (!hasConnectedSink()) {
    return {};
  }
  if (publish_seq_next_ != 0 &&
      static_cast<uint16_t>(publish_seq_next_ & 0xFFFFU) == 0) {
    requestSessionAnnouncement(SessionAnnouncementReason::SequenceWrap);
  }
  if (session_pending_) {
    return publishSession(now_us);
  }

  RecordAdmission::Record record;
  if (!admission_.popNext(record)) {
    return {};
  }
  const uint8_t* payload = admission_.payload(record);
  PublishServiceResult result = publish(record.type, payload, record.length,
                                        record.priority, record.flags, false);
  result.record_consumed = true;
  if (!result.record_published) {
    counters_.record_no_sink_drop_total++;
  }
  admission_.release(record);
  return result;
}

void CanonicalPublisher::requestSessionAnnouncement(SessionAnnouncementReason reason,
                                                     uint8_t target_sink_mask) {
  session_pending_ = true;
  session_target_sink_mask_ |=
      target_sink_mask == 0 ? connectedSinkMask() : target_sink_mask;
  if (reason == SessionAnnouncementReason::SequenceWrap ||
      session_reason_ != SessionAnnouncementReason::SequenceWrap) {
    session_reason_ = reason;
  }
}

void CanonicalPublisher::discardQueuedRecords() { admission_.discardAll(); }

bool CanonicalPublisher::hasConnectedSink() const {
  for (uint8_t i = 0; i < kMaxSinks; ++i) {
    if (sinks_[i] != nullptr && sinks_[i]->enabled() && sinks_[i]->connected()) {
      return true;
    }
  }
  return false;
}

PublishServiceResult CanonicalPublisher::publish(csm::RecordType type,
                                                  const uint8_t* payload,
                                                  uint16_t length,
                                                  UplinkPriority priority,
                                                  uint8_t flags,
                                                  bool session_record) {
  PublishServiceResult result;
  result.session_record = session_record;
  result.publish_seq = publish_seq_next_;
  size_t written = 0;
  if (!csm::encode_typed_frame(frame_, sizeof(frame_), type, payload, length,
                               static_cast<uint16_t>(publish_seq_next_ & 0xFFFFU),
                               flags, &written)) {
    counters_.frame_encode_fail_total++;
    if (!session_record) admission_.noteEncodeFailure(priority);
    return result;
  }

  PublishedFrameView frame;
  frame.bytes = frame_;
  frame.length = static_cast<uint16_t>(written);
  frame.publish_seq = publish_seq_next_;
  frame.type = type;
  frame.priority = priority;
  frame.delivery = default_delivery_for_record(type);
  for (uint8_t i = 0; i < kMaxSinks; ++i) {
    if (sinks_[i] == nullptr || !sinks_[i]->enabled()) continue;
    if (sinks_[i]->connected()) {
      result.connected_sink_mask |= static_cast<uint8_t>(1u << i);
    }
    const SinkOfferResult offered = sinks_[i]->offer(frame);
    if (offered == SinkOfferResult::Accepted) {
      result.sink_accept_count++;
      result.sink_accept_mask |= static_cast<uint8_t>(1u << i);
      counters_.sink_accept_total[i]++;
    } else if (offered == SinkOfferResult::Overflow ||
               offered == SinkOfferResult::Invalid) {
      counters_.sink_miss_total[i]++;
    }
  }

  if (result.sink_accept_count > 0) {
    result.record_published = true;
    counters_.record_publish_total++;
    if (session_record) counters_.session_publish_total++;
    publish_seq_next_++;
  }
  return result;
}

PublishServiceResult CanonicalPublisher::publishSession(uint64_t now_us) {
  if (session_target_sink_mask_ == 0) {
    session_target_sink_mask_ = connectedSinkMask();
  }
  const uint8_t required_sink_mask = session_target_sink_mask_;
  uint8_t payload[kStreamSessionPayloadLen] = {};
  payload[0] = csm::kStreamSessionSchema;
  payload[csm::kStreamSessionReasonOffset] = static_cast<uint8_t>(session_reason_);
  csm::wr_u16_le(&payload[csm::kStreamSessionFlagsOffset],
                 0x0001);  // boot id sourced from hardware TRNG.
  payload[csm::kStreamSessionProtocolVersionOffset] = csm::kProtocolVersion;
  csm::wr_u64_le(&payload[csm::kStreamSessionBootIdOffset], boot_session_id_);
  csm::wr_u64_le(&payload[csm::kStreamSessionPublishSeqOffset], publish_seq_next_);
  csm::wr_u64_le(&payload[csm::kStreamSessionMonoUsOffset], now_us);
  PublishServiceResult result =
      publish(csm::RecordType::StreamSession, payload, sizeof(payload),
              UplinkPriority::Critical, 0, true);
  result.required_sink_mask = required_sink_mask;
  result.missed_required_sink_mask =
      static_cast<uint8_t>(required_sink_mask & ~result.sink_accept_mask);
  // A canonical sequence may be assigned only once. Re-encoding a missed
  // per-sink anchor on every service pass creates new publish identities and
  // floods healthy sinks. The failed sink owns its admission failure and must
  // recover on a later connection epoch.
  session_pending_ = false;
  session_target_sink_mask_ = 0;
  return result;
}

uint8_t CanonicalPublisher::connectedSinkMask() const {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < kMaxSinks; ++i) {
    if (sinks_[i] != nullptr && sinks_[i]->enabled() && sinks_[i]->connected()) {
      mask |= static_cast<uint8_t>(1u << i);
    }
  }
  return mask;
}

}  // namespace csm::board::uplink
