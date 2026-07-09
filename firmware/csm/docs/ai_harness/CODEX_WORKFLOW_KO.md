# CODEX_WORKFLOW_KO

## 목적
AI 작업이 현재 task 범위를 벗어난 문서를 과도하게 읽거나, 이미 반증된 가설을
반복하거나, wire contract 없이 코드를 크게 바꾸는 일을 막는다.

## 작업 전 읽기
- repo 전체: `AGENTS.md`, `BRIEF.md`
- CSM: `board/AGENTS.md`, `board/BRIEF.md`
- VMS: `qt/AGENTS.md`, `qt/BRIEF.md`
- protocol: `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- passive product changes: `CSM 최종 완성 플랜 Vehicle-Impact-Free Passive Firmware Architecture.md`
- hardware test only: `board/docs/HARDWARE_BRINGUP_GATE_HARNESS_KO.md` first,
  then `board/docs/HARDWARE_TEST_AI_AUX_KO.md`

## 작업 규칙
- 문서/프로토콜 계약이 모호하면 CSM 대수정 전에 먼저 계약을 닫는다.
- 변경 전 현재 baseline을 확인한다.
- default product artifact는 Passive Product이며, Full Instrumented 빌드나
  업로드 결과를 실차 passive acceptance 근거로 보고하지 않는다.
- passive env에서 host downlink/control TX/CAN TX test/USB forced reset guard가
  실패하면 구현 완료로 보고하지 않는다. MCP normal mode는 ACK-observe session
  state machine 안에서만 허용한다.
- 실패한 하드웨어 가설은 근거 없이 반복하지 않는다.
- 빌드/HIL/업로드는 각각 다른 evidence로 보고 구분해서 보고한다.

## 보고 규칙
완료 보고에는 수정 파일, 생성 파일, 실행한 빌드/테스트, 실패 로그 요약, 남은
리스크를 포함한다.
