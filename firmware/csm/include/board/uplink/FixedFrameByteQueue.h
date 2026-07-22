#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board/uplink/FrameSink.h"

namespace csm::board::uplink {

// FIFO frame descriptors over one bounded byte ring. Unlike FixedFrameQueue,
// memory scales with the declared byte envelope rather than record_capacity *
// maximum_frame_size.
template <uint16_t RecordCapacity, uint32_t ByteCapacity>
class FixedFrameByteQueue {
 public:
  static_assert(RecordCapacity > 0, "frame descriptor capacity must be non-zero");
  static_assert(ByteCapacity >= csm::encoded_typed_frame_len(csm::kMaxPayloadLen),
                "byte queue must hold one maximum typed frame");

  struct ConsumeResult {
    uint32_t bytes = 0;
    uint32_t frames = 0;
    uint64_t last_publish_seq = 0;
  };

  bool push(const PublishedFrameView& frame) {
    if (frame.bytes == nullptr || frame.length == 0 ||
        frame.length > ByteCapacity || count_ >= RecordCapacity ||
        frame.length > availableBytes()) {
      return false;
    }
    Descriptor& descriptor = descriptors_[tail_];
    descriptor.length = frame.length;
    descriptor.offset = 0;
    descriptor.publish_seq = frame.publish_seq;
    descriptor.type = frame.type;
    descriptor.priority = frame.priority;
    copyIntoRing(frame.bytes, frame.length);
    tail_ = static_cast<uint16_t>((tail_ + 1u) % RecordCapacity);
    count_++;
    queued_bytes_ += frame.length;
    if (count_ > high_water_records_) high_water_records_ = count_;
    if (queued_bytes_ > high_water_bytes_) high_water_bytes_ = queued_bytes_;
    return true;
  }

  uint16_t copyFrontBytes(uint8_t* destination, uint16_t capacity) const {
    if (destination == nullptr || capacity == 0 || queued_bytes_ == 0) return 0;
    const uint32_t requested =
        queued_bytes_ < capacity ? queued_bytes_ : static_cast<uint32_t>(capacity);
    const uint32_t first =
        requested < ByteCapacity - byte_head_ ? requested : ByteCapacity - byte_head_;
    memcpy(destination, &bytes_[byte_head_], first);
    if (requested > first) memcpy(&destination[first], bytes_, requested - first);
    return static_cast<uint16_t>(requested);
  }

  ConsumeResult consumeMany(uint32_t bytes) {
    ConsumeResult result;
    while (bytes > 0 && count_ > 0) {
      Descriptor& descriptor = descriptors_[head_];
      const uint16_t remaining =
          static_cast<uint16_t>(descriptor.length - descriptor.offset);
      const uint16_t amount =
          bytes < remaining ? static_cast<uint16_t>(bytes) : remaining;
      descriptor.offset = static_cast<uint16_t>(descriptor.offset + amount);
      byte_head_ = (byte_head_ + amount) % ByteCapacity;
      queued_bytes_ -= amount;
      result.bytes += amount;
      bytes -= amount;
      if (descriptor.offset == descriptor.length) {
        result.frames++;
        result.last_publish_seq = descriptor.publish_seq;
        descriptor = {};
        head_ = static_cast<uint16_t>((head_ + 1u) % RecordCapacity);
        count_--;
      }
    }
    if (count_ == 0) {
      head_ = 0;
      tail_ = 0;
      byte_head_ = 0;
      byte_tail_ = 0;
      queued_bytes_ = 0;
    }
    return result;
  }

  uint32_t clear() {
    const uint32_t cleared = queued_bytes_;
    while (count_ > 0) {
      descriptors_[head_] = {};
      head_ = static_cast<uint16_t>((head_ + 1u) % RecordCapacity);
      count_--;
    }
    head_ = 0;
    tail_ = 0;
    byte_head_ = 0;
    byte_tail_ = 0;
    queued_bytes_ = 0;
    return cleared;
  }

  bool empty() const { return count_ == 0; }
  bool full() const { return count_ >= RecordCapacity || queued_bytes_ >= ByteCapacity; }
  uint16_t count() const { return count_; }
  uint32_t queuedBytes() const { return queued_bytes_; }
  uint32_t availableBytes() const { return ByteCapacity - queued_bytes_; }
  uint16_t highWaterRecords() const { return high_water_records_; }
  uint32_t highWaterBytes() const { return high_water_bytes_; }

 private:
  struct Descriptor {
    uint16_t length = 0;
    uint16_t offset = 0;
    uint64_t publish_seq = 0;
    csm::RecordType type = static_cast<csm::RecordType>(0);
    UplinkPriority priority = UplinkPriority::Normal;
  };

  Descriptor descriptors_[RecordCapacity] = {};
  uint8_t bytes_[ByteCapacity] = {};
  uint16_t head_ = 0;
  uint16_t tail_ = 0;
  uint16_t count_ = 0;
  uint16_t high_water_records_ = 0;
  uint32_t byte_head_ = 0;
  uint32_t byte_tail_ = 0;
  uint32_t queued_bytes_ = 0;
  uint32_t high_water_bytes_ = 0;

  void copyIntoRing(const uint8_t* source, uint16_t length) {
    const uint32_t first =
        length < ByteCapacity - byte_tail_ ? length : ByteCapacity - byte_tail_;
    memcpy(&bytes_[byte_tail_], source, first);
    if (length > first) memcpy(bytes_, &source[first], length - first);
    byte_tail_ = (byte_tail_ + length) % ByteCapacity;
  }
};

}  // namespace csm::board::uplink
