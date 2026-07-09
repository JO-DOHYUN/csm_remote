# CSM Remote Phase 1 Skeleton Audit

작성일: 2026-07-09
상태: Phase 1 skeleton closure candidate
대상 코드: `firmware/csm`

이 문서는 Phase 1에서 만든 remote-control skeleton이 제품 정의와 개발 하네스 기준을 지키는지
마감 점검한다.

---

## 1. Phase 1 목표

Phase 1의 목표는 실제 차량 제어 기능을 켜는 것이 아니다.

목표:

```text
최종 module boundary 고정
deny-first default 고정
M4/M7/authority/control/gateway 데이터 흐름 고정
main.cpp 밀집 방지
실제 UART/IPC/CAN TX 전 open decision 차단 유지
```

---

## 2. Completed Skeletons

| Phase | Scope | Status |
|---|---|---|
| 1A | authority/remote/control base types | Complete |
| 1B | autonomy monitor, authority manager, mailbox reader, remote source | Complete |
| 1C | command limiter, vehicle mapper, CAN TX gateway | Complete |
| 1D | M4-M7 mailbox frame contract and decoder | Complete |
| 1E | CRSF parser and RC normalizer skeleton | Complete |
| 1F | M4 mailbox writer helper | Complete |
| 1G | synthetic remote contract self-test helper | Complete |
| 1H | M7 remote control orchestrator skeleton | Complete |
| 1I | Phase 1 guard and audit | Complete candidate |

---

## 3. Current Data Flow

```text
CRSF bytes
  -> CrsfParser
  -> RcNormalizer
  -> M4RemoteMailboxWriter
  -> M4RemoteMailboxFrame
  -> M4RemoteMailboxReader
  -> RemoteControlSource
  -> RemoteControlOrchestrator
  -> AuthorityManager
  -> CommandLimiter
  -> VehicleCommandMapper
  -> CanTxGateway
```

Phase 1 does not connect this flow to `main.cpp`.

---

## 4. Guard

Phase 1 guard:

```powershell
python firmware/csm/tools/remote_phase1_guard.py
```

The guard verifies:

```text
remote/control skeleton files do not contain direct UART/IPC/CAN TX IO
remote module does not know CAN ID/frame/gateway types
main.cpp does not wire Phase 1 remote runtime
platformio.ini does not enable M4/remote runtime
VehicleCommandMapper still rejects real vehicle mapping
CanTxGateway still evaluates only and performs no backend write
```

---

## 5. Verification Commands

Required before Phase 1 closure:

```powershell
git diff --check
python firmware/csm/tools/remote_phase1_guard.py
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

---

## 6. Remaining Open Decisions

Phase 1 does not close these decisions:

| ID | Blocks |
|---|---|
| OD-001 | final autonomy authority profile |
| OD-002 | D1 / hardware TX gate semantics |
| OD-003 | actual R16SM CRSF baud/mode/cadence |
| OD-004 | final M4-M7 IPC mechanism |
| OD-005 | real vehicle CAN command IDs/payloads |
| OD-006 | T16D channel/switch semantics |
| OD-007 | service/HIL host authority policy |
| OD-008 | final watchdog/reset/fault recovery behavior |

---

## 7. Phase 2 Entry Criteria

Phase 2 may start only as evidence work, not production control enablement.

Allowed Phase 2 work:

```text
Portenta H7 M4 PlatformIO env proof
Serial3/R16SM logic analyzer capture
synthetic or captured CRSF parser test vector execution
M4-M7 IPC prototype with torn-read/stale test
guard update for Phase 2 profiles
```

Still forbidden:

```text
production local CAN TX
real vehicle command mapping
hardware TX gate enable as authority proof
raw host/VSM CAN control product path
```

---

## 8. Closure Statement

Phase 1 is complete when the guard and passive build pass and the working tree is committed.

Current engineering conclusion:

```text
Phase 1 skeleton is structurally complete.
The project is ready to move from architecture skeleton to Phase 2 evidence/prototype work.
It is not ready for production vehicle motion-control TX.
```
