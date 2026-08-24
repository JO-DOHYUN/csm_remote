# CSM Remote Operating Scenarios

Authority: `OPERATOR OUTCOMES`

각 시나리오는 결과와 evidence를 소유한다. 현재 processor, timing, queue, CAN ID 또는
함수 경로는 `../architecture/ACTIVE_ARCHITECTURE.yaml`과 L2 문서가 소유한다.

## S1 안전한 부팅

- Preconditions: 전원과 승인된 firmware bundle.
- Input/action: 장치 부팅.
- Expected: 출력은 safe/inhibited에서 시작하고 권한·safety·실행 경계가 준비된 뒤에만
  control admission이 가능하다.
- Failure: identity, backend, safety 또는 source가 불명확하면 fail closed한다.
- Evidence: boot/session identity, capability, board health, explicit failure reason.

## S2 Production 동시 관측

- Preconditions: vehicle evidence와 하나 이상의 observer route.
- Input/action: RC/autonomy 운용 중 Windows/Android observer 연결·단절.
- Expected: observer는 같은 canonical truth를 독립적으로 보고 어느 sink도 control이나
  다른 sink를 막지 않는다.
- Failure: slow/stalled/lost sink만 bounded 정책에 따라 degraded/closed된다.
- Evidence: source/publish/session identity, sink epoch, sent/drop/high-water/close reason.

## S3 Service/HIL 제어 시작

- Preconditions: 명시적 Service/HIL profile, compatible identity/capability, 유효한 authority,
  lease와 CAN backend.
- Input/action: operator가 ARM하고 상위 제어기가 명령을 요청한다.
- Expected: 하나의 semantic owner와 하나의 physical execution owner만 유효하며, 각 요청은
  권한·safety·frame admission을 거친다.
- Failure: precondition 또는 authority가 모호하면 명시적으로 reject하고 active로 위장하지 않는다.
- Evidence: profile/capability, authority/lease, request identity, admission ACK.

## S4 Service/HIL 실행 truth

- Preconditions: S3 admission 가능.
- Input/action: 상위 제어기가 차량 의미와 시퀀스를 생성한다.
- Expected: CSM은 허용 frame을 의미 변환하지 않고 물리 경계에 전달한다. admission은 실제
  실행 성공과 별도이며 실제 성공은 상관된 hardware evidence로만 판정한다.
- Failure: busy, format, authority, safety 또는 backend failure는 정확한 단계와 이유로 남는다.
- Evidence: acceptance/rejection, command terminal outcome, physical TX evidence, CAN diagnostics.

## S5 Host stale·disconnect·authority 전환

- Preconditions: Host가 active이거나 물리 작업이 terminal 전이다.
- Input/action: lease/stale, transport epoch 종료, higher-priority authority 또는 DISARM.
- Expected: 새 Host admission을 먼저 닫고 Host session/lease를 종료한다. pending hardware
  work는 reasoned cancel/terminal truth로 정리한 뒤 다음 authority를 허용하며 overlap과
  인위적인 dead zone을 모두 피한다.
- Failure: unresolved physical work가 있으면 새 owner를 조기에 열지 않는다.
- Evidence: session close, authority transition, cancellation reason, terminal outcome.

## S6 CAN failure

- Preconditions: 임의 control source active 가능.
- Input/action: bus/backend/tracking failure.
- Expected: 새 실행을 fail closed하고 가능한 범위에서 이미 소유한 작업의 HW outcome을
  보존한다. 실행 불가능한 neutral을 전송했다고 주장하지 않는다.
- Evidence: safety reason, inhibit/failure class, terminal outcome, board/CAN health.

## S7 Reset·reconnect·loss

- Preconditions: observer 또는 control session 존재 가능.
- Input/action: board reset, client reconnect, parser/drop/storage failure.
- Expected: 새 identity/epoch로 경계를 명확히 하며 과거 state를 현재처럼 재사용하지 않는다.
  loss나 partial capture를 정상으로 숨기지 않는다.
- Evidence: boot/session/epoch transition, continuity gap, drop/reset/close/storage state.

## S8 종료와 검증

- Preconditions: Service/HIL 또는 observer session active.
- Input/action: 정상 stop, background/transport loss 또는 시험 종료.
- Expected: 적용 가능한 safety closeout과 terminal evidence를 확인하고 실제로 실행한
  build/device/HIL 범위만 판정한다.
- Evidence: final authority state, terminal/physical evidence, capture finalization, gate report.
