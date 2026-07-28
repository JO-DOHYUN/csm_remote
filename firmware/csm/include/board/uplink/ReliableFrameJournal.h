#pragma once

#include <atomic>
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board/uplink/FrameSink.h"

namespace csm::board::uplink {

// Single-producer/single-worker retained frame journal.
//
// The canonical publisher is the only producer. The Wi-Fi worker is the only
// owner of send, reconnect-rewind and ACK/reclaim cursors. A positive socket
// write advances only the send cursor; storage is reusable only after a
// cumulative application commit ACK. No operation waits for the other side.
template <uint16_t RecordCapacity, uint32_t ByteCapacity>
class ReliableFrameJournal {
 private:
  struct Descriptor {
    uint64_t publish_seq = 0;
    uint32_t admitted_ms = 0;
    uint16_t length = 0;
    uint16_t ring_offset = 0;
    uint8_t priority = 0;
    uint8_t delivery = 0;
    uint16_t reserved0 = 0;
    uint32_t reserved1 = 0;
  };

  static_assert(sizeof(Descriptor) == 24,
                "reliable journal descriptor RAM contract changed");

  struct DescriptorSlot {
    alignas(Descriptor) uint8_t bytes[sizeof(Descriptor)];
  };

 public:
  static_assert(RecordCapacity > 0,
                "journal descriptor capacity must be non-zero");
  static_assert(ByteCapacity <= UINT16_MAX,
                "journal byte ring must fit a 16-bit physical offset");
  static_assert(ByteCapacity >=
                    csm::encoded_typed_frame_len(csm::kMaxPayloadLen),
                "journal must hold one maximum typed frame");

  static constexpr size_t kDescriptorSizeBytes = sizeof(Descriptor);
  static constexpr size_t kDescriptorStorageBytes =
      sizeof(Descriptor) * RecordCapacity;

  // Raw NOLOAD storage. Cursors remain in initialized RAM and a descriptor is
  // never observed before the producer's release-store commit.
  struct Storage {
    DescriptorSlot descriptors[RecordCapacity];
    uint8_t bytes[ByteCapacity];
  };

  static constexpr size_t kStorageBytes = sizeof(Storage);

  struct TxLease {
    uint32_t generation = 0;
    uint16_t length = 0;
  };

  struct SendResult {
    uint32_t bytes = 0;
    uint32_t frames = 0;
    uint32_t critical_frames = 0;
    uint32_t latency_frames = 0;
    uint64_t last_publish_seq = 0;
  };

  enum class AckStatus : uint8_t {
    Accepted = 0,
    Duplicate,
    NoSentFrame,
    AheadOfSent,
    NotContiguous,
  };

  struct AckResult {
    AckStatus status = AckStatus::NotContiguous;
    uint32_t reclaimed_bytes = 0;
    uint32_t reclaimed_frames = 0;
    uint64_t last_acked_publish_seq = 0;

    bool accepted() const {
      return status == AckStatus::Accepted || status == AckStatus::Duplicate;
    }
  };

  struct Snapshot {
    uint32_t retained_bytes = 0;
    uint32_t unsent_bytes = 0;
    uint32_t high_water_bytes = 0;
    uint32_t front_admitted_ms = 0;
    uint16_t retained_records = 0;
    uint16_t unsent_records = 0;
    uint16_t high_water_records = 0;
    bool front_latency_bounded = false;
    bool ack_valid = false;
    bool sent_valid = false;
    uint64_t last_acked_publish_seq = 0;
    uint64_t highest_sent_publish_seq = 0;
  };

  explicit ReliableFrameJournal(Storage& storage) : storage_(&storage) {}
  ReliableFrameJournal(const ReliableFrameJournal&) = delete;
  ReliableFrameJournal& operator=(const ReliableFrameJournal&) = delete;

  bool push(const PublishedFrameView& frame, uint32_t admitted_ms) {
    const uint32_t tail =
        descriptor_tail_.load(std::memory_order_relaxed);
    const uint32_t reclaim_head =
        reclaim_head_.load(std::memory_order_acquire);
    const uint32_t byte_tail = producer_byte_tail_;
    const uint32_t byte_head =
        reclaim_byte_head_.load(std::memory_order_acquire);
    if (frame.bytes == nullptr || frame.length == 0 ||
        frame.length > ByteCapacity ||
        tail - reclaim_head >= RecordCapacity ||
        frame.length > ByteCapacity - (byte_tail - byte_head)) {
      return false;
    }

    Descriptor& descriptor =
        *new (storage_->descriptors[tail % RecordCapacity].bytes) Descriptor();
    descriptor.publish_seq = frame.publish_seq;
    descriptor.admitted_ms = admitted_ms;
    descriptor.length = frame.length;
    descriptor.ring_offset = producer_ring_tail_;
    descriptor.priority = static_cast<uint8_t>(frame.priority);
    descriptor.delivery = static_cast<uint8_t>(frame.delivery);
    copyIntoRing(frame.bytes, frame.length, producer_ring_tail_);

    producer_ring_tail_ = advanceRingOffset(producer_ring_tail_, frame.length);
    producer_byte_tail_ = byte_tail + frame.length;
    byte_tail_.store(producer_byte_tail_, std::memory_order_relaxed);
    descriptor_tail_.store(tail + 1u, std::memory_order_release);

    const uint32_t records = tail + 1u - reclaim_head;
    const uint32_t bytes = byte_tail + frame.length - byte_head;
    updateHighWater(high_water_records_, records);
    updateHighWater(high_water_bytes_, bytes);
    return true;
  }

  bool stage(uint8_t* destination, uint16_t capacity, TxLease& lease) const {
    if (destination == nullptr || capacity == 0) return false;
    const uint32_t head = send_head_.load(std::memory_order_acquire);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    if (head == tail) return false;

    uint32_t cursor = head;
    uint16_t descriptor_offset =
        send_descriptor_offset_.load(std::memory_order_acquire);
    uint16_t copied = 0;
    while (cursor != tail && copied < capacity) {
      const Descriptor& descriptor = descriptorAt(cursor);
      const uint16_t remaining =
          static_cast<uint16_t>(descriptor.length - descriptor_offset);
      const uint16_t room = static_cast<uint16_t>(capacity - copied);
      const uint16_t amount = remaining < room ? remaining : room;
      copyFromRing(&destination[copied],
                   advanceRingOffset(descriptor.ring_offset,
                                     descriptor_offset),
                   amount);
      copied = static_cast<uint16_t>(copied + amount);
      descriptor_offset = static_cast<uint16_t>(descriptor_offset + amount);
      if (descriptor_offset == descriptor.length) {
        ++cursor;
        descriptor_offset = 0;
      }
    }
    lease.generation = generation_.load(std::memory_order_relaxed);
    lease.length = copied;
    return copied != 0;
  }

  bool advanceSent(const TxLease& lease, uint16_t bytes, SendResult& result,
                   bool& stale_generation) {
    stale_generation =
        lease.generation != generation_.load(std::memory_order_relaxed);
    if (stale_generation) return true;
    uint32_t remaining =
        bytes > lease.length ? lease.length : static_cast<uint32_t>(bytes);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    uint32_t send_head = send_head_.load(std::memory_order_relaxed);
    uint16_t send_offset =
        send_descriptor_offset_.load(std::memory_order_relaxed);
    while (remaining != 0 && send_head != tail) {
      const Descriptor& descriptor = descriptorAt(send_head);
      const uint16_t descriptor_remaining =
          static_cast<uint16_t>(descriptor.length - send_offset);
      const uint16_t amount =
          remaining < descriptor_remaining
              ? static_cast<uint16_t>(remaining)
              : descriptor_remaining;
      send_offset = static_cast<uint16_t>(send_offset + amount);
      send_byte_head_.fetch_add(amount, std::memory_order_release);
      result.bytes += amount;
      remaining -= amount;
      if (send_offset == descriptor.length) {
        ++result.frames;
        if (descriptor.priority ==
            static_cast<uint8_t>(UplinkPriority::Critical)) {
          ++result.critical_frames;
        }
        if (descriptor.delivery ==
            static_cast<uint8_t>(UplinkDeliveryClass::LatencyBounded)) {
          ++result.latency_frames;
        }
        result.last_publish_seq = descriptor.publish_seq;
        highest_sent_publish_seq_ = descriptor.publish_seq;
        sent_valid_ = true;
        ++send_head;
        send_offset = 0;
      }
    }
    send_descriptor_offset_.store(send_offset, std::memory_order_relaxed);
    send_head_.store(send_head, std::memory_order_release);
    return true;
  }

  AckResult acknowledge(uint64_t publish_seq) {
    AckResult result;
    result.last_acked_publish_seq = last_acked_publish_seq_;
    if (ack_valid_ && publish_seq == last_acked_publish_seq_) {
      result.status = AckStatus::Duplicate;
      return result;
    }
    if (!sent_valid_) {
      result.status = AckStatus::NoSentFrame;
      return result;
    }
    if (publish_seq > highest_sent_publish_seq_) {
      result.status = AckStatus::AheadOfSent;
      return result;
    }
    if (ack_valid_ && publish_seq < last_acked_publish_seq_) {
      result.status = AckStatus::NotContiguous;
      return result;
    }

    uint32_t head = reclaim_head_.load(std::memory_order_relaxed);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    uint32_t reclaimed_bytes = 0;
    uint32_t reclaimed_frames = 0;
    bool found = false;
    while (head != tail) {
      const Descriptor& descriptor = descriptorAt(head);
      if (descriptor.publish_seq > publish_seq) break;
      reclaimed_bytes += descriptor.length;
      ++reclaimed_frames;
      ++head;
      if (descriptor.publish_seq == publish_seq) {
        found = true;
        break;
      }
    }
    if (!found) {
      result.status = AckStatus::NotContiguous;
      return result;
    }

    const uint32_t reclaim_byte_head =
        reclaim_byte_head_.load(std::memory_order_relaxed) + reclaimed_bytes;

    // A reconnect rewinds send_head to the previous reclaim boundary. A
    // restarted consumer can then prove that bytes sent in the prior epoch
    // were already durable and ACK them before replay reaches that point.
    // Never leave the send cursor behind newly reclaimed storage: the producer
    // may immediately reuse those descriptors and ring bytes.
    const uint32_t send_head = send_head_.load(std::memory_order_relaxed);
    if (cursorBehind(send_head, head)) {
      send_descriptor_offset_.store(0, std::memory_order_relaxed);
      send_byte_head_.store(reclaim_byte_head, std::memory_order_release);
      send_head_.store(head, std::memory_order_release);
    }
    reclaim_byte_head_.store(reclaim_byte_head, std::memory_order_relaxed);
    reclaim_head_.store(head, std::memory_order_release);
    last_acked_publish_seq_ = publish_seq;
    ack_valid_ = true;
    result.status = AckStatus::Accepted;
    result.reclaimed_bytes = reclaimed_bytes;
    result.reclaimed_frames = reclaimed_frames;
    result.last_acked_publish_seq = publish_seq;
    return result;
  }

  // Called only by the socket worker after an epoch closes. Bytes already
  // ACKed stay reclaimed; every other record becomes replayable.
  void rewindToLastAck() {
    send_descriptor_offset_.store(0, std::memory_order_relaxed);
    send_head_.store(reclaim_head_.load(std::memory_order_acquire),
                     std::memory_order_release);
    send_byte_head_.store(
        reclaim_byte_head_.load(std::memory_order_acquire),
        std::memory_order_release);
    generation_.fetch_add(1, std::memory_order_relaxed);
  }

  // Explicit lifecycle reset only. Product disconnect paths must use rewind.
  uint32_t clear() {
    const uint32_t byte_tail = byte_tail_.load(std::memory_order_acquire);
    const uint32_t byte_head =
        reclaim_byte_head_.load(std::memory_order_relaxed);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    reclaim_head_.store(tail, std::memory_order_release);
    reclaim_byte_head_.store(byte_tail, std::memory_order_release);
    send_descriptor_offset_.store(0, std::memory_order_relaxed);
    send_head_.store(tail, std::memory_order_release);
    send_byte_head_.store(byte_tail, std::memory_order_release);
    ack_valid_ = false;
    sent_valid_ = false;
    last_acked_publish_seq_ = 0;
    highest_sent_publish_seq_ = 0;
    generation_.fetch_add(1, std::memory_order_relaxed);
    return byte_tail - byte_head;
  }

  // worker_owned_fields may be requested only by the single socket worker.
  // Producer/main diagnostics use the atomic cursor subset and receive no
  // unsynchronised 64-bit ACK/send state.
  Snapshot snapshot(bool worker_owned_fields = false) const {
    Snapshot result;
    const uint32_t reclaim_head =
        reclaim_head_.load(std::memory_order_acquire);
    const uint32_t tail = descriptor_tail_.load(std::memory_order_acquire);
    const uint32_t byte_tail = byte_tail_.load(std::memory_order_acquire);
    result.retained_records =
        static_cast<uint16_t>(tail - reclaim_head);
    const uint32_t send_head = send_head_.load(std::memory_order_acquire);
    result.unsent_records = static_cast<uint16_t>(tail - send_head);
    result.retained_bytes =
        byte_tail - reclaim_byte_head_.load(std::memory_order_acquire);
    result.unsent_bytes =
        byte_tail - send_byte_head_.load(std::memory_order_acquire);
    result.high_water_records = static_cast<uint16_t>(
        high_water_records_.load(std::memory_order_acquire));
    result.high_water_bytes =
        high_water_bytes_.load(std::memory_order_acquire);
    if (send_head != tail) {
      const Descriptor& front = descriptorAt(send_head);
      result.front_admitted_ms = front.admitted_ms;
      result.front_latency_bounded =
          front.delivery ==
          static_cast<uint8_t>(UplinkDeliveryClass::LatencyBounded);
    }
    if (worker_owned_fields) {
      result.ack_valid = ack_valid_;
      result.sent_valid = sent_valid_;
      result.last_acked_publish_seq = last_acked_publish_seq_;
      result.highest_sent_publish_seq = highest_sent_publish_seq_;
    }
    return result;
  }

  uint16_t retainedRecords() const {
    return snapshot().retained_records;
  }
  uint32_t retainedBytes() const { return snapshot().retained_bytes; }
  uint32_t availableBytes() const {
    return ByteCapacity - retainedBytes();
  }

 private:
  Storage* storage_ = nullptr;

  // Producer-owned cursors.
  std::atomic<uint32_t> descriptor_tail_{0};
  std::atomic<uint32_t> byte_tail_{0};
  uint32_t producer_byte_tail_ = 0;
  uint16_t producer_ring_tail_ = 0;

  // Worker-owned send cursors. Atomic byte publication is diagnostic only.
  std::atomic<uint32_t> send_head_{0};
  std::atomic<uint16_t> send_descriptor_offset_{0};
  std::atomic<uint32_t> send_byte_head_{0};
  bool sent_valid_ = false;
  uint64_t highest_sent_publish_seq_ = 0;

  // Worker-owned reclaim state, observed by the producer.
  std::atomic<uint32_t> reclaim_head_{0};
  std::atomic<uint32_t> reclaim_byte_head_{0};
  bool ack_valid_ = false;
  uint64_t last_acked_publish_seq_ = 0;

  std::atomic<uint32_t> generation_{0};
  std::atomic<uint32_t> high_water_records_{0};
  std::atomic<uint32_t> high_water_bytes_{0};

  static void updateHighWater(std::atomic<uint32_t>& target,
                              uint32_t candidate) {
    uint32_t observed = target.load(std::memory_order_relaxed);
    while (candidate > observed &&
           !target.compare_exchange_weak(observed, candidate,
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {
    }
  }

  static bool cursorBehind(uint32_t candidate, uint32_t floor) {
    // The bounded live distance is far below 2^31, so signed modular ordering
    // remains valid across the uint32 monotonic cursor wrap.
    return static_cast<int32_t>(candidate - floor) < 0;
  }

  void copyIntoRing(const uint8_t* source, uint16_t length,
                    uint16_t ring_tail) {
    const uint32_t first =
        length < ByteCapacity - ring_tail ? length : ByteCapacity - ring_tail;
    memcpy(&storage_->bytes[ring_tail], source, first);
    if (length > first) {
      memcpy(storage_->bytes, &source[first], length - first);
    }
  }

  void copyFromRing(uint8_t* destination, uint16_t ring_head,
                    uint16_t length) const {
    const uint32_t first =
        length < ByteCapacity - ring_head ? length : ByteCapacity - ring_head;
    memcpy(destination, &storage_->bytes[ring_head], first);
    if (length > first) {
      memcpy(&destination[first], storage_->bytes, length - first);
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
