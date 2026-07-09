# VMS_ARCHITECTURE_KO

## 목표 구조
VMS/Qt는 typed evidence를 저장/표시/분석하고, 제어 intent와 actual audit을
분리해서 보여준다.

Required runtime split:
- `TransportRuntime`: serial open/read/write, typed frame parser
- `StorageRuntime`: exact typed stream append-only storage
- `AnalysisRuntime`: CAN DB decode, voltage scaling, feedback interpretation
- `EvidenceRuntime`: health/event/drop/fault evidence
- `ControlRuntime`: command encode, request timeline, ack/audit correlation
- `OperatorRuntime`: UI-visible state and notes

## 연결 상태
COM open은 물리 포트 open일 뿐이다. typed board alive는 valid `CAPABILITY`와
protocol match 이후로 판단한다.

## 제어 표시
UI/모델은 requested, accepted/rejected, sent audit, feedback observed를 별도 상태로
표시한다. `CONTROL_ACK`만으로 sent를 표시하지 않는다.

## Bus labeling
Qt/VMS must not hard-code bus labels. Bus display names, roles, backends,
transceivers, bitrates, and TX permissions come from `CAPABILITY`. Current Mid
Carrier profile major `3` displays MCP2515/TJA1050 `bus=0` and, in the final
dual CSM env, Mid Carrier J4/U2 `bus=1` with RX/TX enabled according to each
descriptor. Deferred profile major `2` remains the unproven dual internal
CAN/TJA1051 target.
