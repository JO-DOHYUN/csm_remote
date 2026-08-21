# AGENTS.md

이 저장소는 CSM Remote 펌웨어의 정식 작업 공간이다. 실제 PlatformIO project는
`firmware/csm`이며 Android VSM은 `C:\WORKS\VS\vsm_android_app`에서 관리한다.

## 기본 진입

1. `CURRENT.md`
2. 요청에 정확히 맞는 `.agents/skills/*/SKILL.md` 하나
3. 그 skill이 지정한 권위 문서와 영향받는 source/test

정상 작업에서 전체 `docs/`, `history/`, wire 문서를 일괄 읽지 않는다. 소유 경계가
불명확하거나 둘 이상을 실제로 바꿀 때만 `INDEX.md`를 읽는다.

## 권위 계층

- L0 owner input: 명시된 실행 계약과 승인
- L1 제품 불변식: `docs/product/PRODUCT_CONSTITUTION_KO.md`
- L2 현재 구조: `docs/architecture/ACTIVE_ARCHITECTURE.yaml`과 해당 architecture 문서
- L3 실험: 명시된 experiment 기록; `production_authority: false`
- HISTORY: `history/`; 기본 context가 아니며 현재 구조를 소유하지 않는다.
- wire: `firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- behavior truth: source와 test

같은 책임의 정본은 하나만 둔다. README, skill, scenario, history는 정본을 링크하며
그 계약을 별도로 재정의하지 않는다.

## 작업 Mode

- `IMPLEMENT`: L0+L1+L2가 binding이다. 현재 구조 안의 구현·수정·검증만 한다.
- `ARCH_CHANGE`: L0+L1이 binding이고 L2는 검토 기준선이다. 현재안, 제안안,
  더 단순한 제3안, 무변경안을 비교한 뒤 승인 전에는 runtime을 바꾸지 않는다.
- `EXPERIMENT`: L0+L1 안전 경계 안에서 가설만 측정한다. 결과는 정책이 아니다.
- `PRODUCT_CHANGE`: L1 변경은 제품 owner의 명시 승인 뒤에만 가능하다.

반복되는 같은 계층의 delay, queue, retry, timing compensation 또는 test 전용 분기는
소유 경계 결함 가능성을 검토하고 필요하면 `IMPLEMENT`에서 `ARCH_CHANGE`로 올린다.

## 제품 불변식 요약

- 제어 권한과 hard-safety는 단일 fail-closed 경계를 통과한다.
- 한 controlled bus에는 유효한 physical execution owner가 하나뿐이다.
- admission과 실제 물리 실행은 다른 사실이며 실제 성공은 상관된 HW evidence가 필요하다.
- stale/untrusted 제어, hidden retry/replay, 무제한 backlog, 조용한 손실을 허용하지 않는다.
- telemetry sink 실패가 safety/control을 막지 않는다.
- Production observer와 Service/HIL control profile을 명시적으로 분리한다.
- 실험값은 측정·검토·동결·qualification 없이 제품 상수가 되지 않는다.
- 실행하지 않은 build/device/HIL을 PASS로 주장하지 않는다.

정확한 전체 L1은 Constitution이 소유한다. 현재 core 배치, scheduler, FIFO, 함수명,
CAN 일정은 L1이 아니며 active architecture/source에서 확인한다.

## Skill routing

- 구현·버그·리팩터: `implement`
- owner/dataflow/scheduler/protocol 구조 변경: `architecture-change`
- 계측·가설·후보 상수: `experiment`
- build/test/HIL/release 판정: `verification`
- AGENTS/CURRENT/authority/skill/guard 정리: `harness-maint`
- PlatformIO/Mbed/M4·M7 build: `embedded-platformio`
- 실제 CAN bench/external analyzer: `can-hil`

## 변경 규율

- 코드 전에 owner, input/output, state, boundedness, failure, test seam을 확정한다.
- architecture 변경은 active manifest와 prose conformance guard를 같은 변경에서 맞춘다.
- wire 변경은 canonical wire와 모든 소비자 compatibility evidence를 함께 다룬다.
- 구형 path, flag, build route, test, 문서를 검색해 제거하거나 active 공존 근거를 남긴다.
- 사용자 dirty worktree를 보존하고 관련 없는 파일을 수정하지 않는다.
- upload, 차량/HIL, remote write는 명시 승인과 안전한 대상 확인 없이는 실행하지 않는다.

## 검증과 주장

- 하네스: `python tools/verify_harness.py`
- L1: `python firmware/csm/tools/control_execution_guard.py`
- L2: `python firmware/csm/tools/architecture_conformance_guard.py`
- L3: `python firmware/csm/tools/experiment_guard.py`
- build/test 명령: `docs/operations/DEVELOPMENT_SETUP_KO.md`

build는 device가 아니고 device는 HIL이 아니다. 결과에는 실제 실행한 gate, 미실행 gate,
남은 field risk를 구분한다.

## 결과 보고

- 변경·삭제·보존 파일
- 권위/데이터 흐름/책임 경계 변화
- 실행한 guard/build/test와 결과
- device/HIL 실제 수행 여부
- unresolved와 다음 gate
