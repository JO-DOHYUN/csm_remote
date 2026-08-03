# CSM Product Envelope

Updated: 2026-08-03

이 문서는 Portenta H7 + Feather RP2040 CAN feeder 제품의 계산·메모리·검증
gate다. 2026-08-03 transient-envelope 구현은 host 계약, 제품 build, exact
artifact 업로드와 PC dual-sink 4,000 fps gate까지 통과했다. 아래 계산은 최종
동시부하 qualification 입력이며 그 자체가 전체 release PASS 주장은 아니다.

## 제품 데이터율 기준

| Gate | 계산 또는 기존 측정 | 현재 판정 |
|---|---:|:---:|
| CAN0+CAN1 aggregate 4,000 fps | 88,874 B/s | 계산 |
| enabled product stream exact | 111,922 B/s | 계산 |
| qualification minimum | 120,000 B/s | 실기 gate |
| design envelope | 135,000 B/s | 계산/실기 margin gate |
| 기존 raw AP 저/고 실측 | 65,083 / 69,413 B/s | 과거 참고 |

기존 raw AP 결과는 canonical product stream, Android Capture, RC/CAN/USB
동시부하를 검증하지 않는다. 특히 zero-progress window가 있었으므로 평균
처리량만으로 release margin을 주장하지 않는다.

## Wi-Fi live FIFO envelope

현재 승인 계약:

```text
descriptor capacity: 256 (normal 252, critical reserve 4)
encoded-byte capacity: 49,152 B (normal 47,040, critical reserve 2,112)
admission: single nonblocking offer
release: positive socket send 즉시
disconnect replay: 없음
application ACK: 없음
high/low pressure: 32,768/8,192 B and 192/64 records, diagnostic only
actual admission miss: close reason 6 + exact loss + flush
continuous no-positive-progress: 5,000 ms close reason 4
```

설계 envelope에서 250 ms 유입은 33,750 B/172 records다. 최대 encoded frame
523 B와 5 ms fallback 유입 675 B/4 records를 더한 `34,948 B/177 records`가
normal `47,040 B/252 records` 안에 들어옴을 생성 계산과 static assertion으로
고정한다. byte 기준 normal coverage는 약 348 ms다. 이는 scheduling transient
계약이며 sustained drain 부족이나 TCP 단절 보존 시간이 아니다.

descriptor는 compile-time 16 B이며 256개는 4,096 B다. 49,152 B encoded
ring과 합한 정적 DTCM storage는 53,248 B이고 linker assertion과
`sizeof(WifiTcpSink::TxStorage)` assertion이 이를 고정한다. 현재 link 계산상
DTCM 잔여는 77,152 B이며 linker가 최소 65,536 B를 강제한다.
reserve assertion은 Wi-Fi queue 끝이 아니라 최종 `__csm_dtcm_bss_end__`를
검사하므로 이후 추가되는 모든 명시적 DTCM arena도 이 여유를 침범할 수 없다.

high-water에서 normal byte reserve까지 14,272 B가 남아 최대 frame+fallback
1,198 B보다 크다. high-water는 close 조건이 아니다. 실제 reserve/full에서
처음 수락하지 못한 record만 exact loss/epoch close를 발생시키며, low-water
두 조건이 모두 만족될 때 pressure 관측 상태가 복구된다.

## USB live FIFO envelope

USB sink도 `FixedFrameByteQueue`를 사용하며 계약은 `192 descriptors / 40,960
encoded bytes`다. 135,000 B/s, 686 records/s의 250 ms 유입은 33,750 B/172
records이고 최대 encoded frame 하나를 포함한 요구량은 `34,273 B/173
records`다. descriptor 3,072 B와 byte arena를 합한 D1 정적 storage는 44,032
B다. 이 구조는 record마다 523 B를 고정 할당하지 않으면서 USB host scheduling
transient를 흡수하고, 실제 overflow는 독립 sink loss로 명시한다.

2026-08-03 final 60초 PC gate의 boot-cumulative USB high-water는 519 B였고 overflow,
`serial_enqueue_fail`, typed/segment/capture gap이 모두 0이었다. 같은 창에서
Wi-Fi도 두 source의 240,000 frame을 정확히 수신했고 CRC/gap/close/loss가 0이었다.
timestamp가 capture 순서에서 역행해도 segment base를 구간 최솟값으로 정하므로
재정렬 없이 241,627 frame을 10,673 segment에 담았다(평균 22.64).

## overflow와 복구 계약

queue full 또는 socket stall 시 다음 순서를 검증한다.

1. RC/CAN/canonical publisher/USB는 계속 진행한다.
2. Wi-Fi sink가 first/last dropped publish sequence와 누계를 고정한다.
3. 해당 TCP epoch만 close하고 FIFO와 partial frame을 flush한다.
4. 과거 record를 rewind/replay하지 않는다.
5. 새 연결은 현재 boot/full sequence의 fresh `STREAM_SESSION`부터 시작한다.
6. Android는 이전 segment를 GAP/PARTIAL로 닫고 최신 Live를 계속한다.

CSM은 Android Capture 상태나 파일 commit을 이 과정의 조건으로 사용하지
않는다.

## Mbed/lwIP candidate

2026-07-28 pinned profile은 재현 가능한 내부 Wi-Fi 실험 기준이며 최종 제품
승인이 아니다.

- ArduinoCore-mbed commit:
  `6816d442fd00bc17f83c73396d3d8d90285a6a8a`
- Mbed OS commit:
  `17dc3dc2e6e2817a8bd3df62f38583319f0e4fed`
- candidate `libmbed.a` SHA-256:
  `032494298FC6CAFAAD23277B8CBEB01F1BA75CA7F72CCD90383A850EE561FD70`

MSS/SND_BUF/TCP_WND/lwIP heap/D3 linker/MPU/WHD override는 live-first build의
실제 필요성, RAM 비용과 regression을 다시 판정한다. retained replay
용량을 만족시키기 위한 설정은 더 이상 제품 근거가 아니다.

## 메모리 ownership gate

새 build는 다음을 map과 runtime evidence로 함께 확인한다.

| 영역 | gate |
|---|---|
| D1 | app/core data + USB FIFO, static 211,448 B, heap span 311,816 B |
| DTCM | live FIFO와 CPU-only arena의 실제 사용량 |
| D2 | M4 window와 M7 network DMA section 비중첩 |
| D3 | lwIP heap actual size와 MPU attribute |

필수 evidence:

- section별 used/free와 linker assertion
- worker/main/M4 stack minimum free
- heap high-water와 장시간 growth 0
- cache/MPU read-back
- Wi-Fi 미시작 또는 반복 close 중 RC/CAN/USB deadline 영향 0

## release table

각 실기 행은 다음을 같은 시간창에서 기록한다.

```text
input fps와 canonical produced B/s
Wi-Fi offered/admitted/dropped/socket-sent/flushed B와 record
queue current/high-water records와 bytes
first/last dropped publish sequence
epoch/connect/disconnect/close reason
would-block/socket error/stall/maximum no-progress
USB/source/CAN drop와 canonical gap
main-loop maximum gap, control deadline miss, worker stack floor
Android received/applied gap와 Capture 독립 상태
```

필수 행:

1. boot/startup 중 Wi-Fi disabled/AP failure와 RC/CAN/USB 정상
2. idle + PC live consumer
3. idle + 실제 Android production consumer
4. 2,000 fps + USB + Wi-Fi
5. 2,000 fps + J4 RC + CAN0/CAN1 + USB + Wi-Fi
6. blocked client와 강제 queue overflow/socket stall
7. reconnect 100회: backlog 0, fresh session, 최신 Live 복구
8. Android Capture open/write/fsync/storage 실패: Live/TCP 지속
9. 1시간/8시간/24시간 soak

통과 조건은 각 측정창의 시작·종료 queue delta를 포함해
`offered = admitted + dropped`,
`admitted = socket-sent + flushed + queued-delta`가 일치하고, USB도 같은
방식으로 독립 보존되며, 모든 loss/close sequence 경계가 일치하는 것이다.
Wi-Fi drop은 허용된 fault injection에서만 명시적 GAP으로 인정하며 정상
steady-state에서는 0이어야 한다.

현재 내부 Wi-Fi 상태는 `IMPLEMENTED CANDIDATE / RELEASE BLOCKED`다.
