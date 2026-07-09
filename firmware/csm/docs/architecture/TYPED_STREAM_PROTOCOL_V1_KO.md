# TYPED_STREAM_PROTOCOL_V1_KO

## Canonical Contract
이 문서는 설명용이다. 실제 binary wire contract의 단일 기준은
`shared/docs/TRANSPORT_AND_RECORDS_KO.md`다.

## Transport
- SOF: `0xA5 0x5A`
- header: `version`, `record_type`, `flags`, `seq u16_le`, `payload_len u16_le`
- payload: record-specific little-endian bytes
- trailer: `crc16_ccitt_le`
- recovery: SOF scan, length check, CRC check, dispatch

## Live Records
- `1 CAN_RX_RAW`
- `2 CAN_TX_RAW`
- `3 ENC_EDGE_RAW`
- `4 ENC_DERIVED`
- `5 ADC_SAMPLE`
- `6 CONTROL_ACK`
- `7 BOARD_EVENT`
- `8 BOARD_HEALTH`
- `9 CAPABILITY`
- `10 HOST_CAN_TX_REQUEST` host-to-board only
- `11 HOST_HEARTBEAT` host-to-board only
- `12 HOST_CONTROL_SESSION` host-to-board only
- `16 CAN_RX_SEGMENT`

## CAN_RX_SEGMENT
Current high-load dual-CAN CSM emits CAN RX as `CAN_RX_SEGMENT`. This is a
lossless packing record: one entry is one factual CAN frame with `capture_seq64`,
`mono_us`, `bus`, `can_id_flags`, `dlc_flags`, and `data[8]`. VMS must expand
every entry into the same truth path as `CAN_RX_RAW`.

## Compatibility
Legacy fixed 20-byte data is replay/import compatibility only. It must not be
mixed into the live typed stream parser.
