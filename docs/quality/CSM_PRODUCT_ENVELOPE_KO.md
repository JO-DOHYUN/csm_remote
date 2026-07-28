# CSM Product Envelope

Updated: 2026-07-28

이 문서는 Portenta H7 + Feather RP2040 CAN feeder 제품의 계산·메모리·검증
gate다. 실행 가능한 계산 원본은
`firmware/csm/tools/product_envelope.py`이며, prose보다 우선한다.

## 계산 기준

| Gate | 계산값 | 판정 |
|---|---:|:---:|
| CAN RX 2,000 fps legacy → compact | 65,762 → 44,437 B/s | PASS |
| 2,000 fps 전체 제품 stream | 57,735 B/s | PASS(계산) |
| 기존 raw AP 저/고 실측 | 65,083 / 69,413 B/s | 참고 |
| 4,000 fps 전체 제품 stream | 102,172 B/s | GATE |
| 57,735 B/s × 1.02 s | 58,890 B | PASS |
| normal journal byte envelope | 63,408 B | PASS |
| Wi-Fi journal DTCM / usable DTCM | 90,096 / 130,408 B | PASS |

2,000 fps 계산은 현재 compact CAN schema 2, control evidence와 1 Hz product
health/reliability record를 포함한다. 기존 raw AP 실측은 용량 참고값이지
새 pinned build의 물리 통과 증거가 아니다. 4,000 fps는 현재 내부 Wi-Fi
release 목표가 아니며 USB truth path 또는 상위 transport gate가 필요하다.

## retained journal 용량

- descriptor: 1,024 × 24 B = 24,576 B
- encoded byte ring: 65,520 B
- 총 DTCM storage: 90,096 B
- critical reserve: 4 records / 2,112 B
- normal byte envelope: 63,408 B
- 2,000 fps 계산 부하에서 normal retention: 약 1.098 s
- 검증 계약 outage: 1.02 s, 58,890 B

Full 전 정상 record를 버려 더 오래 버티는 정책은 사용하지 않는다.
최초 admission failure가 곧 무결성 경계이며 epoch isolation과 명시적
counter/event를 발생시킨다.

## 제품 Mbed/lwIP profile

- ArduinoCore-mbed commit:
  `6816d442fd00bc17f83c73396d3d8d90285a6a8a`
- Mbed OS commit:
  `17dc3dc2e6e2817a8bd3df62f38583319f0e4fed`
- deterministic `libmbed.a` SHA-256:
  `032494298FC6CAFAAD23277B8CBEB01F1BA75CA7F72CCD90383A850EE561FD70`
- MSS 1,460; SND_BUF 11,680 B; TCP_WND 5,840 B; TCP_SEG 40;
  MEM_SIZE 40,960 B; PBUF_POOL_SIZE 5; TCP/IP stack 4,096 B;
  IPv4 on/IPv6 off; max sockets 4; WHD TX `PBUF_RAM`.

Archive, generated config와 application override SHA는
`third_party/mbed_portenta_product/artifact/PORTENTA_H7_M7/artifact-manifest.json`
에 고정한다.

## 메모리 ownership

| 영역 | 제품 ownership |
|---|---|
| D1 `0x24000000` | app/core initialized data, stack/heap |
| DTCM | 90,096 B retained Wi-Fi journal + CPU-only arenas |
| D2 `..0x30040000` | M4-owned physical window |
| D2 `0x30040000..0x30048000` | M7 network DMA/lwIP sections |
| D3 `0x38000400..0x3800A7FF` | pinned lwIP heap envelope |

Product linker는 heap object 40,979 B, D3 envelope 41,984 B, DTCM journal
90,096 B, D2 경계와 OpenAMP exclusion을 assert한다. MPU region 15 read-back이
실패하면 Wi-Fi는 fail disabled다. 구형 MCP/diagnostic 환경은 별도 standard
linker를 사용하므로 이 product-only 배치에 종속되지 않는다.

## 현재 artifact

- M7 product build: D1 RAM 169,632/523,624 B, flash
  364,856/786,432 B
- M7 firmware SHA-256:
  `59E646042B0FB312843A9D29DCBBB61CF75169979BA392CF9FA40BA86E9D33AF`
- M4 remote frontend: RAM 43,248/294,248 B, flash 73,928/1,048,576 B
- RP2040 feeder: RAM 59,732/262,144 B, flash 80,892/8,384,512 B

위 SHA는 현재 오프라인 candidate다. 실제 upload 직전에 source/manifest가
변하면 다시 산출한다.

## release table

각 실기 행은 input fps/bytes, canonical admitted B/s, socket sent B/s,
ACK reclaimed B/s, retained/unsent/high-water, first-not-admitted, ACK reject,
rewind, epoch/close, CRC/typed/segment/capture gap, source drop, main-loop
maximum gap와 worker stack floor를 함께 기록한다.

필수 행:

1. idle + PC durable consumer
2. idle + Android production consumer
3. 2,000 fps + USB + Wi-Fi
4. 2,000 fps + J4 RC + CAN0/CAN1 + USB + Wi-Fi
5. blocked client 1.02 s 이내 복구와 초과 failure boundary
6. reconnect, Android kill/restart와 capture recovery
7. capture open/append/fsync failure
8. 장시간 soak

모든 required integrity counter가 0이고 계산 conservation과 실측
`offered → admitted → sent → reclaimed → retained`가 일치해야 통과한다.
