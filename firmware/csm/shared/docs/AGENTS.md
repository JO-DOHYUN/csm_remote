# AGENTS.md - Shared Contract Docs Router

## Scope
This file applies to `firmware/csm/shared/docs/**`.

These documents define CSM/VSM typed transport and shared evidence contracts.

## Required Context
For CSM Remote changes, read:

1. `../../../../AGENTS.md`
2. `../../../../docs/remote/AGENTS.md`
3. `../../AGENTS.md`

## Rules
- Wire-format changes must be synchronized with firmware code and VSM parser
  work.
- Remote-control evidence must keep request, board acceptance, actual TX, and
  feedback separated.
- Do not reinterpret `CONTROL_ACK` as physical CAN TX success.
- Update `../../../../docs/remote/product/REQUIREMENT_TRACE_KO.md` when a remote
  requirement is satisfied or changed.
