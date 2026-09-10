# Generated from CSM canonical contract. Offline codec, not live/control admission.
from dataclasses import dataclass
from typing import ClassVar
import struct
STAGE = {'begin': 1, 'complete': 2, 'staged': 3, 'replaced': 4, 'taken': 5, 'accepted': 6, 'rejected': 7, 'applied': 8, 'canceled': 9, 'closed': 10, 'lost': 11, 'snapshot': 12}
PRODUCER = {'m7_foreground': 1, 'm7_realtime_worker': 2, 'm7_socket_worker': 3, 'app_intent': 16, 'app_realtime_plane': 17, 'app_udp_send': 18, 'app_udp_receive': 19, 'app_control_send': 20, 'app_control_receive': 21, 'app_observer_receive': 22, 'app_truth_worker': 23, 'app_network_lifecycle': 24, 'app_capture_worker': 25, 'app_control_plane': 26}
CLOCK = {'m7_millis_u32': 1, 'android_elapsed_realtime_nanos': 2, 'android_system_nano_time': 3}
ADMISSION_FIELD = {'wire_sequence': 1, 'generation': 2, 'authority_epoch': 4, 'proof_reference': 8, 'rx_token': 16, 'source_epoch': 32, 'activation_epoch': 64, 'forward_age_ms': 128, 'proof_age_ms': 256}

MANIFEST = {'schema_sha256': 'b2cf35a8531d16a4fd1856101f7e02c7d110bf689bce403bd00c038ac083c34b', 'schema_version': 1, 'profile': 'service_hil_debug_only', 'runtime_coverage': 'NOT_PROVEN', 'sinks': ['usb_debug', 'android_local_file'], 'record_type': 30, 'header_bytes': 72, 'header': [{'name': 'schema_version', 'type': 'u8', 'offset': 0}, {'name': 'producer', 'type': 'u8', 'offset': 1}, {'name': 'clock', 'type': 'u8', 'offset': 2}, {'name': 'reserved', 'type': 'u8', 'offset': 3}, {'name': 'endian_tag', 'type': 'u32', 'offset': 4}, {'name': 'trace_sequence', 'type': 'u32', 'offset': 8}, {'name': 'epoch', 'type': 'u64', 'offset': 12}, {'name': 'boot_id', 'type': 'u64', 'offset': 20}, {'name': 'ticks', 'type': 'u64', 'offset': 28}, {'name': 'event_id', 'type': 'u16', 'offset': 36}, {'name': 'body_length', 'type': 'u16', 'offset': 38}, {'name': 'schema_sha256', 'type': 'bytes32', 'offset': 40}], 'typed_frame': {'sof0': 165, 'sof1': 90, 'version': 1, 'max_payload': 512}, 'producers': [{'id': 1, 'name': 'm7_foreground', 'clocks': [1]}, {'id': 2, 'name': 'm7_realtime_worker', 'clocks': [1]}, {'id': 3, 'name': 'm7_socket_worker', 'clocks': [1]}, {'id': 16, 'name': 'app_intent', 'clocks': [3]}, {'id': 17, 'name': 'app_realtime_plane', 'clocks': [3]}, {'id': 18, 'name': 'app_udp_send', 'clocks': [2]}, {'id': 19, 'name': 'app_udp_receive', 'clocks': [2]}, {'id': 20, 'name': 'app_control_send', 'clocks': [2]}, {'id': 21, 'name': 'app_control_receive', 'clocks': [2]}, {'id': 22, 'name': 'app_observer_receive', 'clocks': [2]}, {'id': 23, 'name': 'app_truth_worker', 'clocks': [3]}, {'id': 24, 'name': 'app_network_lifecycle', 'clocks': [2]}, {'id': 25, 'name': 'app_capture_worker', 'clocks': [3]}, {'id': 26, 'name': 'app_control_plane', 'clocks': [3]}], 'clocks': [{'id': 1, 'name': 'm7_millis_u32', 'unit': 'ms', 'width_bits': 32}, {'id': 2, 'name': 'android_elapsed_realtime_nanos', 'unit': 'ns', 'width_bits': 64}, {'id': 3, 'name': 'android_system_nano_time', 'unit': 'ns', 'width_bits': 64}], 'stages': {'begin': 1, 'complete': 2, 'staged': 3, 'replaced': 4, 'taken': 5, 'accepted': 6, 'rejected': 7, 'applied': 8, 'canceled': 9, 'closed': 10, 'lost': 11, 'snapshot': 12}, 'admission_valid_fields': {'wire_sequence': 1, 'generation': 2, 'authority_epoch': 4, 'proof_reference': 8, 'rx_token': 16, 'source_epoch': 32, 'activation_epoch': 64, 'forward_age_ms': 128, 'proof_age_ms': 256}, 'events': [{'id': 1, 'name': 'Intent', 'class': 'DEBUG_TRACE', 'producers': [16], 'fields': [{'name': 'generation', 'type': 'u32', 'unit': 'identity', 'offset': 0, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'authority_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 4, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'drive', 'type': 'i32', 'unit': 'raw', 'offset': 8, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'steering', 'type': 'i32', 'unit': 'raw', 'offset': 12, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'ehb', 'type': 'u32', 'unit': 'raw', 'offset': 16, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'lane_mask', 'type': 'u8', 'unit': 'raw', 'offset': 20, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}], 'relation': 'User intent produces latest coherent generation; replacement is explicit, not one packet per UI event.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 21}, {'id': 2, 'name': 'DatagramIo', 'class': 'DEBUG_TRACE', 'producers': [2, 18, 19], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'operation_id', 'type': 'u32', 'unit': 'identity', 'offset': 1, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'wire_sequence', 'type': 'u32', 'unit': 'identity', 'offset': 5, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'generation', 'type': 'u32', 'unit': 'identity', 'offset': 9, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'authority_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 13, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'proof_reference', 'type': 'u32', 'unit': 'raw', 'offset': 17, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'rx_token', 'type': 'u32', 'unit': 'identity', 'offset': 21, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'length', 'type': 'u16', 'unit': 'bytes', 'offset': 25, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'result', 'type': 'i32', 'unit': 'native_result', 'offset': 27, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'peer_ipv4', 'type': 'u32', 'unit': 'raw', 'offset': 31, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'peer_port', 'type': 'u16', 'unit': 'raw', 'offset': 35, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}], 'relation': 'Begin/complete matched by producer, epoch, operation_id; syscall success does not imply remote RX.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 37}, {'id': 3, 'name': 'DatagramBytes', 'class': 'DEBUG_TRACE', 'producers': [2, 18, 19], 'fields': [{'name': 'operation_id', 'type': 'u32', 'unit': 'identity', 'offset': 0, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'wire_sequence', 'type': 'u32', 'unit': 'identity', 'offset': 4, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'rx_token', 'type': 'u32', 'unit': 'identity', 'offset': 8, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'wire_length', 'type': 'u16', 'unit': 'bytes', 'offset': 12, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'captured_length', 'type': 'u16', 'unit': 'bytes', 'offset': 14, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'data', 'type': 'bytes128', 'unit': 'raw', 'offset': 16, 'bytes': 128, 'cpp': 'bytes', 'kotlin': 'ByteArray', 'fmt': '128s'}], 'relation': 'Exact bounded datagram bytes join Io by producer/epoch/operation_id; captured_length < wire_length is incomplete.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 144}, {'id': 4, 'name': 'Admission', 'class': 'DEBUG_TRACE', 'producers': [1, 2, 17, 23], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'reason', 'type': 'u32', 'unit': 'raw', 'offset': 1, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'wire_sequence', 'type': 'u32', 'unit': 'identity', 'offset': 5, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'generation', 'type': 'u32', 'unit': 'identity', 'offset': 9, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'authority_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 13, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'proof_reference', 'type': 'u32', 'unit': 'raw', 'offset': 17, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'rx_token', 'type': 'u32', 'unit': 'identity', 'offset': 21, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'source_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 25, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'activation_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 29, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'forward_age_ms', 'type': 'u32', 'unit': 'ms', 'offset': 33, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'proof_age_ms', 'type': 'u32', 'unit': 'ms', 'offset': 37, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'valid_fields', 'type': 'u32', 'unit': 'identity', 'offset': 41, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}], 'relation': 'Existing admission outcome only; token links latest replacement/take, epochs never confer permission.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 45}, {'id': 5, 'name': 'Proof', 'class': 'DEBUG_TRACE', 'producers': [1, 2, 17, 19], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'sequence', 'type': 'u32', 'unit': 'identity', 'offset': 1, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'admitted_sequence', 'type': 'u32', 'unit': 'identity', 'offset': 5, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'generation', 'type': 'u32', 'unit': 'identity', 'offset': 9, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'authority_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 13, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'rx_token', 'type': 'u32', 'unit': 'identity', 'offset': 17, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'status', 'type': 'u32', 'unit': 'raw', 'offset': 21, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'reason', 'type': 'u32', 'unit': 'raw', 'offset': 25, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'flags', 'type': 'u32', 'unit': 'raw', 'offset': 29, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}], 'relation': 'Cumulative latest proof; staged, selected, sent, socket RX, parsed and applied are distinct.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 33}, {'id': 6, 'name': 'Transaction', 'class': 'DEBUG_TRACE', 'producers': [1, 3, 20, 21, 16, 26], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'command_id', 'type': 'u32', 'unit': 'identity', 'offset': 1, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'transaction_id', 'type': 'u32', 'unit': 'identity', 'offset': 5, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'record_type', 'type': 'u16', 'unit': 'raw', 'offset': 9, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'result', 'type': 'i32', 'unit': 'native_result', 'offset': 11, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'authority_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 15, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'generation', 'type': 'u32', 'unit': 'identity', 'offset': 19, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'reason', 'type': 'u32', 'unit': 'raw', 'offset': 23, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}], 'relation': 'Transaction application is not ACK staging or TCP acceptance; old epochs never joined.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 27}, {'id': 7, 'name': 'Stream', 'class': 'DEBUG_TRACE', 'producers': [1, 3, 20, 21, 22, 23, 25, 26], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'channel', 'type': 'u16', 'unit': 'raw', 'offset': 1, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'operation_id', 'type': 'u32', 'unit': 'identity', 'offset': 3, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'publish_sequence', 'type': 'u64', 'unit': 'identity', 'offset': 7, 'bytes': 8, 'cpp': 'uint64_t', 'kotlin': 'ULong', 'fmt': 'Q'}, {'name': 'byte_start', 'type': 'u64', 'unit': 'raw', 'offset': 15, 'bytes': 8, 'cpp': 'uint64_t', 'kotlin': 'ULong', 'fmt': 'Q'}, {'name': 'length', 'type': 'u32', 'unit': 'bytes', 'offset': 23, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'result', 'type': 'i32', 'unit': 'native_result', 'offset': 27, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'record_type', 'type': 'u16', 'unit': 'raw', 'offset': 31, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'reason', 'type': 'u32', 'unit': 'raw', 'offset': 33, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}], 'relation': 'TCP byte ranges frame independently of packet segmentation; sink admission, partial sends and parser results are distinct.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 37}, {'id': 8, 'name': 'Resource', 'class': 'DEBUG_TRACE', 'producers': [3, 24], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'resource_id', 'type': 'u64', 'unit': 'identity', 'offset': 1, 'bytes': 8, 'cpp': 'uint64_t', 'kotlin': 'ULong', 'fmt': 'Q'}, {'name': 'resource_kind', 'type': 'u16', 'unit': 'raw', 'offset': 9, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'operation_id', 'type': 'u32', 'unit': 'identity', 'offset': 11, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'result', 'type': 'i32', 'unit': 'native_result', 'offset': 15, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'owner_epoch', 'type': 'u64', 'unit': 'identity', 'offset': 19, 'bytes': 8, 'cpp': 'uint64_t', 'kotlin': 'ULong', 'fmt': 'Q'}], 'relation': 'Acquisition and cleanup ownership follow existing network/session epoch; no new lease authority.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 27}, {'id': 9, 'name': 'Capacity', 'class': 'DEBUG_TRACE', 'producers': [1, 2, 3, 17, 24, 25], 'fields': [{'name': 'resource_id', 'type': 'u32', 'unit': 'identity', 'offset': 0, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'used', 'type': 'u32', 'unit': 'raw', 'offset': 4, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'high_water', 'type': 'u32', 'unit': 'raw', 'offset': 8, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'failures', 'type': 'u32', 'unit': 'raw', 'offset': 12, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'service_gap_ticks', 'type': 'u64', 'unit': 'clock_ticks', 'offset': 16, 'bytes': 8, 'cpp': 'uint64_t', 'kotlin': 'ULong', 'fmt': 'Q'}], 'relation': 'Measured counters only; resource-specific units and native wrap are specified by binding, not a safety gate.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 24}, {'id': 10, 'name': 'TraceLoss', 'class': 'DEBUG_TRACE', 'producers': [1, 2, 3, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26], 'fields': [{'name': 'first_missing_sequence', 'type': 'u32', 'unit': 'identity', 'offset': 0, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'last_missing_sequence', 'type': 'u32', 'unit': 'identity', 'offset': 4, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'dropped', 'type': 'u32', 'unit': 'raw', 'offset': 8, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'reason', 'type': 'u16', 'unit': 'raw', 'offset': 12, 'bytes': 2, 'cpp': 'uint16_t', 'kotlin': 'Int', 'fmt': 'H'}, {'name': 'high_water', 'type': 'u32', 'unit': 'raw', 'offset': 14, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}], 'relation': 'Diagnostic loss invalidates affected evidence; no control action or replay. Range valid within one producer session.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 18}, {'id': 11, 'name': 'IntentState', 'class': 'DEBUG_TRACE', 'producers': [16], 'fields': [{'name': 'stage', 'type': 'u8', 'unit': 'raw', 'offset': 0, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'revision', 'type': 'u64', 'unit': 'identity', 'offset': 1, 'bytes': 8, 'cpp': 'uint64_t', 'kotlin': 'ULong', 'fmt': 'Q'}, {'name': 'generation', 'type': 'u32', 'unit': 'identity', 'offset': 9, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'authority_epoch', 'type': 'u32', 'unit': 'identity', 'offset': 13, 'bytes': 4, 'cpp': 'uint32_t', 'kotlin': 'Long', 'fmt': 'I'}, {'name': 'drive', 'type': 'i32', 'unit': 'permille', 'offset': 17, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'steering', 'type': 'i32', 'unit': 'permille', 'offset': 21, 'bytes': 4, 'cpp': 'int32_t', 'kotlin': 'Int', 'fmt': 'i'}, {'name': 'lane_mask', 'type': 'u8', 'unit': 'raw', 'offset': 25, 'bytes': 1, 'cpp': 'uint8_t', 'kotlin': 'Int', 'fmt': 'B'}, {'name': 'data005', 'type': 'bytes8', 'unit': 'raw', 'offset': 26, 'bytes': 8, 'cpp': 'bytes', 'kotlin': 'ByteArray', 'fmt': '8s'}, {'name': 'data007', 'type': 'bytes8', 'unit': 'raw', 'offset': 34, 'bytes': 8, 'cpp': 'bytes', 'kotlin': 'ByteArray', 'fmt': '8s'}, {'name': 'data364', 'type': 'bytes8', 'unit': 'raw', 'offset': 42, 'bytes': 8, 'cpp': 'bytes', 'kotlin': 'ByteArray', 'fmt': '8s'}], 'relation': 'BEGIN is UI semantic revision: generation=0 and data bytes unavailable (zero padding, never neutral). STAGED/REJECTED links the revision captured with the selected image to the exact attempted wire generation and bytes. Revision is not generation; multiple UI updates may coalesce, multiple staged generations may share a revision. SNAPSHOT is initial/safe staging with no claimed user causal revision. Existing producer session scopes revision; no runtime authority is added.', 'authority_ref': 'docs/architecture/NETWORK_TRANSPORT_ARCHITECTURE_KO.md', 'body_bytes': 50}]}
EVENTS = {event['id']: event for event in MANIFEST['events']}
PRODUCERS = {item['id']: item['clocks'] for item in MANIFEST['producers']}
DIGEST = bytes.fromhex('b2cf35a8531d16a4fd1856101f7e02c7d110bf689bce403bd00c038ac083c34b')

@dataclass(frozen=True)
class Meta:
    producer: int
    clock: int
    trace_sequence: int
    epoch: int
    boot_id: int
    ticks: int

@dataclass(frozen=True)
class Intent:
    id: ClassVar[int] = 1
    generation: int
    authority_epoch: int
    drive: int
    steering: int
    ehb: int
    lane_mask: int

@dataclass(frozen=True)
class DatagramIo:
    id: ClassVar[int] = 2
    stage: int
    operation_id: int
    wire_sequence: int
    generation: int
    authority_epoch: int
    proof_reference: int
    rx_token: int
    length: int
    result: int
    peer_ipv4: int
    peer_port: int

@dataclass(frozen=True)
class DatagramBytes:
    id: ClassVar[int] = 3
    operation_id: int
    wire_sequence: int
    rx_token: int
    wire_length: int
    captured_length: int
    data: bytes

@dataclass(frozen=True)
class Admission:
    id: ClassVar[int] = 4
    stage: int
    reason: int
    wire_sequence: int
    generation: int
    authority_epoch: int
    proof_reference: int
    rx_token: int
    source_epoch: int
    activation_epoch: int
    forward_age_ms: int
    proof_age_ms: int
    valid_fields: int

@dataclass(frozen=True)
class Proof:
    id: ClassVar[int] = 5
    stage: int
    sequence: int
    admitted_sequence: int
    generation: int
    authority_epoch: int
    rx_token: int
    status: int
    reason: int
    flags: int

@dataclass(frozen=True)
class Transaction:
    id: ClassVar[int] = 6
    stage: int
    command_id: int
    transaction_id: int
    record_type: int
    result: int
    authority_epoch: int
    generation: int
    reason: int

@dataclass(frozen=True)
class Stream:
    id: ClassVar[int] = 7
    stage: int
    channel: int
    operation_id: int
    publish_sequence: int
    byte_start: int
    length: int
    result: int
    record_type: int
    reason: int

@dataclass(frozen=True)
class Resource:
    id: ClassVar[int] = 8
    stage: int
    resource_id: int
    resource_kind: int
    operation_id: int
    result: int
    owner_epoch: int

@dataclass(frozen=True)
class Capacity:
    id: ClassVar[int] = 9
    resource_id: int
    used: int
    high_water: int
    failures: int
    service_gap_ticks: int

@dataclass(frozen=True)
class TraceLoss:
    id: ClassVar[int] = 10
    first_missing_sequence: int
    last_missing_sequence: int
    dropped: int
    reason: int
    high_water: int

@dataclass(frozen=True)
class IntentState:
    id: ClassVar[int] = 11
    stage: int
    revision: int
    generation: int
    authority_epoch: int
    drive: int
    steering: int
    lane_mask: int
    data005: bytes
    data007: bytes
    data364: bytes

CLASSES = {key: globals()[value] for key, value in {1: 'Intent', 2: 'DatagramIo', 3: 'DatagramBytes', 4: 'Admission', 5: 'Proof', 6: 'Transaction', 7: 'Stream', 8: 'Resource', 9: 'Capacity', 10: 'TraceLoss', 11: 'IntentState'}.items()}


def crc16(data):
    value=0xffff
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 0xffff
    return value


def _meta(meta, event):
    if meta.producer not in event['producers'] or meta.clock not in PRODUCERS.get(meta.producer, []):
        raise ValueError('producer/clock mismatch')
    if meta.clock == 1 and not 0 <= meta.ticks <= 0xffffffff:
        raise ValueError('M7 clock is raw u32 milliseconds')


def encode(meta, event):
    definition = EVENTS.get(event.id)
    if definition is None or type(event) is not CLASSES[event.id]:
        raise ValueError('unknown event')
    _meta(meta, definition)
    values = []
    for field in definition['fields']:
        value = getattr(event, field['name'])
        if field['type'].startswith('bytes'):
            if not isinstance(value, bytes) or len(value) != field['bytes']:
                raise ValueError('fixed byte field length')
        elif type(value) is not int:
            raise ValueError('integer field required')
        values.append(value)
    try:
        body = struct.pack('<'+''.join(f['fmt'] for f in definition['fields']), *values)
        payload=struct.pack('<BBBBIIQQQHH32s',1,meta.producer,meta.clock,0,0x01020304,
                            meta.trace_sequence,meta.epoch,meta.boot_id,meta.ticks,event.id,len(body),DIGEST)+body
        raw=struct.pack('<BBBBBHH',165,90,1,30,0,meta.trace_sequence&0xffff,len(payload))+payload
    except struct.error as error:
        raise ValueError(str(error)) from error
    return raw+struct.pack('<H',crc16(raw[2:]))


def decode(raw):
    if len(raw)<83 or len(raw)>523:
        raise ValueError('frame length')
    sof0,sof1,version,record,flags,sequence,length=struct.unpack_from('<BBBBBHH',raw)
    if (sof0,sof1,version,record,flags)!=(165,90,1,30,0) or length!=len(raw)-11:
        raise ValueError('typed frame/header mismatch')
    if struct.unpack_from('<H',raw,len(raw)-2)[0]!=crc16(raw[2:-2]):
        raise ValueError('CRC')
    v,p,c,reserved,tag,seq,epoch,boot,ticks,event_id,body_len,digest=struct.unpack_from('<BBBBIIQQQHH32s',raw,9)
    if v!=1 or reserved!=0 or tag!=0x01020304 or digest!=DIGEST or sequence!=(seq&0xffff):
        raise ValueError('schema/endian/sequence mismatch')
    event=EVENTS.get(event_id)
    if event is None or body_len!=event['body_bytes'] or len(raw)!=83+body_len:
        raise ValueError('event ID/body length')
    meta=Meta(p,c,seq,epoch,boot,ticks)
    _meta(meta,event)
    values=struct.unpack_from('<'+''.join(f['fmt'] for f in event['fields']),raw,81)
    return meta,CLASSES[event_id](*values)
