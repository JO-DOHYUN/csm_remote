# CSM/VSM Product Constitution

Authority: `L1 PRODUCT INVARIANTS`

이 문서는 CSM과 VSM에 공통인 architecture-independent 제품 불변식의 단일 정본이다.
현재 processor, thread, timer, CAN ID schedule, FIFO 수, class/function 또는 queue 구현은
`docs/architecture/ACTIVE_ARCHITECTURE.yaml`과 L2 문서가 소유한다.

1. 모든 제어 명령은 단일 권한·admission 판정 경계를 통과한다.
2. 하나의 controlled bus에는 유효한 physical execution owner가 정확히 하나 존재한다.
3. 모호한 control authority는 fail closed한다.
   여기서 fail-closed는 신뢰할 수 없거나 stale인 active motion을 즉시 제거한다는
   뜻이며, healthy physical CAN transport의 lane-safe wire 동작까지 반드시 silence해야
   한다는 뜻은 아니다. lane-safe wire action은 frozen wire contract만 따른다.
4. stale 또는 신뢰할 수 없는 제어가 조용히 active로 남지 않는다.
5. admission truth와 physical-execution truth를 구분한다.
6. 실제 물리 성공은 command와 상관된 hardware evidence가 있어야 한다.
7. hidden retry 또는 replay를 금지한다.
8. 무제한 control backlog를 금지한다.
9. telemetry sink 실패가 safety/control을 막지 않는다.
10. Production observer와 Service/HIL control profile을 명시적으로 분리한다.
11. 각 control source/command semantic에는 유효한 semantic owner가 정확히 하나이며,
    같은 semantic transformation을 architecture layer 사이에서 중복하지 않는다.
12. loss, drop, reset, reconnect, session transition을 관측 가능하게 만든다.
13. experiment 값은 자동으로 production policy가 되지 않는다.
14. threshold는 exploratory measurement와 reviewed value decision 뒤에 product
    constant로 동결하고, 그 뒤 qualification HIL을 수행한다.
15. 실행하지 않은 build, device, HIL gate를 PASS로 주장하지 않는다.

L1 변경은 `PRODUCT_CHANGE`이며 제품 owner의 명시 승인이 필요하다. 현재 구조를 바꾸는
`ARCH_CHANGE`는 이 Constitution을 유지하면서 current/proposal/third/no-change를 비교한다.
