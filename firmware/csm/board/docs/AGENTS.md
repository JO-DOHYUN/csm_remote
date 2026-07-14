# AGENTS.md - Board Docs Router

## Scope
This file applies to `firmware/csm/board/docs/**`.

Board docs define imported CSM hardware, passive-product, safety, and HIL
reference material. They do not override CSM Remote product authority.

## Required Context
For CSM Remote tasks, read:

1. `../../../../AGENTS.md`
2. `../../../../docs/remote/AGENTS.md`
3. `../AGENTS.md`
4. `../BRIEF.md`

## Rules
- Hardware docs may constrain what is physically possible, but they must not
  silently authorize remote vehicle CAN TX.
- HIL/full-instrumented evidence is not passive or remote product evidence unless
  the remote product docs explicitly accept it.
- If pin, carrier, bus, or transceiver assumptions change, update the remote
  open decisions and requirement trace.
