# CSM Firmware Architecture — HNO1 REV.B

Authority: `L2 ACTIVE ARCHITECTURE / BINDING IN IMPLEMENT`

machine-readable owner와 qualification 상태는 `ACTIVE_ARCHITECTURE.yaml`이 소유한다.

## Paired artifacts

| 역할 | PlatformIO environment |
|---|---|
| M7 semantic/data-plane product | `portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi` |
| M4 hard-RT control island + RC frontend | `portenta_h7_m4_remote_frontend` |

두 image는 같은 schema/wire/memory identity를 사용한다. 단독 image 교체나 이전 boot의
source/authority/transaction 재사용은 허용하지 않는다.

## Ownership

### Android VSM

- Service/HIL Host vehicle semantics와 최종 `0x005/0x007/0x364` 3×8 byte image
- coherent latest-state generation과 lease/session command
- 필요할 때 source-agnostic `HostControlNShot` transaction 요청
- physical cadence, retry, queue, CAN controller ownership 없음

### M7

- Host ingress/freshness/lease와 M4 RC mailbox 검증
- RC channel semantics, limiter, HNO1 payload mapping
- Host/RC 전역 단일 source authority와 coherent final snapshot
- canonical typed evidence, feeder bus, bounded USB/Wi-Fi sinks
- FDCAN1 register/pin/interrupt/static release ownership 없음

### M4 control island

- TIM4 5 ms base와 static slot table
- FDCAN1, dedicated Tx buffers `0/1/2`, terminal `TXBRP/TXBTO/TXBCF` truth
- hard-safety GPIO와 external watchdog
- latest snapshot depth 1, local publish-liveness timeout, bounded cancel
- raw CAN1 RX ring과 generic successful-TX N-shot budget

## Execution flow

```text
Android Host state ─┐
                    ├─> M7 global source authority ─> CRC32 2-slot final snapshot
R16SM -> M4 mailbox ┘                                  │
                                                      v
M4 hard-safety -> TIM4 static slots -> FDCAN1 dedicated buffers -> terminal truth
                                                      │
                       health/terminal counters <─────┤
                       bounded raw CAN1 ring <────────┘ -> M7 canonical evidence
```

Source는 image 전체로만 선택한다. lane별 Host/RC 혼합은 금지한다. authority 전환 시 old
pending을 cancel 요청하고 terminal closure를 확인한 뒤에만 새 source release를 시작한다.
transition overlap은 없으며 별도 sleep으로 공백을 만들지 않는다.

## Static schedule and latest state

- base slot: `5,000 us`
- slot 0: `0x005`, `0x007`, `0x364`
- slot 1/2/3: `0x005`
- dedicated Tx buffers: 각각 `0/1/2`
- Host/RC state depth: 1; 새 coherent image는 같은 authority의 latest value만 교체
- missed slot catch-up, replay, per-frame Host queue, hidden retry 없음
- constant payload는 stale이 아니다. M7 publish liveness와 value generation은 별도다.

N-shot은 continuous state와 분리된 transaction이다. `TXBTO` terminal만 successful count를
증가시키며 N번째 성공 직후 추가 release를 차단한다. cancelled/faulted transaction은
complete로 보고하지 않는다.

## D3 shared memory

| Range | Owner/use |
|---|---|
| `0x38000000..0x380003FF` | existing Remote IPC |
| `0x38000400..0x3800A7FF` | lwIP |
| `0x3800A800..0x3800B7FF` | control/health 2-slot IPC |
| `0x3800B800..0x3800F7FF` | 512-entry raw CAN1 ring |
| `0x3800F800..0x3800FBFF` | guard/unused |
| `0x3800FC00..0x3800FFFF` | PDM |

Control/health slot은 sequence, CRC32, boot identity와 fixed contract identity를 검증한다.
M4/M7 timestamp는 서로 빼지 않는다. M7 canonical RX timestamp는 M7 observation time이며
M4 local time은 M4-local deadline/liveness에만 사용한다.

## Failure containment and qualification

M4 hard inhibit, M7 publish stale, bus-off/error-passive, tracking ambiguity, IPC integrity failure는
physical release를 fail closed한다. telemetry/network/storage failure는 M4 slot execution을
막지 않는다. raw ring overflow는 explicit drop/high-water evidence이며 control backlog가
되지 않는다.

현재 CAN bitrate, M4↔M7 timeout, hard-safety polarity/reset, numeric NVIC priorities, RX drain,
bus-off recovery, jitter thresholds와 product authorization/traffic matrix는 HIL-frozen fact가
아니다. 따라서 qualification flags와 timeout은 `0`이며 product physical TX는 fail-closed다.
Build/test 성공은 device/HIL 또는 physical timing/ACK 증거가 아니다.
