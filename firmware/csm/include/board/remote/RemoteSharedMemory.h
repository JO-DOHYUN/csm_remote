#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/remote/M4RemoteMailboxContract.h"

namespace csm::board::remote {

// This product replaces OpenAMP/RPC with a fixed mailbox and owns the 1 KiB
// resource-table window immediately before the stock OpenAMP data region.
// Linking OpenAMP/RPC into either core is therefore an incompatible profile.
static constexpr uintptr_t kRemoteSharedMemoryAddress = 0x38000000u;
static constexpr size_t kRemoteSharedMemoryReservedBytes = 0x0400u;
static constexpr uint32_t kRemoteSharedMemoryMagic = 0x5243534Du;  // "MSCR".
static constexpr uint16_t kRemoteSharedMemoryVersion = 3;

struct RemoteFrontendDiagnostics {
  uint32_t uart_baud = 0;
  uint32_t rx_bytes = 0;
  uint32_t valid_frames = 0;
  uint32_t rc_frames = 0;
  uint32_t accepted_rc_frames = 0;
  uint32_t link_frames = 0;
  uint32_t normalization_rejects = 0;
  uint32_t rejected_length = 0;
  uint32_t rejected_crc = 0;
  uint32_t rejected_address = 0;
  uint32_t inter_byte_resets = 0;
  uint32_t mailbox_publishes = 0;
  uint32_t telemetry_tx_frames = 0;
  uint32_t telemetry_tx_bytes = 0;
  uint32_t serial_write_failures = 0;
  uint32_t shared_publish_failures = 0;
  uint32_t last_rc_age_ms = 0xFFFFFFFFu;
  uint32_t last_link_statistics_age_ms = 0xFFFFFFFFu;
  uint32_t admission_resets = 0;
  uint32_t subset_rc_frames = 0;
  uint16_t raw_ch2 = 0;
  uint16_t raw_ch4 = 0;
  uint16_t raw_channels[kRcChannelCount] = {};
  uint8_t last_address = 0;
  uint8_t last_type = 0;
  uint8_t link_statistics_valid = 0;
  uint8_t uplink_rssi_ant1 = 0xFF;
  uint8_t uplink_rssi_ant2 = 0xFF;
  int8_t uplink_snr = 0;
  uint8_t active_antenna = 0xFF;
  uint8_t rf_profile = 0xFF;
  uint8_t uplink_rf_power = 0xFF;
  uint8_t downlink_rssi = 0xFF;
  uint8_t downlink_link_quality = 0xFF;
  int8_t downlink_snr = 0;
  uint16_t last_normalize_reject_detail = 0;
  uint16_t channel_valid_mask = 0;
  uint8_t admission_streak = 0;
  uint8_t receiver_qualified = 0;
  uint8_t link_statistics_type = 0;
  uint8_t reserved0 = 0;
};

struct alignas(32) RemoteSharedSampleSlot {
  uint32_t sequence_begin = 0;
  uint32_t m4_boot_id = 0;
  uint32_t heartbeat_sequence = 0;
  uint32_t reserved0 = 0;
  M4RemoteMailboxFrame mailbox = {};
  RemoteFrontendDiagnostics diagnostics = {};
  uint32_t checksum = 0;
  uint32_t sequence_end = 0;
};

struct alignas(32) RemoteTelemetrySlot {
  uint32_t sequence_begin = 0;
  uint32_t magic = kRemoteSharedMemoryMagic;
  uint16_t version = kRemoteSharedMemoryVersion;
  uint16_t frame_size = 0;
  uint32_t m7_time_ms = 0;
  uint8_t authority_state = 0;
  uint8_t safety_state = 0;
  uint8_t remote_link_state = 0;
  uint8_t flags = 0;
  char flight_mode[16] = {};
  uint32_t checksum = 0;
  uint32_t sequence_end = 0;
};

struct alignas(32) RemoteSharedHeader {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t region_size = 0;
  uint32_t m7_boot_id = 0;
  uint32_t reserved[5] = {};
};

struct alignas(32) RemoteM4ToM7Channel {
  uint32_t m4_boot_id = 0;
  uint32_t active_sample_slot = 0;
  uint32_t sample_sequence = 0;
  uint32_t reserved[5] = {};
  RemoteSharedSampleSlot samples[2] = {};
};

struct alignas(32) RemoteM7ToM4Channel {
  uint32_t active_telemetry_slot = 0;
  uint32_t telemetry_sequence = 0;
  uint32_t reserved[6] = {};
  RemoteTelemetrySlot telemetry[2] = {};
};

struct alignas(32) RemoteSharedMemoryRegion {
  RemoteSharedHeader header = {};
  RemoteM4ToM7Channel m4_to_m7 = {};
  RemoteM7ToM4Channel m7_to_m4 = {};
};

static_assert(sizeof(RemoteSharedHeader) % 32u == 0,
              "shared header must own complete cache lines");
static_assert(sizeof(RemoteM4ToM7Channel) % 32u == 0,
              "M4 channel must own complete cache lines");
static_assert(sizeof(RemoteM7ToM4Channel) % 32u == 0,
              "M7 channel must own complete cache lines");
static_assert(offsetof(RemoteSharedMemoryRegion, m4_to_m7) % 32u == 0,
              "M4 channel must start on a cache-line boundary");
static_assert(offsetof(RemoteSharedMemoryRegion, m7_to_m4) % 32u == 0,
              "M7 channel must start on a cache-line boundary");

static_assert(sizeof(RemoteSharedMemoryRegion) <= kRemoteSharedMemoryReservedBytes,
              "remote IPC must stay below the reserved SRAM4 window");

struct RemoteSharedSampleReadResult {
  bool accepted = false;
  bool new_sample = false;
  uint16_t detail = 0;
  uint32_t shared_sequence = 0;
  RemoteSharedSampleSlot slot = {};
};

RemoteSharedMemoryRegion* remoteSharedMemoryRegion();
void initializeRemoteSharedMemoryForM7(uint32_t m7_boot_id);
uint32_t initializeRemoteSharedMemoryForM4();

bool publishRemoteSharedSample(uint32_t m4_boot_id,
                               uint32_t heartbeat_sequence,
                               const M4RemoteMailboxFrame& mailbox,
                               const RemoteFrontendDiagnostics& diagnostics);
RemoteSharedSampleReadResult readRemoteSharedSample(uint32_t last_sequence);

bool publishRemoteTelemetry(const RemoteTelemetrySlot& telemetry);
bool readRemoteTelemetry(uint32_t last_sequence,
                         RemoteTelemetrySlot* telemetry,
                         uint32_t* published_sequence);

}  // namespace csm::board::remote
