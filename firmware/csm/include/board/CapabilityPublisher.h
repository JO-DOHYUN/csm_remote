#pragma once

#include <stdint.h>

namespace csm::board {

struct CapabilityBusDescriptor {
  uint8_t bus_id = 0;
  uint8_t role = 0;  // role_hint only; VSM must not treat this as authoritative.
  uint8_t backend = 0;
  uint8_t transceiver = 0;
  uint8_t rx_supported = 0;
  uint8_t tx_supported = 0;
  uint8_t control_tx_allowed = 0;
  uint8_t classic_can_supported = 1;
  uint8_t can_fd_supported = 0;
  uint8_t max_live_dlc = 8;
  uint32_t nominal_bitrate = 500000;
  uint32_t data_bitrate = 0;
  uint8_t termination_policy = 0;
  uint8_t isolation_policy = 0;
};

struct CapabilityPayloadConfig {
  uint8_t profile_major = 1;
  uint8_t profile_minor = 0;
  uint32_t can_queue_size = 0;
  uint32_t encoder_ppr = 0;
  uint32_t encoder_frequency_limit_hz = 0;
  bool adc_sample_supported = false;
  uint8_t adc_channel_count = 0;
  uint8_t adc_resolution_bits = 0;
  uint8_t adc_sample_period_ms = 0;
  uint8_t lane_capability_flags = 0;
  uint8_t limitation_flags = 0;
  bool include_v2 = false;
  bool include_v3 = false;
  bool include_v4 = false;
  bool include_v5 = false;
  bool include_v6 = false;
  uint8_t bus_count = 0;
  uint16_t capability_v2_flags = 0;
  uint32_t supported_uplink_records = 0;
  uint32_t supported_downlink_records = 0;
  uint32_t safety_feature_flags = 0;
  uint32_t policy_hash = 0;
  uint32_t firmware_build_id = 0;
  uint16_t host_tx_queue_size = 0;
  uint16_t capability_v3_flags = 0;
  uint8_t firmware_identity_version = 0;
  bool firmware_dirty = true;
  uint8_t firmware_irq_mode = 0;
  uint32_t firmware_build_epoch = 0;
  uint32_t mcp_spi_hz = 0;
  uint16_t can_record_drain_budget = 0;
  uint16_t serial_ring_kib = 0;
  const char* firmware_git_sha = "unknown";
  const char* firmware_env_name = "unknown";
  uint8_t firmware_profile = 0;
  uint8_t profile_lock_state = 0;
  uint8_t vehicle_impact_state = 0;
  uint8_t host_command_rx = 0;
  uint8_t control_path = 0;
  uint8_t usb_backpressure_isolated = 0;
  uint8_t dtr_reset_sensitive = 0;
  uint8_t passive_acceptance_allowed = 0;
  uint8_t usb_cdc_dtr_session_required = 0;
  uint8_t usb_cdc_dtr_session_only = 0;
  uint32_t hardware_safety_case_id = 0;
  uint32_t bench_verification_id = 0;
  uint8_t bus_mode[2] = {};
  uint8_t bus_ack_capability[2] = {};
  uint8_t bus_error_frame_capability[2] = {};
  uint8_t bus_transceiver_reset_safe[2] = {};
  uint8_t hardware_silent_strapped[2] = {};
  uint8_t galvanic_isolated[2] = {};
  uint8_t power_off_passive[2] = {};
  uint8_t reset_safe[2] = {};
  uint8_t txd_gated[2] = {};
  uint8_t normal_enable_path_populated[2] = {};
  uint32_t field_sku_id = 0;
  uint32_t external_analyzer_artifact_id = 0;
  uint32_t hotplug_pass_count = 0;
  uint32_t host_session_epoch = 0;
  uint32_t transport_epoch = 0;
  uint32_t usb_attach_quarantine_total = 0;
  uint32_t host_absent_gap_total = 0;
  uint32_t pre_session_payload_replay_total = 0;
  CapabilityBusDescriptor buses[2] = {};
};

uint16_t build_capability_payload(const CapabilityPayloadConfig& config,
                                  uint8_t* payload,
                                  uint16_t capacity);

}  // namespace csm::board
