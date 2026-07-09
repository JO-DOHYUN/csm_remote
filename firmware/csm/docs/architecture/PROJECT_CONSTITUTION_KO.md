# PROJECT_CONSTITUTION_KO

## 목적
이 프로젝트의 최종 형태는 CSM 보드와 VMS Qt 프로그램을 하나의 typed
evidence/control 시스템으로 맞추는 것이다.

## 원칙
- CSM은 USB-CAN 브릿지가 아니라 typed evidence + control gateway다.
- live production stream은 typed transport v1만 사용한다.
- legacy 20-byte는 삭제하지 않지만 replay/import compatibility로만 둔다.
- raw evidence, derived analysis, operator UI state, control intent를 섞지 않는다.
- drop, overflow, timeout, parser error, CAN write failure는 숨기지 않는다.

## Canonical Source
- wire format: `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- CSM baseline: `board/BRIEF.md`
- VMS baseline: `qt/BRIEF.md`
- integration synthesis: `VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md`

## 금지
- Qt serial write success를 CAN success로 표시하지 않는다.
- `CONTROL_ACK`만 보고 actual sent로 판정하지 않는다.
- ADC, voltage, encoder raw evidence를 fake CAN frame으로 만들지 않는다.
- MCP2515 falling-edge-only INT gate를 복원하지 않는다.
- `src/main.cpp`나 Qt `AppController`에 기능을 계속 누적하지 않는다.

