# AGENTS.md

이 저장소는 CSM firmware의 정식 작업 공간이며 PlatformIO project는
`firmware/csm`이다. 항상 참인 route만 이 파일이 소유한다.

## 기본 진입

1. `git rev-parse HEAD`, `git status --short`, `git diff --stat`로 live truth를 복구한다.
2. `docs/exec-plans/active/`가 있으면 현재 요청과 일치하는 plan만 읽는다.
3. 요청 의도에 맞는 primary skill 하나를 읽는다.
4. 영향받는 L1/L2/contract와 source/test만 읽는다.

대화 요약이나 과거 snapshot보다 Git state, approved active plan, 현재 authority,
source/test가 우선한다. 책임을 모를 때만 `docs/index.md`를 읽고 HISTORY는 ID나
failure mechanism을 targeted search할 때만 사용한다.

## 권위 계층

- L0: owner task 또는 approved exec-plan
- L1: `docs/product/PRODUCT_CONSTITUTION_KO.md`
- L2: `docs/architecture/ACTIVE_ARCHITECTURE.yaml`과 해당 domain 문서
- L3: `production_authority: false`인 experiment
- behavior: 현재 source와 tests
- evidence: `docs/integration/verified-baselines/` 및 실제 artifact
- history: `history/`; rationale 전용이며 current policy를 소유하지 않는다.
- wire: `firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`

README, skill, history, verification status는 제품 계약을 복제하지 않는다.

## Primary skill

- bounded 구현·버그·리팩터: `implement`
- owner/dataflow/scheduler/protocol 변경: `architecture-change`
- 계측·가설·후보 상수: `experiment`
- route/guard/authority/repository knowledge: `harness-maint`

검증은 primary와 경쟁하지 않는 procedure다.

- tests/evidence/release: `verification`
- PlatformIO/M4·M7 build: `embedded-platformio`
- 실제 CAN bench: `can-hil`

`architecture-change`의 `decision_state: review`는 대안을 비교하고 승인 전 runtime을
바꾸지 않는다. L0/active plan이 `approved`이면 재비교 없이 migration을 수행하며,
새 L1 충돌을 발견한 경우에만 escalation한다.

## Bounded impact scan

변경 전 다음을 짧게 확정한다.

`owner -> input -> state -> output -> downstream consumer -> failure/stale -> test/evidence`

변경 contract는 definition, producer, consumer, startup/shutdown, error/recovery,
tests, guards/docs, 필요한 cross-repo consumer까지 검색한다. owner, scheduler,
state lifecycle, queue/retry, wire, profile이 바뀌면 architecture scope다.

## 구현 규율

- stale/untrusted control, hidden retry/replay, unbounded backlog, silent loss를 허용하지 않는다.
- telemetry sink 실패가 control 또는 다른 observer를 막지 않는다.
- admission, terminal outcome, physical success를 구분한다.
- obsolete path/test/doc은 증거를 확인해 같은 변경에서 제거한다.
- 사용자 dirty worktree를 보존한다.
- upload/HIL/vehicle/remote write는 명시 승인과 대상 확인 없이는 실행하지 않는다.

## 검증

- H0: `python tools/verify_harness.py`
- L1: `python firmware/csm/tools/product_invariant_guard.py`
- L2: `python firmware/csm/tools/architecture_conformance_guard.py`
- L3: `python firmware/csm/tools/experiment_guard.py`
- cross-repo: Android repository의 integration guard를 explicit roots로 실행
- build/test: `docs/operations/DEVELOPMENT_SETUP_KO.md`

build/device/HIL은 서로 대체하지 않는다. 실행하지 않은 증거는 NOT RUN으로 보고한다.
