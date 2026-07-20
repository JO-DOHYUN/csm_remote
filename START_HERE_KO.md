# CSM Remote 시작점

이 제품의 고정 방향은 다음과 같다.

```text
Radiolink RC -> M4 frontend -> M7 authority/safety -> vehicle CAN
vehicle evidence -> one canonical publisher -> USB Windows VSM
                                           -> Wi-Fi Android VSM
```

RC와 안전 경로가 제품의 1순위다. VSM 두 종류는 production에서 관측자이며, USB와 Wi-Fi는 서로 영향을 주지 않는 독립 출력이다.

작업자는 `AGENTS.md`의 진입 규칙에 따라 `BRIEF.md`와 작업에 필요한 권위 문서 하나만 읽는다. 현재 구현되지 않은 Wi-Fi나 검증되지 않은 하드웨어를 완성 사실로 취급하지 않는다.
