# R16SM Receiver Product Profile

Authority: `L2 ACTIVE PROFILE / BINDING IN IMPLEMENT`

이 문서는 RadioLink R16SM receiver와 T16D transmitter의 제품 연결·admission 경계를
소유한다. CAN/FDCAN 실행, vehicle wire semantic 또는 전역 source scheduler를 소유하지 않는다.

## Frozen profile

- Receiver mode: CRSF, blue LED mode
- Product radio condition: T16D `FHSS V2.1`, 16-channel profile
- Receiver TX -> CSM `PJ9 / J2-27` (UART RX)
- Receiver RX <- CSM `PJ8 / J2-25` (UART TX)
- Common GND required
- Runtime configured baud: `416666`, 8N1
- CRSF device address: `0xC8`
- Control mapping: CH4 drive, CH2 steering
- Optional functions: CH5 auxiliary, CH10 steering overlay, CH11 momentary overlay

`416666`은 단일 runtime configuration이다. 실제 UART bit-time 측정은 release evidence이며
source constant나 build PASS가 실측을 대체하지 않는다. `420000` alternate runtime profile이나
auto-probe/retry는 허용하지 않는다.

## Admission and parsing

M4 parser는 address/length/CRC를 검증하고 byte 단위로 bounded resynchronization한다.
`0x16` 16-channel packed frame과, 한 frame 안에 CH2/CH4가 함께 완결된 analog `0x17`
subset frame을 허용한다. 서로 다른 subset frame을 시간축에서 병합하지 않는다.

RC candidate는 valid `0xC8` + CRC + calibrated CH2/CH4를 가진 fresh RC frame 3개가
연속될 때만 qualified된다. 100 ms RC stale, parser/normalization fault, transmitter OFF는
qualification을 폐기하며 다시 3개를 요구한다. CH5/CH10/CH11이 없거나 유효하지 않으면 해당
기능만 neutral-disabled되고 CH2/CH4 authority는 유지된다.

Link Statistics `0x14`, receiver stats `0x1C`, transmitter stats `0x1D`를 decode한다.
R16SM이 Link Statistics를 보내지 않는 것은 ProtocolFault가 아니다. Receiver에서 statistics가
관측된 경우에만 LQ=0 또는 500 ms stale이 추가 veto다. Qualification/HIL evidence는 runtime
permission을 대신하지 않는다.

UART foreground는 한 turn에 최대 64 bytes 또는 500 us만 drain한 뒤 control ingress와
health/sample publication을 반드시 서비스한다. 남은 UART backlog는 다음 turn에 이어서
처리하며 queue/replay를 추가하지 않고 budget-hit counter로만 관측한다.

## Evidence boundary

2026-07-21 typed capture는 address `0xC8`, type `0x16`, 16 decoded raw channels,
positive RC progress와 Link Statistics 0건을 증명한다. Native golden fixture는 그 typed 값을
CRSF packing 규칙으로 재구성한 byte-exact frame이며 실제 raw UART capture라고 주장하지 않는다.
다음 항목은 physical release blocker로 남는다.

- oscilloscope/logic-analyzer UART bit-time measured baud
- transmitter OFF->ON을 포함한 raw UART stream과 실제 `0x17/0x1C/0x1D` 사용 여부
- T16D FHSS V2.1/16-channel 선택과 전 채널 sweep
