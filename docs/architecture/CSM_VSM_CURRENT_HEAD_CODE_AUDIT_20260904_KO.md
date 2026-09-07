# CSM–VSM 최신 HEAD 코드 감사 및 실제 제어 흐름 진단

**문서 기준일:** 2026-09-04  
**감사 기준:** 이전 대화에서 노출된 결함 목록을 출발점으로 삼지 않고, 아래 최신 HEAD의 실제 코드에서 control-critical path를 다시 추적하여 판정함.

- **CSM:** `85f8ce074ad8e450f274659a7378e1e241ba0caa`
- **Android VSM:** `78e583a3cfb83ca69a16975266672f82c0219fda`

---

## 1. 최종 결론

현재 큰 아키텍처 방향은 유지하는 것이 맞다.

```text
TCP :3333  = Observer / telemetry / evidence
TCP :3334  = Reliable transaction
UDP :3335  = Realtime latest-state + causal proof

Android/M7 = authority/session owner
M4         = deterministic physical control island
FDCAN1     = final physical CAN release owner
```

특히 다음 설계는 현재 코드상 강점이다.

- UDP realtime latest-only 구조
- receiver-local CSM liveness
- M4 TIM4 기반 5/20/20 ms 물리 주기
- FDCAN dedicated TX buffer 3개
- CAN auto retransmission 비활성화
- TXBRP/TXBTO/TXBCF 기반 terminal truth
- M7↔M4 double-slot + CRC shared IPC
- M4 독립 fail-close

그러나 현재 HEAD를 **결함 없음**으로 판정하면 안 된다.

| 등급 | 결함 | 핵심 영향 |
|---|---|---|
| P0 | shutdown에서 3334 ARM epoch와 3333 observer epoch를 비교 | operator STOP의 neutral 전송이 지연될 수 있음 |
| P0 | M4 단독 reboot 후 기존 Host ARM epoch 자동 재사용 가능 | reset 후 old motion 자동 재개 가능 |
| P1 | M4 local revoke가 M7 Host authority retirement와 연결되지 않음 | Android/M7 ACTIVE, M4 SAFE 상태 분리 |
| P1 | CSM authority timeout 후 Android가 terminal Expired truth를 못 소비할 수 있음 | HMI/authority truth 불일치 |
| P1 | UDP peer가 protocol validation 전에 proof destination이 될 수 있음 | peer hijack형 DoS / false SAFE |
| P2 | M4 activation epoch 비교가 u32 wrap-safe가 아님 | 장기 correctness deadlock |
| P2 | ARM transaction application timeout 부재 | Arming 상태 장기 정체 가능 |
| RISK | touch liveness가 Compose 75 ms scheduler에 의존 | UI scheduling stall 시 false stop 가능 |

---

# 2. 실제 사람 조작 → 물리 CAN 전체 데이터 흐름

```text
[사용자 손가락]
      │
      ▼
Android Compose joystick
      │
      ├─ setDriveIntent()
      ├─ setSteeringIntent()
      └─ hold 중 75 ms refresh
      │
      ▼
DualAxisIntent
      │
      ▼
ServiceHilController
      │
      │ dedicated semantic worker / 5 ms
      │ motion slew / EHB policy
      ▼
ControlImage
      ├─ 0x005 image
      ├─ 0x007 image
      └─ 0x364 image
      │
      ▼
HostRealtimePlane
      │ newest coherent image only
      │ no FIFO / no retry / no catch-up
      │ 20 ms publication
      ▼
AndroidUdpRealtimeTransport
      │
      ▼
UDP :3335
      │
      ▼
Android Network / Wi-Fi stack / driver
      │
      ▼
802.11 RF
      │
      ▼
Portenta Wi-Fi / Mbed / lwIP
      │
      ▼
WifiRealtimeWorker
      │ nonblocking recvfrom()
      │ bounded service
      ▼
WifiRealtimeMailbox
      │ depth = 1 / latest-only
      ▼
M7 service_host_realtime()
      │
      ├─ typed frame length
      ├─ SOF
      ├─ protocol version
      ├─ record type
      ├─ CRC
      ├─ boot_session_id
      ├─ authority_epoch
      ├─ realtime_sequence
      ├─ proof_ref
      └─ control contract
      │
      ▼
HostRealtimeAuthority
      │
      ▼
ControlSourceManager
      │
      ├─ Host
      ├─ Remote RC
      └─ None
      │
      ▼
FinalControlSnapshotPayload
      │ ~20 ms publication
      ▼
D3 SRAM shared IPC
      │ 2-slot + sequence + CRC
      ▼
M4 foreground / serviceControlIngress()
      │
      ▼
M4StaticCyclicExecutor
      │ TIM4 IRQ = 5 ms
      ├─ lane 0 / 0x005 = 5 ms
      ├─ lane 1 / 0x007 = 20 ms
      └─ lane 2 / 0x364 = 20 ms
      │
      ▼
M4Fdcan1Owner
      │ dedicated TX buffers 0 / 1 / 2
      ▼
STM32H747 FDCAN1
      │
      ▼
CAN transceiver
      │
      ▼
CANH/CANL
      │
      ▼
실제 vehicle-side actuator ECU
```

---

# 3. 물리 실행 계층 판정

## 3.1 M4 TIM4

M4 physical cadence는 Android, M7 foreground, TCP/UDP packet arrival 주기로 직접 실행되지 않는다.

M4는 TIM4를 직접 구성하여:

- timer base: 1 MHz
- `ARR = 4999`
- IRQ period: 5 ms

로 고정한다.

```text
TIM4 tick
  ├─ every tick     → 0x005
  └─ every 4 ticks  → 0x007 + 0x364
```

즉:

```text
0x005 = 200 Hz
0x007 =  50 Hz
0x364 =  50 Hz
```

물리 CAN release schedule의 최종 주인은 M4이다.

## 3.2 FDCAN

현재 M4는 상위 Arduino `CAN.write()` 식의 cyclic path를 사용하지 않는다.

부트스트랩에서 Mbed `can_init_freq()`를 사용해 pin mapping/RCC/initial nominal timing을 확보한 뒤, FDCAN을 direct HAL configuration으로 다시 구성한다.

최종 특성:

```text
Classic CAN
Normal mode
AutoRetransmission = DISABLE
TxBuffersNbr = 3
Tx FIFO = 0
각 lane → dedicated buffer
```

latest-state physical control에 적합한 방향이다. 오래된 request가 controller 내부에서 자동 재전송되며 나중에 살아나는 것을 차단하기 때문이다.

## 3.3 CAN terminal truth

최종 physical truth는 단순 callback이 아니라 FDCAN hardware state를 reconciliation한다.

```text
TXBRP = transmission request pending
TXBTO = transmission occurred
TXBCF = cancellation finished
```

따라서:

```text
software request accepted
≠
physical transmission completed
```

을 분리한다.

---

# 4. M7↔M4 shared-memory 흐름

Control IPC는 단순 shared struct가 아니다.

```text
M7
  ├─ inactive slot 선택
  ├─ odd sequence_begin
  ├─ payload
  ├─ CRC32
  ├─ sequence_end
  ├─ cache clean
  ├─ sequence_begin → even commit
  └─ active_slot 전환
      │
      ▼
M4
  ├─ active sequence 확인
  ├─ begin/end 동일성
  ├─ slot consistency
  ├─ CRC
  └─ M7 boot identity 확인
```

주요 address:

```text
0x3800A800 = control/health IPC
0x3800B800 = CAN1 raw capture ring
```

이 부분은 현재 구조의 강점으로 유지하는 것이 맞다.

---

# 5. 정상 사람 조작 시나리오

## 5.1 ARM

```text
Android session 확인
→ neutral PREARM UDP state
→ CSM PREARM proof
→ Android preArmQualified
→ TCP3334 ARM transaction
→ CSM ARM accepted
→ ARM command_id = authority_epoch
→ Android UDP ACTIVE state
→ CSM ACTIVE proof
→ ServiceHil armAcknowledged
```

구조상 타당하다.

## 5.2 Drive / Steering 동시 입력

Android는 두 축을 독립적으로 수집하지만 ServiceHilController가 하나의 coherent `ControlImage`를 생성한다.

따라서 UDP에서는 Drive/Steering/EHB가 하나의 full-state로 전송된다.

## 5.3 정상 touch release

```text
pointer release
→ releaseDriveIntent / releaseSteeringIntent
→ effective intent = 0
→ next semantic image neutralized
→ next 20 ms UDP full-state
→ CSM state admission
→ M4 latest state
→ physical CAN safe/neutral image
```

정상 흐름이다.

## 5.4 단일 UDP packet loss

```text
packet N lost
→ packet N+1 new full-state
```

N이 재전송되거나 N+1을 막지 않는다. latest-state control 관점에서 맞는 구조다.

---

# 6. P0-1 — shutdown에서 서로 다른 TCP epoch domain을 비교

## 6.1 현재 문제

ARM 시 저장:

```text
armedConnectionEpoch = control.connectionEpoch
```

여기서 `control`은 TCP `:3334` transaction plane이다.

shutdown 시:

```text
product.session.connectionEpoch == armedConnectionEpoch
```

를 검사하는데, `product.session`은 TCP `:3333` observer session이다.

즉:

```text
3334 transaction epoch
        ==
3333 observer epoch
```

을 비교한다. 두 connection은 독립 transport라 동일 epoch domain이 아니다.

## 6.2 실제 장애 시나리오

```text
초기:
3333 epoch = 1
3334 epoch = 1
ARM
armedConnectionEpoch = 1

observer만 reconnect:
3333 epoch = 2
3334 epoch = 1
UDP3335 healthy
Host ACTIVE

사용자 STOP:
2 != 1
→ routeAvailable = false
```

이 조건 때문에 즉시 neutral path가 지연될 수 있다.

HostRealtimePlane에 마지막 nonzero staged image가 남아 있다면 UDP3335는 기존 ACTIVE full-state를 계속 송신할 수 있다.

## 6.3 최악 stop latency

```text
~850 ms Android fallback
+350 ms CSM realtime timeout
+~20 ms M7→M4 alignment
+lane release alignment
≈ ~1.22 s class
```

이전 command 유지 가능 경로가 생긴다.

## 6.4 수정 원칙

```text
STOP
 │
 ├─ 1. UDP3335 즉시 neutral
 │      observer/transaction 상태와 무관
 │
 ├─ 2. M4 physical neutral evidence 확인
 │      3333 available 시
 │
 └─ 3. TCP3334 DISARM transaction
        available 시
```

**observer route가 없다고 realtime neutral 전송을 막으면 안 된다.**

---

# 7. P0-2 — M4 단독 reboot 후 old Host epoch 재수용 가능

## 7.1 문제 구조

M4 executor boot:

```text
rearm_required = true
rejected_activation_epoch = 0
```

기존 Host `authority_epoch = 100`인 상태에서 M4만 reboot되고 Android/M7/UDP가 살아 있으면 M7은 기존 epoch 100 snapshot을 다시 publish할 수 있다.

새 M4 판단:

```text
100 <= rejected_activation_epoch(0)
```

false이므로 기존 epoch 100을 새 usable authority처럼 받을 수 있다.

## 7.2 실제 결과

```text
M4 reset
→ physical SAFE
→ M4 reboot
→ FDCAN/TIM4 healthy
→ M7 기존 Host ACTIVE 유지
→ old epoch snapshot 재publish
→ M4 old epoch accept
→ CAN motion 자동 재개 가능
```

이는 `reset/recovery never resumes old motion automatically` 목표와 충돌한다.

## 7.3 수정 원칙

Host ARM authority를 M4 boot identity에 bind한다.

```text
Host ARM
→ host_authority_m4_boot_id = current M4 boot id

ACTIVE 중:
current_m4_boot_id != host_authority_m4_boot_id
→ Host authority retire
→ permit = 0
→ fresh PREARM + new ARM required
```

이 판단은 Android가 아니라 **CSM M7이 직접 수행**해야 한다.

---

# 8. P1-1 — M4 local revoke와 M7 Host authority retirement 단절

M4는 다음 조건에서 자체 `revokeActive(true)`할 수 있다.

```text
M7 publish stale
IPC integrity failure
tracking fault
FDCAN unavailable
error passive
bus-off
```

M4는:

```text
active invalid
rearm_required = true
rejected_activation_epoch = old epoch
```

로 닫지만 M7 HostRealtimeAuthority는 계속 ACTIVE일 수 있다.

결과:

```text
Android = ACTIVE
M7      = ACTIVE
M4      = SAFE / rearm required
```

fault가 회복되어도 M7은 old epoch를 계속 publish하고, M4는 old epoch를 거부하는 dead-state가 가능하다.

## 권장 수정

M4 health에 monotonic:

```text
authority_revoke_generation
```

을 둔다.

```text
M4 revokeActive(true)
→ authority_revoke_generation++

M7 ARM 시 baseline 저장
ACTIVE 중 generation 변경
→ Host authority retire
→ new ARM required
```

---

# 9. P1-2 — CSM timeout 후 Android terminal truth 잔존 가능

CSM realtime authority timeout 후:

```text
active = false
authority_epoch = 0
proof_status = Expired
```

이 될 수 있다.

Android는 아직 old ARM epoch를 expected authority로 갖고 있을 수 있다.

CSM이 authority=0 Expired proof를 보내면 Android는 authority mismatch로 proof를 reject할 수 있다.

결과:

```text
CSM = SAFE
Android = ACTIVE
```

상태가 남을 수 있다.

Android snapshot에는 `lastProofAtMillis`가 있지만 이를 authority expiration의 receiver-local watchdog으로 사용하지 않는 구조라면 terminal truth 불일치가 지속 가능하다.

## 수정 원칙

### A. Terminal proof가 retired epoch를 명시

```text
authority_epoch = expired old epoch
status = EXPIRED
```

### B. Android proof-age watchdog

```text
proof_age > qualified timeout
→ local ACTIVE false
→ require re-arm
```

이는 CSM safety authority를 대체하지 않고 Android HMI/runtime truth 정합성을 위한 것이다.

---

# 10. P1-3 — UDP peer ownership이 protocol validation보다 빠를 가능성

UDP `recvfrom()`의 raw 송신자 주소가 boot/session/authority validation 전에 proof destination으로 승격될 수 있으면 동일 AP의 다른 endpoint가 garbage UDP로 latest peer를 바꿀 수 있다.

영향:

```text
motion grant 공격      = X
proof starvation/SAFE  = O
```

즉 fail-safe 방향의 DoS/availability 문제다.

## 수정 원칙

```text
raw UDP arrival
→ candidate peer
→ protocol + boot/session + PREARM validation
→ validated peer ownership
→ proof destination promote
```

---

# 11. P2-1 — M4 activation epoch 비교가 u32 wrap-safe가 아님

authority epoch가:

```text
0xFFFFFFFE
0xFFFFFFFF
1
2
```

처럼 wrap할 수 있는데 M4가 일반 숫자 `<=`/`>` 비교를 쓰면:

```text
rejected = 0xFFFFFFFF
new valid = 1
```

을 오래된 값으로 오판한다.

실사용 발생 가능성은 낮지만 protocol correctness bug다.

M4도 동일한 u32 sequence semantic 비교를 사용해야 한다.

---

# 12. P2-2 — ARM transaction application timeout 부재

현재:

```text
ARM TCP write success
→ armSent = true
```

이후 CSM ACK/ACTIVE proof 또는 reconnect를 기다린다.

N-shot/DISARM처럼 명시적인 application-level ARM timeout을 두는 것이 맞다.

물리적으로 PREARM/SAFE이므로 safety-critical failure는 아니지만 transaction lifecycle은 bounded해야 한다.

---

# 13. RISK — touch liveness margin

Compose UI hold refresh:

```text
75 ms
```

Controller touch timeout:

```text
250 ms class
```

margin:

```text
250 / 75 ≈ 3.33 refresh period
```

Android UI/Main scheduling stall이 250 ms를 넘으면 실제 손가락이 붙어 있어도 false touch timeout으로 stop될 수 있다.

현재 판정:

```text
safety failure     = 아님
availability risk  = 맞음
```

실기기 scheduler/GC/load HIL qualification 필요.

---

# 14. Realtime proof의 의미 구분

현재 proof의 `STATE_APPLIED` / `appliedStateGeneration`은 physical CAN까지 적용됐다는 의미로 해석하면 안 된다.

실제 의미는 더 가깝게:

```text
M7 source-state accepted/admitted
```

이다.

그 뒤:

```text
M7→M4 IPC
→ M4 activation
→ TIM4 lane due
→ FDCAN request
→ TX terminal
```

이 남아 있다.

권장 evidence 용어:

```text
M7_STATE_ADMITTED
M4_STATE_OBSERVED
CAN_TX_REQUESTED
CAN_TX_TERMINAL
```

---

# 15. 이론 latency 계산

실제 RF/Android OS jitter를 제외한 코드 scheduling component 기준.

## Drive 0x005

```text
사람 입력
→ semantic worker       ≤ 5 ms
→ next UDP emission     ≤20 ms
→ M7 final snapshot     ≤20 ms
→ M4 TIM4 observation   ≤ 5 ms
→ 0x005 release         ≤ 5 ms class
```

대략:

```text
≤ ~50 ms + Android/Wi-Fi/M7 foreground jitter
```

## Steering 0x007 / EHB 0x364

최종 physical period가 20 ms이므로:

```text
≤ ~65 ms + Android/Wi-Fi/M7 foreground jitter
```

이 수치는 **hard real-time WCET가 아니다.** Android/Linux scheduler와 802.11 RF는 deterministic upper bound를 현재 코드만으로 증명할 수 없다.

---

# 16. Realtime UDP bandwidth 계산

```text
HOST_REALTIME_STATE
64 B payload + 11 B frame overhead
= 75 B

REALTIME_PROOF
36 B payload + 11 B frame overhead
= 47 B
```

각 50 Hz.

## Typed/application wire

```text
(75 + 47) × 50
= 6,100 B/s
≈ 48.8 kbps
```

## IPv4 + UDP overhead 포함

```text
Host state:
(75 + 28) × 50 = 5,150 B/s

Proof:
(47 + 28) × 50 = 3,750 B/s

Total:
8,900 B/s
≈ 71.2 kbps
```

802.11 MAC/PHY overhead 전 기준.

---

# 17. Observer bandwidth와 비교

현재 product observer envelope는 코드상 대략:

```text
496 records/s
109,556 B/s
≈ 876 kbps
```

즉:

```text
Realtime UDP ≈ 71 kbps
Observer     ≈ 876 kbps
```

따라서 realtime path의 핵심 위험은 bandwidth 자체보다 RF burst loss, airtime contention, driver/stack scheduling, peer ownership, deadline, queue ownership이다.

---

# 18. CSM control CAN load 계산

```text
0x005 = 200 fps
0x007 =  50 fps
0x364 =  50 fps
----------------
Total = 300 fps
```

Classic CAN 11-bit + DLC8 frame을 약 110~135 bit/frame 범위로 보면:

```text
300 × 110~135
≈ 33~40.5 kbit/s
```

500 kbit/s bus 대비:

```text
≈ 6.6~8.1 %
```

CSM 자체 3-lane cyclic control은 bus capacity 병목이 아니다. 외부 차량 traffic/arbitration은 별도 qualification 대상이다.

---

# 19. 사람/장애 시나리오 최종 판정표

| 시나리오 | 현재 실제 흐름 | 판정 |
|---|---|---|
| ARM | PREARM proof → TCP ARM → UDP ACTIVE | 정상 구조 |
| Drive hold | UI→semantic→UDP→M4→005 | 정상 |
| Steering hold | 독립 intent→UDP→007 | 정상 |
| Drive + Steering 동시 | 하나의 coherent full-state | 정상 |
| 정상 touch release | next full-state neutral | 정상 |
| 단일 UDP loss | 다음 full-state 독립 처리 | 정상 |
| UDP reorder/duplicate | realtime sequence reject | 정상 |
| sustained UDP loss | CSM local revoke | 정상 |
| TCP3334 RTO while ACTIVE | UDP authority 유지 | 정상 |
| TCP3333 stale while ACTIVE | realtime authority 독립 | 정상 |
| RC valid 등장 | Host preempt / RC source | 정상 방향 |
| FDCAN bus-off | M4 local revoke | 물리 안전 |
| M7 publish stale | M4 local revoke | 물리 안전 |
| **3333 reconnect 후 STOP** | **neutral 지연 가능** | **P0 결함** |
| **M4 단독 reboot while ACTIVE** | **old Host epoch 재개 가능** | **P0 결함** |
| **M4 local revoke 후 회복** | **M7 ACTIVE / M4 SAFE 분리 가능** | **P1 결함** |
| **CSM Host authority timeout** | **Android ACTIVE truth 잔존 가능** | **P1 결함** |
| **다른 UDP peer garbage traffic** | **proof peer 교란 가능** | **P1 availability 결함** |

---

# 20. 수정 우선순위

## P0-1 — shutdown route domain 분리

**수정 대상:** Android `ServiceHilController`

- 3333 `product.session.connectionEpoch`
- 3334 `armedConnectionEpoch`

직접 비교 제거.

정지 시 UDP neutral은 observer 연결과 무관하게 즉시 stage.

## P0-2 — Host authority를 M4 boot identity에 bind

**수정 대상:** CSM M7 authority lifecycle

```text
Host ARM:
host_authority_m4_boot_id = current_m4_boot_id

ACTIVE 중 boot 변경:
retire Host authority
permit = 0
new ARM required
```

## P1-1 — M4 local revoke generation

```text
authority_revoke_generation
```

을 M4 health에 추가하고 `revokeActive(true)`마다 증가. M7은 ARM baseline과 비교하여 변경 시 old Host epoch를 폐기.

## P1-2 — Terminal realtime proof lifecycle

- expired old epoch를 terminal proof에 명시
- Android proof-age watchdog 추가
- terminal `Expired/Rejected`는 stale ACTIVE authority보다 우선 적용

## P1-3 — Validated UDP peer ownership

raw `recvfrom()` peer를 바로 proof owner로 사용하지 말고 protocol-valid PREARM/session을 통과한 peer만 owner로 승격.

## P2-1 — wrap-safe activation epoch

M4 일반 숫자 비교를 u32 sequence semantic으로 통일.

## P2-2 — ARM timeout

Android transaction layer에 bounded ARM timeout 추가.

---

# 21. 수정 후 필수 HIL

## HIL-1 — observer reconnect 중 STOP

조건:

```text
UDP3335 ACTIVE
3334 ACTIVE
3333 강제 reconnect
operator STOP
```

합격:

```text
STOP 입력
→ 즉시 neutral UDP publication
→ M4 neutral generation 관찰
→ CAN terminal neutral 확인
```

## HIL-2 — M4 reset while Host ACTIVE

```text
Host ACTIVE
drive/steering nonzero
M4만 reset
```

합격:

```text
M4 reset
→ safe
→ reboot complete
→ old authority 자동 재개 없음
→ new ARM 전까지 motion CAN 없음
```

## HIL-3 — M4 local revoke recovery

강제:

```text
M7 publish stale / tracking fault / FDCAN fault
```

합격:

```text
M4 revoke
→ M7 Host authority retire
→ Android terminal truth
→ fault recovery만으로 old epoch 재개 없음
```

## HIL-4 — CSM realtime timeout

forward 또는 return proof를 deadline 이상 drop.

합격:

```text
CSM SAFE
→ Android ACTIVE clear
→ terminal reason visible
→ old epoch packet으로 auto recovery 없음
```

## HIL-5 — UDP invalid-peer injection

동일 AP의 다른 endpoint에서 garbage UDP 송신.

합격:

```text
invalid peer가 정상 Android proof route를 hijack하지 못함
```

## HIL-6 — TCP3334 RTO isolation

ACTIVE 상태에서 TCP3334만 stall.

합격:

```text
UDP realtime healthy
→ motion authority 유지
→ transaction unavailable 표시
→ false SAFE 없음
```

---

# 22. 유지해야 할 설계

수정 과정에서 다음은 되돌리면 안 된다.

```text
1. UDP realtime latest-only
2. no realtime retransmission
3. no realtime FIFO backlog
4. receiver-local CSM liveness
5. M4 physical cadence ownership
6. M4 direct FDCAN terminal truth
7. M7/M4 CRC IPC
8. TCP3333 observer와 motion authority 분리
9. TCP3334 transaction과 realtime liveness 분리
10. fault 후 old epoch 자동 복구 금지
```

---

# 23. 현재 감사 범위

이번 문서의 **CONFIRMED** 판정은 다음 control-critical path를 실제 코드 기준으로 추적한 결과다.

```text
사람 입력
→ Android Compose control
→ DualAxisIntent
→ ServiceHilController
→ HostRealtimePlane / HostControlPlane
→ UDP3335 / TCP3334
→ CSM Wi-Fi worker/mailboxes
→ M7 HostRealtimeAuthority
→ ControlSourceManager
→ M7/M4 shared IPC
→ M4StaticCyclicExecutor
→ M4Fdcan1Owner
→ STM32 FDCAN HAL
```

Arduino/Mbed STM32H7 CAN bootstrap 동작도 upstream 구현과 대조했다.

---

# 24. 아직 동일 깊이로 전수감사하지 않은 범위

다음은 이 문서만으로 “결함 없음”을 선언하면 안 된다.

- feeder UART 전체 parser 및 1 Mbps ingress worst-case
- 3333 telemetry/bulk queue의 모든 overflow/backpressure edge case
- Android lifecycle/background/process-death 전체
- Wi-Fi firmware/WHD/lwIP 내부 queue와 RF retransmission 실측 worst-case
- Murata/CYW radio 내부 buffering
- Mid Carrier CAN transceiver 전기적 fault response
- 실제 downstream HNO1 / MDPS / EHB ECU 내부 firmware
- 차량 전체 CAN arbitration traffic worst-case
- 장시간 soak에서 Android scheduler/GC touch-hold margin

따라서 현재 결론은:

> **핵심 제어 아키텍처 자체는 유지 가능하지만, authority retirement와 operator stop lifecycle의 연결이 완전히 닫히지 않았다.**

최우선 수정:

```text
1. STOP에서 3333/3334 epoch domain 혼용 제거
2. M4 reboot 시 old Host authority 즉시 폐기
3. M4 local revoke → M7 Host authority retirement 연결
4. CSM terminal realtime truth → Android ACTIVE 종료 보장
```

---

# 25. 최종 판정

## 아키텍처

**KEEP**

```text
3333 TCP Observer
3334 TCP Transaction
3335 UDP Realtime
M7 Authority
M4 Deterministic Physical Island
```

## 물리 safety island

**방향 적절**

- M4 local watchdog/fail-close
- fixed physical cadence
- no stale CAN auto retransmission
- dedicated TX buffer
- hardware terminal reconciliation
- CRC shared IPC

## 현재 release readiness

**아직 PASS 아님**

이유:

- operator STOP 경로의 transport epoch domain bug
- M4 reboot authority rebinding 결함
- M4 local revoke와 상위 authority lifecycle 단절
- Android terminal authority truth 유지 가능성

```text
ARCHITECTURE: KEEP
CORE PHYSICAL SAFETY DESIGN: GOOD
CURRENT CONTROL LIFECYCLE IMPLEMENTATION: FIX REQUIRED
PRODUCT RELEASE: NOT YET PASS
```
