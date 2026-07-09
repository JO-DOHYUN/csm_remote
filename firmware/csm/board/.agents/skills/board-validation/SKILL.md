---
name: board-validation
description: Use when preparing HIL, flood, pulse injection, ADC burst, reconnect, or soak validation for the board.
---

# board-validation

## Purpose
Detect hidden loss, timing drift, unsafe control behavior, and cross-lane starvation.

## Validation Set
- CAN flood
- encoder pulse injection
- ADC burst with concurrent CAN load
- reconnect / lease timeout / estop
- power-loss recovery
- long soak
- passive hotplug: USB plug/unplug, VSM connect/disconnect, debug tap on/off,
  MCU reset/bootloader/upload while reference CAN analyzer watches error frames
- host absent no-replay: frames received while CDC/DTR absent must not be staged
  or replayed after host session open

## Rules
- validation must measure evidence quality, not just functionality
- any loss must be localized to lane and time range
- if exact measurement is unavailable, say so
- Passive Product PASS requires two-bus RX capability and external analyzer,
  scope/hotplug, and DTC evidence. Capability hardware fields are claims only.
- USB attach quarantine is CDC/uplink/session cleanup only; CAN front-end drain
  must continue.

## Output
- test command or setup
- expected result
- observed limitation
- release blocker or non-blocker classification
