---
authority: PROCEDURE
owner: harness-v3
status: active
read_when: changing repository routes, skills, guards, or exec-plan lifecycle
---

# Harness V3 Architecture

## Default loop

`Git HEAD/status/diff -> matching active exec-plan -> one primary skill -> affected authority/source/test`

CURRENT/status cache와 HISTORY는 default context가 아니다. Context가 compact되면 chat이 아니라
Git, matching active plan, authority, source/test 순서로 복구한다.

## Guard ownership

| Guard | Sole responsibility |
|---|---|
| H0 harness | route, metadata, context budget, links, duplicate/stale route, exec-plan schema, reference/generated classification |
| L1 product invariant | Constitution schema and architecture-independent invariants |
| L2 architecture | active manifest and current source/test/build-role conformance |
| WIRE contract | producer/consumer schema and offsets |
| L3 experiment | non-production authority, promotion and cleanup boundary |
| integration | explicit repository roots and optional verified-baseline comparison |

H0는 processor, timer, CAN ID, environment, module topology, product profile 또는 commit SHA를
검사하지 않는다. Standalone H0는 다른 repository checkout을 요구하지 않는다.

## Work sizing

Simple bug/UI/build fix는 exec-plan을 만들지 않는다. Cross-repo, owner/dataflow/profile,
다단계 migration이나 제품 전체 task만 active exec-plan을 사용한다. Approved plan은
architecture comparison을 생략한다.

모든 변경은 owner, input, state, output, consumer, failure/stale, test/evidence를 먼저
resolve하고 definition부터 downstream consumer와 recovery까지 bounded search한다.

## Exec-plan lifecycle

`active/` plan은 최소 schema와 `decision_state: review|approved`를 가진다. 완료 시 canonical
authority/source/test를 갱신하고 `completed/YYYY-MM/`로 이동한다. Completed plan은 current
architecture가 아니며 unresolved evidence만 별도 route에 남긴다.

## Scenario expectations

local bug=implement/no plan, approved architecture=architecture-change/no re-review,
cross-repo wire=architecture-change+explicit integration, experiment=experiment,
build-only=implement+build procedure, HIL request=implement 또는 verification procedure+authorization,
compacted session=Git/active-plan reconstruction. 모든 scenario에서
HISTORY/CURRENT default-read는 false다.

Cross-repository observability migration의 CSM 진입점은 route-only
`docs/exec-plans/active/OBSERVABILITY_CONTRACT_REFACTOR_20260909.md`다. P0는
`harness-maint`이며 standalone H0는 다른 checkout, runtime hook, schema 또는 제품 PASS를
요구하지 않는다. 실행 상태는 canonical Android plan과 immutable evidence로만 판정한다.
