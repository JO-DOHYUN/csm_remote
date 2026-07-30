# CSM Product Envelope

Updated: 2026-07-30

이 문서는 Portenta H7 + Feather RP2040 CAN feeder 제품의 계산·메모리·검증
gate다. live-first 구현, host 계약, 제품 build, COM7 upload와 짧은 PC live
gate는 통과했다. 아래 계산은 최종 동시부하 qualification 입력이며 그 자체가
release PASS 주장은 아니다.

## 제품 데이터율 기준

| Gate | 계산 또는 기존 측정 | 현재 판정 |
|---|---:|:---:|
| CAN RX 2,000 fps legacy → compact | 65,762 → 44,437 B/s | 계산 |
| 2,000 fps 전체 제품 stream | 57,735 B/s | 동시 HIL 필요 |
| 기존 raw AP 저/고 실측 | 65,083 / 69,413 B/s | 과거 참고 |
| 4,000 fps 전체 제품 stream | 102,172 B/s | 제품 목표 아님 |

기존 raw AP 결과는 canonical product stream, Android Capture, RC/CAN/USB
동시부하를 검증하지 않는다. 특히 zero-progress window가 있었으므로 평균
처리량만으로 release margin을 주장하지 않는다.

## Wi-Fi live FIFO envelope

현재 승인 계약:

```text
record capacity: 128
encoded-byte capacity: 8,192 B
admission: single nonblocking offer
release: positive socket send 즉시
disconnect replay: 없음
application ACK: 없음
overflow/stall: close + flush + fresh STREAM_SESSION
```

8,192 B는 57,735 B/s 생산량의 약 142 ms에 해당하지만 outage 보존 시간이
아니다. record와 byte 한계 중 먼저 닿는 경계가 적용된다. queue는 짧은
scheduling jitter만 흡수하며 실제 TCP 단절 구간은 손실로 종료한다.

descriptor는 compile-time 16 B이며 128개는 2,048 B다. 8,192 B encoded
ring과 합한 정적 DTCM storage는 10,240 B이고 linker assertion과
`sizeof(WifiTcpSink::TxStorage)` assertion이 이를 고정한다. 기존 90,096 B
retained journal 대비 79,856 B를 회수하지만, 전체 section 사용량과 여유는
최종 build map으로 다시 확인한다.

64% pressure 경계는 5,243 B다. normal admission 6,080 B까지 남는 837 B가
최대 encoded frame 523 B와 5 ms fallback 동안의 계산 유입 289 B 합계
812 B보다 크다는 것을 source-backed 계산과 static assertion으로 고정한다.

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
| D1 | app/core data, stack/heap headroom |
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
