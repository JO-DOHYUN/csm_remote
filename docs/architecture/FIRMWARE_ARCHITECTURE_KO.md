# CSM Firmware Architecture — HNO1 REV.B

Authority: `L2 ACTIVE ARCHITECTURE / BINDING IN IMPLEMENT`

machine-readable owner와 qualification 상태는 `ACTIVE_ARCHITECTURE.yaml`이 소유한다.

## Paired artifacts

| 역할 | PlatformIO environment |
|---|---|
| M7 semantic/data-plane product | `portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi` |
| M4 RT control island + RC frontend | `portenta_h7_m4_remote_frontend` |

두 image는 같은 schema/wire/memory identity를 사용한다. 단독 image 교체나 이전 boot의
source/authority/transaction 재사용은 허용하지 않는다.

## Ownership

### Android VSM

- Service/HIL Host vehicle semantics와 최종 `0x005/0x007/0x364` 3×8 byte image
- coherent latest-state generation과 lease/session command
- 필요할 때 source-agnostic `HostControlNShot` transaction 요청
- physical cadence, retry, queue, CAN controller ownership 없음

### M7

- 독립 Host control TCP `3334`의 heartbeat causal-ACK proof/ARM/lease와 전역 consumed command-ID 검증, M4 RC mailbox 검증
- RC channel semantics, limiter, HNO1 payload mapping
- Host/RC 전역 단일 source authority와 coherent final snapshot
- canonical typed evidence, feeder bus, bounded USB/Wi-Fi sinks
- FDCAN1 register/pin/interrupt/static release ownership 없음
- D3 Control/Remote IPC를 boot epoch당 한 번 초기화한 뒤 RC/Wi-Fi 결과와 무관하게
  M4를 부팅한다. 독립 Control-Island coordinator가 health/Host session/RC candidate/
  global selection/permit/final snapshot을 계속 서비스한다.

### M4 control island

- TIM4 5 ms base와 static slot table
- FDCAN1, dedicated Tx buffers `0/1/2`, terminal `TXBRP/TXBTO/TXBCF` truth
- latest snapshot depth 1, 300 ms local publish-liveness timeout, bounded cancel
- raw CAN1 RX ring과 generic successful-TX N-shot budget
- mutable executor state의 유일 writer. foreground는 검증된 latest snapshot/event 하나만
  stage하고 FDCAN IRQ는 terminal/error bit만 latch한다. 모든 transition/release/health counter
  갱신은 TIM4가 소비한 뒤 수행하며 health publish read는 상태를 바꾸지 않는다.

## Execution flow

```text
Android Host state ─┐
                    ├─> M7 global source authority ─> CRC32 2-slot final snapshot
R16SM -> M4 mailbox ┘                                  │
                                                      v
M4 -> TIM4 static slots -> FDCAN1 dedicated buffers -> terminal truth
                                                      │
                       health/terminal counters <─────┤
                       bounded raw CAN1 ring <────────┘ -> M7 canonical evidence
```

Host control transport는 bulk canonical telemetry와 분리된다. TCP `3334`는 fresh
`STREAM_SESSION` anchor 뒤 Host 명령과 `CONTROL_ACK`만 전달하고 자체 connection epoch와
publish sequence를 가진다. 16×64-byte bounded ACK FIFO가 full이거나 300 ms 동안 positive
send progress가 없으면 해당 control epoch를 닫고 old bytes를 폐기한다. TCP `3333`의
canonical `CONTROL_ACK` mirror는 capture/diagnostic evidence이며 Host causal proof를 열지
않는다. 두 socket은 같은 bounded nonblocking Wi-Fi worker를 사용하되 매 turn control을 먼저
서비스한다. `3333` downlink는 observer `HOST_QUERY_CAPABILITY`만 허용하고 motion/session
record는 dispatch하지 않는다. reconnect replay와 telemetry backlog에 의한 control gating은 없다.

Source 우선순위는 `RC > Host > None`이며 이 active profile에는 autonomy source가 없다.
RC source admission은 R16SM profile의 `0xC8` address, CRC, 지원 RC frame(`0x16`, bounded
`0x17`), CH4 drive/CH2 steering calibration, 연속 3개 fresh frame을 요구한다. Link Statistics
`0x14/0x1C/0x1D`는 receiver가 보내지 않으면 admission 필수가 아니고, 한 번 관측된 receiver에서는
LQ=0 또는 500 ms stale이 추가 veto다. Optional CH5/CH10/CH11 부재·범위 오류는 해당 기능만
disable하며 RC 전체를 폐기하지 않는다. 상세 electrical/radio 조건은
`R16SM_RECEIVER_PROFILE_KO.md`가 소유한다. Source는 image 전체로만 선택한다. Host는 `005/007/364`, RC는 `005/007`만 소유하며 RC가
소유하지 않는 `364`는 M4 `SuppressTx`로 해석한다. source별 lane을 다른 source의 old image와
혼합하지 않는다. authority 전환 시 old ACTIVE pending만 cancel 요청하고 terminal closure를
확인한 뒤 새 source release를 시작한다. SAFE pending은 source transition과 무관하게 보존한다.
transition overlap은 없으며 별도 sleep으로 공백을 만들지 않는다.

## Static schedule and latest state

- base slot: `5,000 us`
- slot 0: `0x005`, `0x007`, `0x364`
- slot 1/2/3: `0x005`
- dedicated Tx buffers: 각각 `0/1/2`
- Host/RC state depth: 1; 새 coherent image는 같은 source의 latest value만 교체
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

M4는 `physical transport readiness`, `ACTIVE motion permission`, `safe-wire fallback`을
분리한다. source/lease/ARM/M7 freshness/permit 상실은 즉시 ACTIVE를 revoke하고 old motion을
재사용하지 않는다. transport가 healthy이면 M4는 frozen per-lane policy만 실행한다:
`0x005` safe는 `AA 02 00 00 00 00 00 00`, `0x007` safe는 `82 00 00 00 00 00 00 00`,
`0x364`는 `SuppressTx`다. M4는 vehicle semantic을 계산하거나 unspecified byte를 zero-fill하지
않는다. FDCAN은 500 kbps로 정상 시작하며 evidence qualification은 runtime permission이 아니다.

error-passive 또는 unrecoverable tracking fault는 ACTIVE를 globally revoke하고 other ACTIVE
pending lane을 bounded-cancel한다. error-passive에서도 viable physical request에는 SAFE cyclic을
유지한다. bus-off/fatal은 physical TX를 latch-stop하며 reset 이후 SAFE로만 복귀한다. fresh coherent
source와 더 새로운 activation epoch의 explicit re-ARM 없이는 ACTIVE를 자동 재개하지 않는다.
source epoch와 activation epoch는 분리되므로 같은 Host source의 명시 re-ARM도 가능하다. telemetry/network/storage
failure는 M4 slot execution을 막지 않는다. raw ring overflow는 explicit drop/high-water evidence이며
control backlog가 되지 않는다.

transport/timing, M4↔M7 liveness, safe-wire contract qualification은 측정 evidence이며 runtime
permission이 아니다. 초기 M4→M7 freshness timeout은 300 ms이고 publish age/max gap/timeout count를
보고한다.
Build/test 성공은 device/HIL 또는 physical timing/ACK 증거가 아니다.

TIM4 configured와 실제 tick은 분리 보고되며 FDCAN begin 실패에도 static due truth가
누적된다. FDCAN request는 Add/Enable/Abort reconciliation 결과를 명시한다. 정상 `Accepted`와
Enable 실패 뒤 hardware pending이 확인되어 bounded abort를 요청한 상태는 모두 executor가
terminal까지 추적하되 admission을 physical success로 세지 않는다. schema-4 Control-Island health는 independent M4 bring-up identity,
TIM4/FDCAN/IRQ/callback, per-lane due→request→terminal, M7 coordinator/read reject를 함께 싣고
normal M4 health가 아직 없을 때도 진단 record 자체를 폐기하지 않는다.
