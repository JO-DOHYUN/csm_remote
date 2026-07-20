#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board/uplink/FrameSink.h"

namespace csm::board::uplink {

template <uint8_t Capacity>
class FixedFrameQueue {
 public:
  static_assert(Capacity > 0, "Frame queue capacity must be non-zero");

  struct Entry {
    uint8_t bytes[csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
    uint16_t length = 0;
    uint16_t offset = 0;
    uint64_t publish_seq = 0;
    csm::RecordType type = static_cast<csm::RecordType>(0);
    UplinkPriority priority = UplinkPriority::Normal;
  };

  struct ConsumeResult {
    uint32_t bytes = 0;
    uint32_t frames = 0;
    uint64_t last_publish_seq = 0;
  };

  bool push(const PublishedFrameView& frame) {
    if (frame.bytes == nullptr || frame.length == 0 ||
        frame.length > sizeof(entries_[0].bytes) || count_ >= Capacity) {
      return false;
    }
    Entry& entry = entries_[tail_];
    memcpy(entry.bytes, frame.bytes, frame.length);
    entry.length = frame.length;
    entry.offset = 0;
    entry.publish_seq = frame.publish_seq;
    entry.type = frame.type;
    entry.priority = frame.priority;
    tail_ = static_cast<uint8_t>((tail_ + 1U) % Capacity);
    count_++;
    queued_bytes_ += frame.length;
    if (count_ > high_water_records_) {
      high_water_records_ = count_;
    }
    if (queued_bytes_ > high_water_bytes_) {
      high_water_bytes_ = queued_bytes_;
    }
    return true;
  }

  Entry* front() { return count_ == 0 ? nullptr : &entries_[head_]; }
  const Entry* front() const { return count_ == 0 ? nullptr : &entries_[head_]; }

  uint16_t copyFrontBytes(uint8_t* destination, uint16_t capacity) const {
    if (destination == nullptr || capacity == 0 || count_ == 0) {
      return 0;
    }
    uint16_t copied = 0;
    for (uint8_t index = 0; index < count_ && copied < capacity; ++index) {
      const Entry& entry = entries_[static_cast<uint8_t>((head_ + index) % Capacity)];
      const uint16_t remaining = static_cast<uint16_t>(entry.length - entry.offset);
      const uint16_t available = static_cast<uint16_t>(capacity - copied);
      const uint16_t amount = remaining < available ? remaining : available;
      memcpy(&destination[copied], &entry.bytes[entry.offset], amount);
      copied = static_cast<uint16_t>(copied + amount);
    }
    return copied;
  }

  ConsumeResult consumeMany(uint32_t bytes) {
    ConsumeResult result;
    while (bytes > 0) {
      Entry* entry = front();
      if (entry == nullptr) {
        break;
      }
      const uint16_t remaining = static_cast<uint16_t>(entry->length - entry->offset);
      const uint16_t consumed = bytes > remaining ? remaining : static_cast<uint16_t>(bytes);
      entry->offset = static_cast<uint16_t>(entry->offset + consumed);
      queued_bytes_ = queued_bytes_ >= consumed ? queued_bytes_ - consumed : 0;
      result.bytes += consumed;
      bytes -= consumed;
      if (entry->offset >= entry->length) {
        result.frames++;
        result.last_publish_seq = entry->publish_seq;
        pop();
      }
    }
    return result;
  }

  void consume(uint16_t bytes) {
    consumeMany(bytes);
  }

  uint32_t clear() {
    const uint32_t cleared = queued_bytes_;
    while (count_ > 0) {
      pop();
    }
    queued_bytes_ = 0;
    return cleared;
  }

  bool empty() const { return count_ == 0; }
  bool full() const { return count_ >= Capacity; }
  uint8_t count() const { return count_; }
  uint32_t queuedBytes() const { return queued_bytes_; }
  uint8_t highWaterRecords() const { return high_water_records_; }
  uint32_t highWaterBytes() const { return high_water_bytes_; }

 private:
  Entry entries_[Capacity] = {};
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
  uint8_t count_ = 0;
  uint8_t high_water_records_ = 0;
  uint32_t queued_bytes_ = 0;
  uint32_t high_water_bytes_ = 0;

  void pop() {
    if (count_ == 0) {
      return;
    }
    entries_[head_] = {};
    head_ = static_cast<uint8_t>((head_ + 1U) % Capacity);
    count_--;
  }
};

}  // namespace csm::board::uplink
