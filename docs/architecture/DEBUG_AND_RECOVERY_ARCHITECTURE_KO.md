# CSM 디버그·복구 아키텍처

## 목적

CSM의 디버그 경계는 장애가 난 뒤에 측정용 코드를 다시 넣는 기능이 아니다.
전원 인가 직후부터 제어·CAN·USB·Wi-Fi가 동작하는 동안 마지막으로 확인된
소유 경계와 진행 상태를 CSM 자체가 보존하고, 다음 부팅에서 이를 읽어 안전한
축소 모드로 복구하는 제품 기능이다.

다음 원칙을 고정한다.

- reset 직전의 원인과 마지막 실행 위치를 구분한다.
- release에도 작은 retained black box와 operator-visible scalar를 남긴다.
- 상세 계측은 같은 제품 경계에 붙이되 제어 truth나 transport owner가 되지 않는다.
- 진단 build는 application-data CAN TX를 차단하고 USB evidence만으로도 판정할 수 있어야 한다.
- 한 sink나 진단기의 정지가 RC, CAN ingest, canonical publisher, 다른 sink를
  기다리게 해서는 안 된다.
- 단일 A/B 결과를 원인 확정으로 표현하지 않고 반복 시험과 반증 가능한 판정표를
  사용한다.

## 부팅부터 evidence까지의 기계적 흐름

```text
reset / power event
  -> Arduino bootloader
  -> M7 application의 earliest reset read
  -> RuntimeSupervisor
       -> BootRecovery (backup SRAM 복구/새 boot commit)
       -> watchdog on/off 결정
       -> Wi-Fi requested/effective mode 결정
  -> safety pins / USB sink
  -> Wi-Fi worker start request
  -> CAN frontend / M4 RC frontend / RemoteControlRuntime
  -> deterministic main service loop

R16SM -> M4 UART/CRSF -> SRAM4 latest sample -> M7 RC source
      -> hard safety -> upstream-autonomy release -> RC/service arbitration
      -> limiter -> explicit vehicle mapper -> CanTxGateway -> CAN backend

CAN/RC/safety/recovery evidence
  -> bounded admission -> CanonicalPublisher (order + encode once)
  -> UsbCdcSink queue
  -> WifiTcpSink queue -> mailbox -> WifiSocketWorker
```

`RuntimeSupervisor`는 Arduino, Wi-Fi, CAN, USB, watchdog driver를 직접 소유하지
않는다. retained 상태를 해석해 startup 결정을 반환하고, main은 그 결정을 각
driver adapter에 적용한다. 이 경계 덕분에 boot-loop 정책은 host unit test에서
검증할 수 있고 특정 vendor driver에 종속되지 않는다.

## reset 원인과 마지막 진행 위치

### 현재 확인 가능한 것

M7 application은 위험한 peripheral보다 먼저 현재 boot chain이 노출한 reset
flag를 읽는다. `BootRecovery`는 이전 boot가 30초 안정 마커에 도달했는지,
마지막 `BootProgress` ID와 detail, boot sequence, source/build identity, early
reset 누계를 checksum과 함께 보존한다. 따라서 다음 boot에서 적어도
"어느 소유 경계까지 진행했는가"와 "같은 구성에서 안정화 전에 반복
종료됐는가"를 판정할 수 있다.

### 현재 확인할 수 없는 것

M7 application은 Arduino bootloader 뒤에 실행된다. bootloader/HAL이 RCC reset
flag를 먼저 읽거나 지우면 application에서 `raw=0` 또는 unknown을 보더라도
watchdog, software reset, brownout, 외부 reset 중 하나로 확정할 수 없다.
retained progress도 전원 자체가 끊겨 backup domain이 유지되지 않으면 남지 않을
수 있다.

정확한 reset-source gate는 bootloader가 RCC reset latch를 clear하기 전에
version, raw flags, boot counter, checksum을 합의된 retained 위치에 commit하고
application이 이를 인계받도록 하는 것이다. brownout/USB 전원/외부 reset을
분리해야 하면 외부 전원·reset monitor evidence도 함께 대조한다. 이 gate 전에는
"3초 watchdog reset"처럼 reset 원인을 시간 간격만으로 확정하지 않는다.

## retained black box

STM32H747 backup SRAM 4 KiB의 현재 배치는 다음과 같다.

| byte 범위 | 크기 | 소유자 | 용도 |
|---|---:|---|---|
| `0..63` | 64 B | legacy breadcrumb | 32 B 슬롯 2개, 마지막 runtime stage |
| `64..383` | 320 B | deep runtime diagnostic | 160 B 슬롯 2개, instrumented build의 상세 snapshot |
| `384..511` | 128 B | reserved | 두 형식 사이 확장·호환 여유 |
| `512..767` | 256 B | `BootRecovery` | 128 B metadata 슬롯 2개, checksum-last 교대 commit |
| `768..2815` | 2048 B | `BootRecovery` | 32 B compact event 64개 ring |
| `2816..3071` | 256 B | reserved | 형식 간 확장·호환 여유 |
| `3072..3199` | 128 B | `RetainedCallLatch` | Wi-Fi vendor call 전/후 64 B 슬롯 2개 |
| `3200..4095` | 896 B | reserved | 향후 형식 확장; 임의 사용 금지 |

`BootRecovery` adapter가 첫 read 전에 backup SRAM과 cache를 준비하고, body와
checksum을 순서대로 clean/commit한다. 두 metadata 슬롯 중 최신 정상 generation을
선택하며 torn write는 반대 슬롯로 복구한다. event ring의 손상 슬롯은 건너뛰고
손상 개수를 product scalar로 노출한다.

legacy breadcrumb는 기존 호환과 상세 분석 보조 수단이다. release 진실은 cache
commit이 명시된 `BootRecovery`이며 legacy 슬롯 하나만으로 reset 원인을
판정하지 않는다.

## release 상시 경계와 deep diagnostic 경계

### release에 남기는 최소 경계

- `RuntimeSupervisor`와 `BootRecovery`
- stable ID인 `BootProgress`와 1초 main-alive commit
- boot/source/build identity, 이전 boot stable 여부, early-reset/quarantine 누계
- retained metadata/event integrity counter
- requested/effective Wi-Fi mode와 watchdog 적용 여부
- `BOARD_HEALTH v13`의 recovery/watchdog/runtime-contract/call-latch scalar projection
- USB/Wi-Fi/CAN queue, overflow, epoch, parser·backend failure counter

이 값은 정상 상태에서도 작동하며 UI가 아니라 typed record와 retained storage가
truth다. progress ID는 source line이 아니라 소유 경계를 뜻하므로 기존 번호를
재사용하거나 재배치하지 않고 새 경계만 뒤에 추가한다.

### instrumented build에만 두는 상세 경계

- 100 ms runtime diagnostic record
- Wi-Fi call phase/sequence/start/duration/result와 worker stack/heartbeat
- FDCAN register before/after snapshot과 TX request/pending/occurred/cancel 상태
- boot checkpoint, retained event phase 7, recovered Wi-Fi call phase 8의 상세 snapshot
- reset 원인 분리를 위한 REF/A/B/C profile selector

상세 계측은 snapshot producer일 뿐 canonical publisher나 recovery policy를 직접
호출하지 않는다. Wi-Fi worker는 retained write와 record publish를 하지 않고,
main이 bounded snapshot을 읽어 기록한다. 진단 record overflow도 counter로
드러내며 제어·CAN truth reserve를 침범하지 않는다.

## early-reset Wi-Fi quarantine

같은 firmware source/experiment-selector identity에서 30초 안정 마커 전에 종료된 boot가
연속 2회 복구되면 다음 boot의 effective Wi-Fi mode를 `Off`로 내린다. 새
artifact build만으로는 이 이력을 지우지 않는다. source manifest 또는 REF/A/B/C
selector가 바뀌면 새 구성에 한 번의 clean trial을 준다.

quarantine의 목적은 원인을 Wi-Fi로 확정하는 것이 아니라 USB·RC frontend·CAN
관측과 retained evidence를 살려 현장 복구 가능성을 높이는 것이다. 현재 진단
profile은 별도로 application-data CAN TX를 compile-time 차단한다. CAN controller의
ACK/error signaling까지 물리적으로 차단한다는 뜻은 아니다. one-shot Wi-Fi retry의
token/consume/success/failure 저장 계약과 host test는 존재하지만, operator-facing
승인 command는 아직 제품 경로에 연결하지 않는다.

## reset 실험 profile

모든 profile은 같은 RC/CAN/USB/Wi-Fi source set을 link하고 selector만 바꾼다.
모두 runtime diagnostic을 켜고 application-data CAN TX를 차단한다.

| profile | watchdog | Wi-Fi mode | 격리 질문 |
|---|---|---|---|
| REF | On | Full AP + TCP | 기준 증상을 재현하는가 |
| A | On | Off, worker/API 호출 없음 | Wi-Fi software 실행 없이도 reset되는가 |
| B | Off | Full AP + TCP | runtime watchdog이 reset 실행자인가, 아니면 별도 reset/power 경로인가 |
| C | On | AP only, TCP server/socket 없음 | AP/WHD/radio 경계인가, TCP server/socket 경계인가 |

판정은 동일 전원·USB·CAN 배선, 같은 관측 시간, source ID와 selector 확인,
boot session/boot sequence/uptime 진행, retained integrity, main gap, Wi-Fi phase,
CAN TX 0을 함께 사용한다.

- REF 실패 + A 안정은 Wi-Fi 실행이 필요한 기여 조건이라는 근거다. 한 번의
  결과만으로 특정 함수가 원인이라고 확정하지 않는다.
- A도 실패하면 Wi-Fi API가 유일 원인이라는 가설을 기각하고 공통 코드,
  power/reset path, CAN/USB 또는 hardware fault로 경계를 이동한다.
- REF reset + B 무응답/정지지만 reset 없음이면 watchdog은 reset 실행자일 수
  있고, full Wi-Fi 또는 공통 runtime이 liveness를 멈추는지 조사한다.
- B도 같은 방식으로 재부팅되면 해당 runtime watchdog 외의 hard fault,
  bootloader/다른 watchdog, 전원·외부 reset 경로를 우선한다.
- C가 REF와 같이 실패하면 configure/AP/WHD/radio/power 경계를 우선하고,
  C가 안정인데 REF만 실패하면 TCP server/socket/downlink 경계를 우선한다.

최소 180초 단일 run은 방향을 좁히는 gate일 뿐 양산 확정이 아니다. 재현성 있는
반복, fault injection, USB+Wi-Fi+RC+CAN 동시 부하와 장시간 soak가 뒤따라야 한다.

## Wi-Fi 격리의 실제 한계

`WifiTcpSink`는 main/publisher용 bounded facade이고, `WifiSocketWorker` 하나가
configure/AP/server/accept/send/recv/close와 socket lifetime을 소유한다. Mbed
`accept()` factory socket은 `close()`가 deallocate하므로 별도 delete하지 않는다.
`Disabled`, `AccessPointOnly`, `FullTcp` mode가 명시적이며 startup attempt는
기본 1회로 제한된다. 이 구조는 main이 socket API를 직접 기다리거나 무한 startup
retry를 반복하는 문제를 제거한다.

그러나 worker와 main은 같은 M7, scheduler, WHD/vendor driver, radio와 전원 경로를
공유한다. mailbox와 thread priority는 논리 격리일 뿐, vendor call이 kernel/IRQ를
막거나 무선 하드웨어·전원 문제가 전체 MCU에 영향을 주는 것을 물리적으로
차단하지 못한다. `beginAP()` 한 호출 자체의 반환 시간도 retry 횟수 제한으로
bounded해지지 않는다. 따라서 heartbeat age와 call-in-progress evidence는
"관찰"이고, 전체 보드 생존을 보장하는 circuit breaker라고 표현하지 않는다.

물리 격리가 양산 요구라면 별도 MCU/network coprocessor, 독립 reset/power domain,
hardware watchdog supervisor를 제품 변경으로 평가한다. 그 경우에도 canonical
identity와 authority의 최종 owner는 M7에 남긴다.

## source/build identity

- source hash는 `platformio.ini`, `include/`, `lib/`, `src/`, `tools/`의 내용으로
  결정하며 build output, artifact, 문서와 `pc_tools`는 포함하지 않는다.
- source ID는 같은 firmware source tree를 묶어 boot-loop 이력을 유지하고
  recovery 내부에서는 experiment selector를 추가로 섞는다.
- build ID는 Git SHA/dirty, PlatformIO env, build epoch를 포함해 exact artifact를
  구분한다.
- 시험 결과는 source hash, build ID, selector, binary SHA-256, boot session을
  함께 보존한다. 단순 `HEAD + dirty` 문자열만으로 artifact를 식별하지 않는다.

`runtime_contract_identity.py`가 선택된 PlatformIO env와 material compile flag의
stable identity를 만들고, recovery identity는 source identity, runtime contract
identity, selector를 합성한다. 비밀값과 build epoch는 제외한다. 따라서 같은 source
tree라도 effective runtime contract가 다르면 boot-loop 이력을 잘못 공유하지 않는다.

## release 축소 정책과 남은 gate

원인 분석이 끝났다고 self-debug 경계를 모두 제거하지 않는다. release에는 상시
scalar black box, compact event ring, quarantine, canonical loss/failure counter를
남긴다. 상세 register snapshot, 100 ms diagnostic stream, A/B/C profile과 실험용
물리 TX suppression은 검증 build로 제한한다.

release artifact 전에 다음 gate가 모두 닫혀야 한다.

1. bootloader early reset-latch 보존 또는 동등한 외부 reset/power evidence
2. upstream autonomy monitor의 실제 CAN profile·runtime wiring과 fail-closed HIL
3. 실제 차량 CAN mapping 승인; `0x007` MDPS mapper는 bench 전용이며 기본 Off
4. D1 hardware gate의 실제 회로 극성·fail-safe 의미와 readback 검증
5. FDCAN TX completion/TXBTO와 상관된 `CAN_TX_RAW`; driver FIFO enqueue 결과만으로
   actual bus transmission을 주장하지 않음
6. Kvaser 등 외부 analyzer에서 ID/payload/주기/ACK와 typed evidence 대조
7. RC + dual CAN + Windows USB + Android Wi-Fi 동시 HIL, fault injection, 장시간 soak

이 gate 전의 Production Remote profile은 RC/CAN 관측 vertical slice이며 vehicle
control release artifact가 아니다.
