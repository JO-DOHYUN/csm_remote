# CSM Remote Authority Architecture Final Handoff

Date: 2026-07-06
Target baseline: `JO-DOHYUN/HAMT2-platform` commit `bfef287` (`Finalize passive CSM fault hold evidence`)
Target hardware: Arduino Portenta H7 + Portenta Mid Carrier + Radiolink T16D + Radiolink R16SM
Primary goal: add RC/manual-control capability without ever competing with upstream autonomous control.

---

## 0. Non-negotiable project philosophy

The CSM must **never** be a competing controller against the upstream autonomous controller.

The CSM is an **autonomy-first fail-silent local authority gate**:

> If upstream autonomous motion-control authority is active, recently active, unknown, ambiguous, or protocol-faulted, the CSM shall output **zero local motion-control CAN frames**.

Local control from RC or VSM is only allowed after upstream autonomous authority is **positively released** and all local handoff conditions are satisfied.

Final control priority:

```text
Hard safety inhibit
> Upstream autonomous authority
> RC/manual authority
> VSM/service authority
> Monitoring only
```

Important interpretation:

- CAN observe/RX is allowed and required.
- Local control acceptance is forbidden unless upstream authority is clearly released.
- Local CAN TX is forbidden unless the full authority chain allows it.
- VSM is observer-only by default.
- RC is not allowed to bypass M7 authority logic.
- M4 is only a CRSF input frontend, not a vehicle controller.

---

## 1. Hardware scope

Final hardware target:

```text
Arduino Portenta H7
+ Portenta Mid Carrier
+ Radiolink T16D transmitter
+ Radiolink R16SM receiver
+ CRSF UART mode
```

### 1.1 Existing CSM pins must remain unchanged

The current CSM pin map from `include/BoardPins.h` must be preserved.

```text
D0  SafetyWatchdogToggle
D1  CanTxEnable
D2  EstopInN
D3  ArmKeyIn
D4  EncoderB
D5  EncoderA
D6  EncoderZ
D7  MCP2515 CS
D8  MCP2515 MOSI/COPI
D9  MCP2515 SCK
D10 MCP2515 MISO/CIPO
D11 MCP2515 INT
D12 FieldPowerOk
D13 EncoderFaultN
D14 SpareServiceGpio / ADC CS reserve
A0  VoltageSense0
A1  VoltageSense1
A6  VoltageSense2
A7  VoltageSense3
```

Do not move or repurpose these pins for RC.

### 1.2 RC receiver connection pin map

Power wiring is intentionally excluded from this document except for common-signal-ground requirement.

Use UART3 on the Mid Carrier.

```text
R16SM CRSF TX  -> Mid Carrier J14 RX3 / Portenta Serial3 RX / PJ_9
R16SM CRSF RX  <- Mid Carrier J14 TX3 / Portenta Serial3 TX / PJ_8
R16SM GND      <-> CSM signal GND
```

Do not use:

```text
SBUS
PWM
RC IN
CAN0/CAN1 TX/RX pins
D0-D14
A0/A1/A6/A7
```

### 1.3 Hardware authority gate

`CanTxEnable` already exists on `D1`.

For product-oriented local-control builds:

```text
When local control is not authorized:
  CanTxEnable = LOW

When a local control frame is authorized:
  CanTxEnable may be enabled only inside the controlled TX path.
```

Software authority checks are not enough. The final CAN TX path must also drive/verify the hardware TX enable state.

---

## 2. Core safety invariants

Codex must treat these as hard requirements, not preferences.

### R1. Autonomy-first invariant

```text
If AutonomyAuthorityState != InactiveConfirmed,
CSM local motion-control CAN TX count must be exactly 0.
```

### R2. Unknown is unsafe

```text
Unknown
Ambiguous
ProtocolFault
RecentlyActive
ActiveConfirmed
```

All of the above states inhibit local RC/VSM motion-control output.

Only this state allows local control to be considered:

```text
InactiveConfirmed
```

### R3. Local control is only considered after handoff

Local control is not allowed immediately after upstream command traffic stops.

Required handoff gates:

```text
1. Upstream autonomy state is InactiveConfirmed
2. Handoff quiet window has elapsed
3. No protocol ambiguity/fault is active
4. Local source is valid
5. Local source is neutral
6. Operator gives explicit takeover/arm request
7. SafetySupervisor allows TX
8. AuthorityManager allows source
9. CanTxGateway allows TX
10. Hardware CanTxEnable gate is correct
```

### R4. Reappearance preempts local

If upstream autonomous motion-control command traffic reappears while RC or VSM local control is active:

```text
- latch local_tx_inhibit immediately
- set CanTxEnable LOW
- release local authority
- reject further local commands
- emit diagnostic/evidence event
- output zero additional local CAN frames after the latch
```

This check must happen before the next local CAN TX attempt. Do not wait for slow UI/telemetry paths.

### R5. Single local CAN TX exit

All local motion-control CAN TX must go through exactly one gateway:

```text
CanTxGateway
```

Forbidden:

```text
RemoteControlSource -> CAN write
HostControlSource   -> CAN write
M4                  -> CAN write
VSM/raw downlink    -> CAN write in product mode
Test module         -> direct CAN write outside explicit test build
```

### R6. RC does not know CAN

The RC path must never produce CAN IDs or CAN payload bytes directly.

RC produces only normalized operator intent:

```text
throttle
steer
brake
mode
switch states
validity/failsafe state
```

Only `VehicleCommandMapper` may convert an accepted safe operator command into CAN frames.

### R7. VSM is observer-only by default

Product/default builds must treat VSM as a monitoring client.

VSM may receive:

```text
CAN_RX_SEGMENT
BoardHealth
BoardEvent
ControlAck
CAN_TX_RAW evidence
capability/status records
```

VSM must not have motion-control authority unless an explicit service-control build/profile is enabled.

### R8. No raw CAN downlink in product mode

Host/VSM raw CAN TX is useful for bench/HIL but must not exist in product local-control builds unless explicitly marked as service/test.

Product build rule:

```text
BOARD_ENABLE_RAW_CAN_DOWNLINK=0
```

### R9. Last value never drives after stale/failsafe

If RC link becomes stale or failsafe:

```text
- do not reuse the last stick position
- release RC local authority
- command neutral or inhibit
- reject takeover until fresh/valid/neutral/takeover sequence is satisfied again
```

### R10. Evidence must prove accepted/rejected behavior

Accepted local control:

```text
ControlAck accepted
+ CAN_TX_RAW actual write evidence
```

Rejected local control:

```text
ControlAck rejected
+ no CAN_TX_RAW for that command
```

---

## 3. Correct conceptual architecture

The architecture is not “M4 controls the vehicle.”

Correct concept:

```text
M4 = CRSF receiver frontend
M7 = sole authority owner and sole CAN TX owner
VSM = observer by default
```

Full flow:

```text
[R16SM CRSF]
  -> M4 CrsfUartDriver
  -> M4 CrsfParser
  -> M4 RcNormalizer / FailsafeDetector
  -> M4 latest RcSample mailbox

[M7]
  CAN observe path -> AutonomyAuthorityMonitor -> local_tx_inhibit_latch
  M4 RcSample      -> RemoteControlSource      -> OperatorCommand
  VSM/service      -> HostServiceSource        -> OperatorCommand

  OperatorCommand
  -> AuthorityManager
  -> SafetySupervisor
  -> CommandLimiter
  -> VehicleCommandMapper
  -> CanTxGateway
  -> hardware CanTxEnable
  -> CAN backend
  -> ControlAck / CAN_TX_RAW / BoardEvent evidence
```

Critical ordering:

```text
AutonomyAuthorityMonitor is above RemoteControlSource and HostServiceSource.
```

RC and VSM source priority only matters after autonomy is released.

```text
If autonomy is not InactiveConfirmed:
  Remote priority over VSM is irrelevant because both are blocked.
```

---

## 4. M4 responsibility boundary

M4 owns only RC serial input handling.

M4 allowed responsibilities:

```text
- own UART3 / Serial3 for R16SM CRSF
- receive UART bytes
- frame sync
- CRSF length/type/CRC validation
- channel unpacking
- normalize RC channels
- detect link state / failsafe / stale
- publish latest RcSample to M7 handoff mailbox
```

M4 forbidden responsibilities:

```text
- CAN ID generation
- CAN payload generation
- CAN TX
- arm/disarm final decision
- source ownership decision
- autonomy authority decision
- SafetySupervisor access
- CanTxEnable control
```

M4 must be simple, bounded, and deterministic.

Implementation constraints:

```text
- no heap allocation
- no strings in hot path
- no JSON in hot path
- fixed-size parser buffer
- fixed-size latest mailbox
- sequence counter
- timestamp
- CRC/check field
- stale/failsafe flags
```

---

## 5. M4 to M7 handoff design

Use latest-sample mailbox, not a growing FIFO queue.

Reason:

```text
RC control cares about the newest sample, not a backlog of old stick positions.
A queue backlog can turn old user input into delayed vehicle motion.
```

Recommended data concept:

```cpp
struct RcSample {
  uint16_t magic;
  uint8_t  version;
  uint8_t  state;       // OK, LOST, FAILSAFE, CRC_BAD, STALE
  uint16_t seq;
  uint32_t m4_time_ms;
  int16_t  ch[16];      // normalized channels
  uint16_t switch_bits;
  uint8_t  link_quality;
  uint8_t  flags;
  uint16_t crc;
};
```

M7 reader must verify:

```text
magic
version
crc
seq monotonicity/gap
age/stale timeout
failsafe state
neutral state
```

If any check fails, RC authority must not be granted.

---

## 6. M7 module boundaries

Recommended new/modified modules:

```text
src/board/authority/
  AutonomyAuthorityMonitor.h/.cpp
  AuthorityManager.h/.cpp
  AuthorityTypes.h

src/board/control/
  OperatorCommand.h
  CommandLimiter.h/.cpp
  VehicleCommandMapper.h/.cpp
  CanTxGateway.h/.cpp
  ControlEvidencePublisher.h/.cpp

src/board/remote/
  M4RemoteMailboxReader.h/.cpp
  RemoteControlSource.h/.cpp

src/board/host/
  HostMonitorSession.h/.cpp
  HostServiceSource.h/.cpp
```

Existing `SafetySupervisor` should not become RC-specific. It remains the safety gate.

`AuthorityManager` decides who may request local control.

`SafetySupervisor` decides whether the board is safe to transmit.

`CanTxGateway` is the final software TX gate.

`CanTxEnable` is the hardware TX gate.

---

## 7. AutonomyAuthorityMonitor design

Because upstream autonomous protocol is not finalized, do not hard-code one fragile ID/byte rule into the architecture.

Create a profile-driven monitor.

Concept:

```cpp
enum class AutonomyAuthorityState : uint8_t {
  Unknown,
  InactiveConfirmed,
  ActiveConfirmed,
  RecentlyActive,
  Ambiguous,
  ProtocolFault,
};
```

Only `InactiveConfirmed` allows local control to be considered.

Rule profile concept:

```cpp
struct AutonomyCommandProfile {
  uint8_t  bus;
  uint32_t can_id;
  uint32_t can_id_mask;
  uint8_t  dlc_min;
  uint8_t  dlc_max;

  bool     has_mode_bit;
  uint8_t  mode_offset;
  uint8_t  mode_mask;

  bool     has_alive_counter;
  uint8_t  alive_offset;
  uint8_t  alive_bits;

  uint16_t expected_period_ms;
  uint16_t active_timeout_ms;
  uint16_t release_quiet_ms;
};
```

Initial implementation may use a conservative placeholder profile, but the architecture must support replacing the profile once the upstream protocol is finalized.

Monitor outputs:

```text
state
last_upstream_command_ms
quiet_window_satisfied
ambiguous_count
protocol_fault_count
local_tx_inhibit_latch
```

Critical behavior:

```text
On observed upstream motion-control command:
  local_tx_inhibit_latch = true
  request CanTxEnable LOW
  AuthorityManager must release local authority
```

---

## 8. AuthorityManager design

AuthorityManager owns local source ownership.

States:

```text
BootInhibit
ObserveOnly
AutonomyActiveLock
AutonomyRecentlyActive
AutonomyAmbiguous
AutonomyProtocolFault
LocalHandoffPending
LocalReady
RemoteActive
HostServiceActive
FaultLockout
```

Allowed local TX by state:

```text
BootInhibit              no
ObserveOnly              no
AutonomyActiveLock        no
AutonomyRecentlyActive    no
AutonomyAmbiguous         no
AutonomyProtocolFault     no
LocalHandoffPending       no
LocalReady                no, unless source authority is explicitly granted
RemoteActive              conditional yes
HostServiceActive         conditional yes, service build only
FaultLockout              no
```

Remote/VSM priority:

```text
If autonomy is not released:
  both blocked

If autonomy released:
  Remote > VSM
```

VSM may be compiled out entirely.

---

## 9. OperatorCommand and CAN mapping

Local source modules output only `OperatorCommand`.

Example concept:

```cpp
enum class ControlSourceId : uint8_t {
  None,
  Remote,
  HostService,
  Test,
};

struct OperatorCommand {
  ControlSourceId source;
  uint32_t seq;
  uint32_t source_time_ms;

  bool enable_request;
  bool takeover_request;
  bool release_request;

  int16_t throttle_permille; // -1000..1000
  int16_t steer_permille;    // -1000..1000
  int16_t brake_permille;    // 0..1000

  uint8_t drive_mode;
  uint16_t validity_flags;
};
```

`RemoteControlSource` must not know the final CAN ID or byte layout.

`VehicleCommandMapper` is the only module that maps a safe accepted operator command to CAN frames.

---

## 10. Build profiles and guards

Preserve the existing passive-product philosophy.

Add profile-level separation.

### Monitor/passive product

```ini
-D BOARD_ENABLE_MONITOR_ONLY=1
-D BOARD_ENABLE_HOST_CONTROL=0
-D BOARD_ENABLE_REMOTE_CONTROL=0
-D BOARD_ENABLE_M4_REMOTE_FRONTEND=0
-D BOARD_ENABLE_RAW_CAN_DOWNLINK=0
```

### Remote local-control test/product-candidate

```ini
-D BOARD_ENABLE_MONITOR_ONLY=0
-D BOARD_ENABLE_HOST_CONTROL=0
-D BOARD_ENABLE_REMOTE_CONTROL=1
-D BOARD_ENABLE_M4_REMOTE_FRONTEND=1
-D BOARD_ENABLE_RAW_CAN_DOWNLINK=0
```

### Service/HIL build only

```ini
-D BOARD_ENABLE_MONITOR_ONLY=0
-D BOARD_ENABLE_HOST_CONTROL=1
-D BOARD_ENABLE_REMOTE_CONTROL=1
-D BOARD_ENABLE_M4_REMOTE_FRONTEND=1
-D BOARD_ENABLE_RAW_CAN_DOWNLINK=1
```

`BOARD_ENABLE_RAW_CAN_DOWNLINK=1` must never be treated as normal product mode.

### Required post-build symbol guards

Extend the current passive guard concept.

Remote-off build must not link:

```text
CrsfParser
M4RemoteMailboxReader
RemoteControlSource
RcFailsafeDetector
```

Monitor-only/product build must not link:

```text
HostCanTxRequest handler
raw CAN downlink TX path
RemoteControlSource, unless explicitly remote product build
CAN::write from forbidden direct paths
```

Raw-CAN-off build must not link:

```text
handle_host_can_tx_request
HostCanTxRequest active path
```

---

## 11. Timing and embedded feasibility target

Expected processing budget is light for Portenta H7.

Reference estimates:

```text
CRSF max frame size: about 64 bytes
CRSF common baud: about 420000 baud
Worst useful RC sample rate: about 150 Hz
Worst raw CRSF traffic: about 9.6 KB/s
```

M4 estimate:

```text
CRSF parser + CRC + normalize: expected low single-digit CPU percent
Static memory: roughly 1-2 KB for parser/ring/mailbox
```

M7 estimate:

```text
Authority/Safety/Mapper/Gateway: O(1) per tick
Recommended M7 authority tick: 100-200 Hz
Recommended initial local CAN command output: 20-50 Hz
Memory addition: roughly a few KB
```

Do not introduce:

```text
heap allocation in hot path
blocking serial writes in control path
JSON/string formatting in control path
unbounded queues
old RC sample backlog processing
```

---

## 12. Required implementation order for Codex

Do not implement everything at once.

### Phase 1: architecture scaffolding, no CAN TX change

Create types/modules:

```text
AutonomyAuthorityState
AutonomyAuthorityMonitor skeleton
AuthorityManager skeleton
OperatorCommand
RemoteControlSource skeleton
CanTxGateway interface skeleton
```

No new local CAN TX behavior yet.

### Phase 2: M4 CRSF read-only

Implement:

```text
M4 UART3 CRSF parser
RcSample latest mailbox
M7 mailbox reader
BoardHealth/BoardEvent summary for RC state
```

Still no vehicle CAN TX.

### Phase 3: autonomy observe/read-only

Implement:

```text
AutonomyAuthorityMonitor on CAN observe path
Active/Recent/Unknown/InactiveConfirmed state
local_tx_inhibit_latch
BoardEvent evidence
```

Still no local vehicle CAN TX.

### Phase 4: TX inhibit proof

Implement/verify:

```text
CanTxEnable LOW in all blocked states
CanTxEnable only enabled by CanTxGateway controlled path
reappearance latch before next local TX
```

Use bench/dummy TX first.

### Phase 5: low-rate remote manual command

Only after phases 1-4 pass:

```text
Autonomy InactiveConfirmed
quiet window complete
remote valid
remote neutral
explicit takeover
SafetySupervisor allows
AuthorityManager allows
CanTxGateway allows
```

Then allow low-rate local CAN command, starting 20 Hz, later 50 Hz if bus/load evidence allows.

---

## 13. Acceptance tests

Codex must implement or prepare tests/evidence for these cases.

### Autonomy lock tests

```text
1. autonomy active + remote arm request -> reject, zero CAN_TX_RAW
2. autonomy active + VSM request -> reject, zero CAN_TX_RAW
3. autonomy unknown + remote arm -> reject, zero CAN_TX_RAW
4. autonomy ambiguous + remote arm -> reject, zero CAN_TX_RAW
5. autonomy protocol fault + remote arm -> reject, zero CAN_TX_RAW
```

### Handoff tests

```text
6. autonomy command timeout immediately followed by remote stick forward -> reject
7. quiet window in progress + takeover -> reject
8. quiet complete + remote not neutral -> reject
9. quiet complete + remote neutral + explicit takeover -> RemoteActive allowed
10. release-frame path and timeout-fallback path both tested
```

### Reappearance tests

```text
11. RemoteActive + one upstream motion frame reappears -> local_tx_inhibit latched
12. RemoteActive + upstream burst -> zero further local CAN TX after latch
13. HostServiceActive + upstream reappears -> host released and local TX blocked
```

### Source conflict tests

```text
14. RemoteActive + VSM command -> VSM rejected
15. HostServiceActive + remote takeover -> remote may win only when autonomy inactive and service policy allows
16. RC stale -> last stick value not used
17. RC failsafe -> takeover forbidden
```

### Output proof tests

```text
18. accepted command -> ControlAck accepted + CAN_TX_RAW evidence pair
19. rejected command -> ControlAck rejected + no CAN_TX_RAW for that command
20. local_tx_inhibit -> CanTxEnable LOW evidence
```

### Build guard tests

```text
21. monitor-only build fails if HostCanTxRequest/raw TX symbols exist
22. remote-off build fails if CRSF/RemoteControlSource symbols exist
23. raw-can-off build fails if raw CAN downlink handler exists
```

---

## 14. What Codex must not do

Do not:

```text
- make M4 send CAN or create CAN frames
- let RemoteControlSource know CAN IDs or byte layouts
- let VSM/raw host command bypass AuthorityManager
- use RC queue backlog for control
- allow local control when autonomy is Unknown/Ambiguous/RecentlyActive/ProtocolFault
- rely only on UI/VSM status to decide authority
- implement direct CAN TX from multiple modules
- weaken existing passive safety guard
- mix VSM hot-path telemetry with control hot path
```

---

## 15. Final definition of success

This project succeeds when the following statement is true and testable:

```text
The CSM never emits a local motion-control CAN frame unless upstream autonomous authority is positively released, the handoff window is complete, the local source is fresh/valid/neutral, explicit local takeover is requested, SafetySupervisor permits control, AuthorityManager grants source authority, CanTxGateway permits TX, and the hardware CanTxEnable gate is in the correct state.
```

Short form:

```text
No confirmed autonomy release = zero local CAN TX.
```

This is the final design direction Codex must preserve.

