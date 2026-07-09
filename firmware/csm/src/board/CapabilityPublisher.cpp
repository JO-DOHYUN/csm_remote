#include "board/CapabilityPublisher.h"

#include <cstring>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace csm::board {

namespace {

constexpr uint32_t record_bit(RecordType type) {
  return 1u << static_cast<uint8_t>(type);
}

bool supports_uplink_record(const CapabilityPayloadConfig& config, RecordType type) {
  return (config.supported_uplink_records & record_bit(type)) != 0;
}

void write_bus_descriptor(uint8_t* payload, uint8_t offset, const CapabilityBusDescriptor& bus) {
  payload[offset + 0] = bus.bus_id;
  payload[offset + 1] = bus.role;
  payload[offset + 2] = bus.backend;
  payload[offset + 3] = bus.transceiver;
  payload[offset + 4] = bus.rx_supported;
  payload[offset + 5] = bus.tx_supported;
  payload[offset + 6] = bus.control_tx_allowed;
  payload[offset + 7] = bus.classic_can_supported;
  payload[offset + 8] = bus.can_fd_supported;
  payload[offset + 9] = bus.max_live_dlc;
  wr_u32_le(&payload[offset + 10], bus.nominal_bitrate);
  wr_u32_le(&payload[offset + 14], bus.data_bitrate);
  payload[offset + 18] = bus.termination_policy;
  payload[offset + 19] = bus.isolation_policy;
}

void write_ascii_field(uint8_t* dst, uint8_t field_len, const char* value) {
  memset(dst, 0, field_len);
  if (value == nullptr || field_len == 0) {
    return;
  }
  uint8_t i = 0;
  while (i + 1 < field_len && value[i] != '\0') {
    dst[i] = static_cast<uint8_t>(value[i]);
    ++i;
  }
}

}  // namespace

uint16_t build_capability_payload(const CapabilityPayloadConfig& config,
                                  uint8_t* payload,
                                  uint16_t capacity) {
  if (capacity < kCapabilityV1PayloadLen) {
    return 0;
  }

  memset(payload, 0, capacity);
  wr_u64_le(&payload[0], mono64_us());
  payload[8] = kProtocolVersion;
  payload[9] = config.profile_major;
  payload[10] = config.profile_minor;
  payload[11] = 1;
  wr_u32_le(&payload[12], config.can_queue_size);
  wr_u32_le(&payload[16], config.encoder_ppr);
  wr_u32_le(&payload[20], config.encoder_frequency_limit_hz);
  payload[24] = (supports_uplink_record(config, RecordType::CanRxRaw) ||
                 supports_uplink_record(config, RecordType::CanRxSegment)) ? 0x01 : 0x00;
  payload[25] = supports_uplink_record(config, RecordType::CanTxRaw) ? 0x01 : 0x00;
  payload[26] = supports_uplink_record(config, RecordType::EncEdgeRaw) ? 0x01 : 0x00;
  payload[27] = supports_uplink_record(config, RecordType::EncDerived) ? 0x01 : 0x00;
  payload[28] = config.adc_sample_supported ? 0x01 : 0x00;
  payload[29] = 0x01;
  payload[30] = 0x01;
  payload[31] = config.adc_channel_count;
  payload[32] = config.adc_resolution_bits;
  payload[33] = config.adc_sample_period_ms;
  payload[34] = config.lane_capability_flags;
  payload[35] = config.limitation_flags;

  if (!config.include_v2 || capacity < kCapabilityV2PayloadLen) {
    return kCapabilityV1PayloadLen;
  }

  payload[36] = config.bus_count;
  payload[37] = kCapabilityV2BusDescriptorLen;
  wr_u16_le(&payload[38], config.capability_v2_flags);

  const uint8_t bounded_bus_count = (config.bus_count > 2) ? 2 : config.bus_count;
  for (uint8_t i = 0; i < bounded_bus_count; ++i) {
    write_bus_descriptor(payload, static_cast<uint8_t>(40 + i * kCapabilityV2BusDescriptorLen),
                         config.buses[i]);
  }

  if (!config.include_v3 || capacity < kCapabilityV3PayloadLen) {
    return kCapabilityV2PayloadLen;
  }

  wr_u32_le(&payload[80], config.supported_uplink_records);
  wr_u32_le(&payload[84], config.supported_downlink_records);
  wr_u32_le(&payload[88], config.safety_feature_flags);
  wr_u32_le(&payload[92], config.policy_hash);
  wr_u32_le(&payload[96], config.firmware_build_id);
  wr_u16_le(&payload[100], config.host_tx_queue_size);
  wr_u16_le(&payload[102], config.capability_v3_flags);

  if (!config.include_v4 || capacity < kCapabilityV4PayloadLen) {
    return kCapabilityV3PayloadLen;
  }

  payload[112] = config.firmware_identity_version;
  payload[113] = config.firmware_dirty ? 1 : 0;
  payload[114] = config.firmware_irq_mode;
  payload[115] = 0;
  wr_u32_le(&payload[116], config.firmware_build_epoch);
  wr_u32_le(&payload[120], config.firmware_build_id);
  wr_u32_le(&payload[124], config.mcp_spi_hz);
  wr_u16_le(&payload[128], config.can_record_drain_budget);
  wr_u16_le(&payload[130], config.serial_ring_kib);
  write_ascii_field(&payload[132], kFirmwareGitShaFieldLen, config.firmware_git_sha);
  write_ascii_field(&payload[144], kFirmwareEnvNameFieldLen, config.firmware_env_name);

  if (!config.include_v5 || capacity < kCapabilityV5PayloadLen) {
    return kCapabilityV4PayloadLen;
  }

  payload[192] = config.firmware_profile;
  payload[193] = config.profile_lock_state;
  payload[194] = config.vehicle_impact_state;
  payload[195] = config.host_command_rx;
  payload[196] = config.control_path;
  payload[197] = config.usb_backpressure_isolated;
  payload[198] = config.dtr_reset_sensitive;
  payload[199] = config.passive_acceptance_allowed;
  wr_u32_le(&payload[200], config.hardware_safety_case_id);
  wr_u32_le(&payload[204], config.bench_verification_id);
  for (uint8_t i = 0; i < 2; ++i) {
    const uint8_t offset = static_cast<uint8_t>(208 + i * 4);
    payload[offset + 0] = config.bus_mode[i];
    payload[offset + 1] = config.bus_ack_capability[i];
    payload[offset + 2] = config.bus_error_frame_capability[i];
    payload[offset + 3] = config.bus_transceiver_reset_safe[i];
  }
  payload[216] = config.usb_cdc_dtr_session_required;
  payload[217] = config.usb_cdc_dtr_session_only;

  if (!config.include_v6 || capacity < kCapabilityV6PayloadLen) {
    return kCapabilityV5PayloadLen;
  }

  payload[224] = 1;  // passive hardware evidence schema version.
  payload[225] = config.hardware_silent_strapped[0];
  payload[226] = config.hardware_silent_strapped[1];
  payload[227] = config.galvanic_isolated[0];
  payload[228] = config.galvanic_isolated[1];
  payload[229] = config.power_off_passive[0];
  payload[230] = config.power_off_passive[1];
  payload[231] = config.reset_safe[0];
  payload[232] = config.reset_safe[1];
  payload[233] = config.txd_gated[0];
  payload[234] = config.txd_gated[1];
  payload[235] = config.normal_enable_path_populated[0];
  payload[236] = config.normal_enable_path_populated[1];
  wr_u32_le(&payload[240], config.field_sku_id);
  wr_u32_le(&payload[244], config.external_analyzer_artifact_id);
  wr_u32_le(&payload[248], config.hotplug_pass_count);
  wr_u32_le(&payload[252], config.host_session_epoch);
  wr_u32_le(&payload[256], config.transport_epoch);
  wr_u32_le(&payload[260], config.usb_attach_quarantine_total);
  wr_u32_le(&payload[264], config.host_absent_gap_total);
  wr_u32_le(&payload[268], config.pre_session_payload_replay_total);

  return kCapabilityV6PayloadLen;
}

}  // namespace csm::board
