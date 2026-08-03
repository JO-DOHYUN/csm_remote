#pragma once

#include <atomic>
#include <new>
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
 private:
  // byte_end_low is the low 16 bits of the committed monotonic byte end.
  // Since the queue can hold fewer than 65536 bytes, uint16 subtraction from
  // the consumer byte head recovers the exact committed distance, including a
  // completely full ring and uint32 counter wrap.
  struct Descriptor {
    uint64_t publish_seq = 0;
    uint16_t length = 0;
    uint16_t offset = 0;
    uint16_t byte_end_low = 0;
    UplinkPriority priority = UplinkPriority::Normal;
    UplinkDeliveryClass delivery = UplinkDeliveryClass::Batchable;
  };

  static_assert(sizeof(Descriptor) == 16,
                "frame descriptor RAM contract changed");

  struct DescriptorSlot {
    alignas(Descriptor) uint8_t bytes[sizeof(Descriptor)];
  };

 public:
  static_assert(RecordCapacity > 0, "frame descriptor capacity must be non-zero");
  static_assert(ByteCapacity <= UINT16_MAX,
                "16-bit committed-byte distance requires capacity below 65536");
  static_assert(ByteCapacity >= csm::encoded_typed_frame_len(csm::kMaxPayloadLen),
                "byte queue must hold one maximum typed frame");
  static constexpr size_t kDescriptorSizeBytes = sizeof(Descriptor);
  static constexpr size_t kDescriptorStorageBytes =
      sizeof(Descriptor) * RecordCapacity;

  // This is deliberately raw storage: production places it in a NOLOAD DTCM
  // section. Queue cursors and atomics remain in normally initialized D1 RAM.
  struct Storage {
    DescriptorSlot descriptors[RecordCapacity];
    uint8_t bytes[ByteCapacity];
  };

  static constexpr size_t kStorageBytes = sizeof(Storage);

  struct ConsumeResult {
    uint32_t bytes = 0;
    uint32_t frames = 0;
    uint32_t critical_frames = 0;
    uint32_t latency_frames = 0;
    uint64_t last_publish_seq = 0;
  };

  struct ClearResult {
    uint32_t bytes = 0;
    uint32_t records = 0;
    uint64_t first_publish_seq = 0;
    uint64_t last_publish_seq = 0;
    bool sequence_valid = false;
  };

  explicit FixedFrameByteQueue(Storage& storage) : storage_(&storage) {}
  FixedFrameByteQueue(const FixedFrameByteQueue&) = delete;
  FixedFrameByteQueue& operator=(const FixedFrameByteQueue&) = delete;
  FixedFrameByteQueue(FixedFrameByteQueue&&) = delete;
  FixedFrameByteQueue& operator=(FixedFrameByteQueue&&) = delete;

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
    Descriptor& descriptor = *new (
        storage_->descriptors[tail % RecordCapacity].bytes) Descriptor();
    descriptor.publish_seq = frame.publish_seq;
    descriptor.length = frame.length;
    descriptor.offset = 0;
    descriptor.byte_end_low =
        static_cast<uint16_t>(byte_tail + frame.length);
    descriptor.priority = frame.priority;
    descriptor.delivery = frame.delivery;
    copyIntoRing(frame.bytes, frame.length, producer_byte_tail_offset_);
    producer_byte_tail_offset_ =
        advanceRingOffset(producer_byte_tail_offset_, frame.length);
    producer_byte_tail_ = byte_tail + frame.length;
    // byte_tail_ is an occupancy upper bound and is allowed to lead briefly.
    // descriptor_tail_ is the sole record commit: its release publishes the
    // descriptor, payload bytes, and the preceding byte-tail update together.
    // A concurrent telemetry snapshot may therefore over-count by at most this
    // one in-flight frame; it can never expose or consume that frame early.
    byte_tail_.store(producer_byte_tail_, std::memory_order_relaxed);
    descriptor_tail_.store(tail + 1u, std::memory_order_release);
    const uint32_t count = tail + 1u - head;
    const uint32_t queued_bytes = byte_tail + frame.length - byte_head;
    if (count > high_water_records_.load(std::memory_order_relaxed)) {
      high_water_records_.store(count, std::memory_order_release);
    }
    if (queued_bytes > high_water_bytes_.load(std::memory_order_relaxed)) {
      high_water_bytes_.store(queued_bytes, std::memory_order_release);
    }
    return true;
  }

  uint16_t copyFrontBytes(uint8_t* destination, uint16_t capacity) const {
    const uint32_t descriptor_head = descriptor_head_.load(std::memory_order_relaxed);
    const uint32_t descriptor_tail = descriptor_tail_.load(std::memory_order_acquire);
    if (destination == nullptr || capacity == 0 ||
        descriptor_head == descriptor_tail) return 0;
    const uint16_t committed_byte_tail =
        descriptorAt(descriptor_tail - 1u).byte_end_low;
    const uint16_t consumer_byte_head = static_cast<uint16_t>(
        byte_head_.load(std::memory_order_relaxed));
    const uint32_t queued_bytes = static_cast<uint16_t>(
        committed_byte_tail - consumer_byte_head);
    const uint32_t requested =
        queued_bytes < capacity ? queued_bytes : static_cast<uint32_t>(capacity);
    const uint32_t ring_head = consumer_byte_head_offset_;
    const uint32_t first =
        requested < ByteCapacity - ring_head ? requested : ByteCapacity - ring_head;
    memcpy(destination, &storage_->bytes[ring_head], first);
    if (requested > first) {
      memcpy(&destination[first], storage_->bytes, requested - first);
    }
    return static_cast<uint16_t>(requested);
  }

  ConsumeResult consumeMany(uint32_t bytes) {
    ConsumeResult result;
    uint32_t head = descriptor_head_.load(std::memory_order_relaxed);
    uint32_t byte_head = byte_head_.load(std::memory_order_relaxed);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    while (bytes > 0 && head != tail) {
      Descriptor& descriptor = descriptorAt(head);
      const uint16_t remaining =
          static_cast<uint16_t>(descriptor.length - descriptor.offset);
      const uint16_t amount =
          bytes < remaining ? static_cast<uint16_t>(bytes) : remaining;
      descriptor.offset = static_cast<uint16_t>(descriptor.offset + amount);
      byte_head += amount;
      consumer_byte_head_offset_ =
          advanceRingOffset(consumer_byte_head_offset_, amount);
      result.bytes += amount;
      bytes -= amount;
      if (descriptor.offset == descriptor.length) {
        result.frames++;
        if (descriptor.priority == UplinkPriority::Critical) {
          result.critical_frames++;
        }
        if (descriptor.delivery == UplinkDeliveryClass::LatencyBounded) {
          result.latency_frames++;
        }
        result.last_publish_seq = descriptor.publish_seq;
        descriptor.~Descriptor();
        head++;
        descriptor_head_.store(head, std::memory_order_release);
      }
    }
    byte_head_.store(byte_head, std::memory_order_release);
    return result;
  }

  ClearResult clearWithEvidence(uint32_t excluded_tail_records = 0) {
    ClearResult result;
    uint32_t head = descriptor_head_.load(std::memory_order_relaxed);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    const uint32_t physical_records = tail - head;
    if (excluded_tail_records > physical_records) {
      excluded_tail_records = physical_records;
    }
    const uint32_t evidence_records =
        physical_records - excluded_tail_records;
    const uint32_t byte_head = byte_head_.load(std::memory_order_relaxed);
    const bool had_descriptors = head != tail;
    const uint16_t committed_byte_tail =
        had_descriptors
            ? descriptorAt(tail - 1u).byte_end_low
            : static_cast<uint16_t>(byte_head);
    result.bytes = had_descriptors
        ? static_cast<uint16_t>(
              committed_byte_tail - static_cast<uint16_t>(byte_head))
        : 0u;
    uint32_t cleared_records = 0;
    while (head != tail) {
      if (cleared_records < evidence_records) {
        const Descriptor& descriptor = descriptorAt(head);
        if (!result.sequence_valid) {
          result.first_publish_seq = descriptor.publish_seq;
          result.sequence_valid = true;
        }
        result.last_publish_seq = descriptor.publish_seq;
        result.records++;
      }
      descriptorAt(head).~Descriptor();
      head++;
      cleared_records++;
    }
    consumer_byte_head_offset_ = advanceRingOffset(
        consumer_byte_head_offset_, static_cast<uint16_t>(result.bytes));
    descriptor_head_.store(tail, std::memory_order_release);
    byte_head_.store(byte_head + result.bytes, std::memory_order_release);
    return result;
  }

  uint32_t clear() {
    return clearWithEvidence().bytes;
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
  uint16_t highWaterRecords() const {
    return static_cast<uint16_t>(
        high_water_records_.load(std::memory_order_acquire));
  }
  uint32_t highWaterBytes() const {
    return high_water_bytes_.load(std::memory_order_acquire);
  }

 private:
  Storage* storage_ = nullptr;
  std::atomic<uint32_t> descriptor_head_{0};
  std::atomic<uint32_t> descriptor_tail_{0};
  std::atomic<uint32_t> high_water_records_{0};
  std::atomic<uint32_t> byte_head_{0};
  std::atomic<uint32_t> byte_tail_{0};
  uint32_t producer_byte_tail_ = 0;
  std::atomic<uint32_t> high_water_bytes_{0};
  uint16_t producer_byte_tail_offset_ = 0;
  uint16_t consumer_byte_head_offset_ = 0;

  void copyIntoRing(const uint8_t* source, uint16_t length,
                    uint16_t ring_tail) {
    const uint32_t first =
        length < ByteCapacity - ring_tail ? length : ByteCapacity - ring_tail;
    memcpy(&storage_->bytes[ring_tail], source, first);
    if (length > first) {
      memcpy(storage_->bytes, &source[first], length - first);
    }
  }

  Descriptor& descriptorAt(uint32_t monotonic_index) {
    return *reinterpret_cast<Descriptor*>(
        storage_->descriptors[monotonic_index % RecordCapacity].bytes);
  }

  const Descriptor& descriptorAt(uint32_t monotonic_index) const {
    return *reinterpret_cast<const Descriptor*>(
        storage_->descriptors[monotonic_index % RecordCapacity].bytes);
  }

  static uint16_t advanceRingOffset(uint16_t offset, uint16_t amount) {
    const uint32_t advanced = static_cast<uint32_t>(offset) + amount;
    return static_cast<uint16_t>(
        advanced >= ByteCapacity ? advanced - ByteCapacity : advanced);
  }

};

}  // namespace csm::board::uplink
