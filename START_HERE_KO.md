# CSM Remote 시작점

CSM Remote는 차량 제어 입력을 단일 권한·hard-safety 경계와 단일 물리 CAN 실행
경계로 전달하고, 차량과 장치의 실제 evidence를 canonical 관측 stream으로 제공한다.

```text
RC / autonomy / service-host inputs
  -> single authority + hard-safety boundary
  -> single physical CAN execution boundary
  -> vehicle

vehicle and execution evidence
  -> canonical observation path
  -> USB / Wi-Fi / UI observers
```

Production 관측과 Service/HIL 제어는 명시적으로 분리된다. 관측 부하나 한 sink의
실패는 safety/control 또는 다른 sink를 막지 않으며, 실제 실행 성공은 HW evidence
없이는 주장하지 않는다.

작업은 `AGENTS.md`의 순서대로 `CURRENT.md`, matching skill, 영향받는 권위와
source만 읽는다. 현재 구현 배치는 `docs/architecture/ACTIVE_ARCHITECTURE.yaml`이
가리키는 L2 문서에서 확인한다.
