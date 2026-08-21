# CSM Product Envelope

Authority: `L2 DERIVED CAPACITY INPUT`

정본 계산은 `firmware/csm/include/board/uplink/ProductUplinkEnvelope.h`와 deterministic
guards/tests가 소유한다. 이 문서는 계산값을 release qualification과 혼동하지 않게 라우팅한다.

## Current Derived Input

| Item | Source-derived value | Meaning |
|---|---:|---|
| enabled records | 1,095 records/s | current schema calculation |
| enabled wire | 131,513 B/s | current schema calculation |
| transient thresholds | exploratory | product constant not frozen |

Queue capacities, reserve, pressure and close behavior는 platformio.ini와 L2 uplink architecture가
소유한다. source-derived rate는 socket capacity, zero-loss load, Android capture, external CAN
timing 또는 release margin을 증명하지 않는다.

## Promotion Gate

1. exact schema/build identity로 exploratory ingress/drain/latency/pressure를 측정한다.
2. reviewed decision으로 product constants를 동결한다.
3. exact artifact에서 transport/CAN/device/HIL/fault/soak qualification을 실행한다.
4. loss/conservation/identity가 모두 닫힌 뒤에만 release envelope로 승격한다.

과거 수치와 실험 결과는 `history/` 또는 dated evidence에서만 해석하며 active policy로
재사용하지 않는다.
