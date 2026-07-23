#pragma once

#include <atomic>
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
    uint32_t critical_frames = 0;
    uint64_t last_publish_seq = 0;
  };

  bool push(const PublishedFrameView& frame) {
    const uint32_t tail = descriptor_tail_.load(std::memory_order_relaxed);
    const uint32_t head = descriptor_head_.load(std::memory_order_acquire);
    const uint32_t byte_tail = producer_byte_tail_;
    const uint32_t byte_head = byte_head_.load(std::memory_order_acquire);
    if (frame.bytes == nullptr || frame.length == 0 ||
        frame.length > ByteCapacity || tail - head >= RecordCapacity ||
        frame.length > ByteCapacity - (byte_tail - byte_head)) {
      return false;
    }
    Descriptor& descriptor = descriptors_[tail % RecordCapacity];
    descriptor.length = frame.length;
    descriptor.offset = 0;
    descriptor.publish_seq = frame.publish_seq;
    descriptor.type = frame.type;
    descriptor.priority = frame.priority;
    descriptor.byte_end = byte_tail + frame.length;
    copyIntoRing(frame.bytes, frame.length, byte_tail);
    producer_byte_tail_ = byte_tail + frame.length;
    descriptor_tail_.store(tail + 1u, std::memory_order_release);
    byte_tail_.store(producer_byte_tail_, std::memory_order_release);
    const uint32_t count = tail + 1u - head;
    const uint32_t queued_bytes = byte_tail + frame.length - byte_head;
    if (count > high_water_records_) {
      high_water_records_ = static_cast<uint16_t>(count);
    }
    if (queued_bytes > high_water_bytes_) high_water_bytes_ = queued_bytes;
    return true;
  }

  uint16_t copyFrontBytes(uint8_t* destination, uint16_t capacity) const {
    const uint32_t descriptor_head = descriptor_head_.load(std::memory_order_relaxed);
    const uint32_t descriptor_tail = descriptor_tail_.load(std::memory_order_acquire);
    if (destination == nullptr || capacity == 0 ||
        descriptor_head == descriptor_tail) return 0;
    const uint32_t byte_head = byte_head_.load(std::memory_order_relaxed);
    const uint32_t committed_byte_tail =
        descriptors_[(descriptor_tail - 1u) % RecordCapacity].byte_end;
    const uint32_t queued_bytes = committed_byte_tail - byte_head;
    const uint32_t requested =
        queued_bytes < capacity ? queued_bytes : static_cast<uint32_t>(capacity);
    const uint32_t ring_head = byte_head % ByteCapacity;
    const uint32_t first =
        requested < ByteCapacity - ring_head ? requested : ByteCapacity - ring_head;
    memcpy(destination, &bytes_[ring_head], first);
    if (requested > first) memcpy(&destination[first], bytes_, requested - first);
    return static_cast<uint16_t>(requested);
  }

  ConsumeResult consumeMany(uint32_t bytes) {
    ConsumeResult result;
    uint32_t head = descriptor_head_.load(std::memory_order_relaxed);
    uint32_t byte_head = byte_head_.load(std::memory_order_relaxed);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    while (bytes > 0 && head != tail) {
      Descriptor& descriptor = descriptors_[head % RecordCapacity];
      const uint16_t remaining =
          static_cast<uint16_t>(descriptor.length - descriptor.offset);
      const uint16_t amount =
          bytes < remaining ? static_cast<uint16_t>(bytes) : remaining;
      descriptor.offset = static_cast<uint16_t>(descriptor.offset + amount);
      byte_head += amount;
      result.bytes += amount;
      bytes -= amount;
      if (descriptor.offset == descriptor.length) {
        result.frames++;
        if (descriptor.priority == UplinkPriority::Critical) {
          result.critical_frames++;
        }
        result.last_publish_seq = descriptor.publish_seq;
        descriptor = {};
        head++;
        descriptor_head_.store(head, std::memory_order_release);
      }
    }
    byte_head_.store(byte_head, std::memory_order_release);
    return result;
  }

  uint32_t clear() {
    uint32_t head = descriptor_head_.load(std::memory_order_relaxed);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    const uint32_t byte_head = byte_head_.load(std::memory_order_relaxed);
    const uint32_t byte_tail = byte_tail_.load(std::memory_order_relaxed);
    while (head != tail) {
      descriptors_[head % RecordCapacity] = {};
      head++;
    }
    descriptor_head_.store(tail, std::memory_order_release);
    byte_head_.store(byte_tail, std::memory_order_release);
    return byte_tail - byte_head;
  }

  bool empty() const { return count() == 0; }
  bool full() const {
    return count() >= RecordCapacity || queuedBytes() >= ByteCapacity;
  }
  uint16_t count() const {
    return static_cast<uint16_t>(descriptor_tail_.load(std::memory_order_acquire) -
                                 descriptor_head_.load(std::memory_order_acquire));
  }
  uint32_t queuedBytes() const {
    return byte_tail_.load(std::memory_order_acquire) -
           byte_head_.load(std::memory_order_acquire);
  }
  uint32_t availableBytes() const { return ByteCapacity - queuedBytes(); }
  uint16_t highWaterRecords() const { return high_water_records_; }
  uint32_t highWaterBytes() const { return high_water_bytes_; }

 private:
  struct Descriptor {
    uint16_t length = 0;
    uint16_t offset = 0;
    uint64_t publish_seq = 0;
    csm::RecordType type = static_cast<csm::RecordType>(0);
    UplinkPriority priority = UplinkPriority::Normal;
    uint32_t byte_end = 0;
  };

  Descriptor descriptors_[RecordCapacity] = {};
  uint8_t bytes_[ByteCapacity] = {};
  std::atomic<uint32_t> descriptor_head_{0};
  std::atomic<uint32_t> descriptor_tail_{0};
  uint16_t high_water_records_ = 0;
  std::atomic<uint32_t> byte_head_{0};
  std::atomic<uint32_t> byte_tail_{0};
  uint32_t producer_byte_tail_ = 0;
  uint32_t high_water_bytes_ = 0;

  void copyIntoRing(const uint8_t* source, uint16_t length,
                    uint32_t monotonic_tail) {
    const uint32_t ring_tail = monotonic_tail % ByteCapacity;
    const uint32_t first =
        length < ByteCapacity - ring_tail ? length : ByteCapacity - ring_tail;
    memcpy(&bytes_[ring_tail], source, first);
    if (length > first) memcpy(bytes_, &source[first], length - first);
  }
};

}  // namespace csm::board::uplink
