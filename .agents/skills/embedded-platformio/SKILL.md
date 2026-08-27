---
name: embedded-platformio
kind: procedure
description: Use for CSM PlatformIO/Mbed/ARM build configuration, exact M4/M7 artifact builds, compile database, linker, or toolchain diagnostics.
---

# Embedded PlatformIO

## Purpose
Operate the existing deterministic embedded toolchain without global installation or PATH churn.

## Inputs
Exact environment, target core, baseline, requested artifact or diagnostic.

## Authority to Read
Git identity, active manifest, `docs/operations/DEVELOPMENT_SETUP_KO.md`, platformio.ini,
and affected linker/build scripts.

## Procedure
1. Resolve the actual existing PlatformIO executable and environment.
2. Inspect effective source filter, flags, framework/toolchain and pinned artifact identity.
3. Build only the requested exact environment.
4. Treat compile DB as generated analysis input tied to its generating environment, never product authority.
5. Record artifact/hash/resource output without claiming device execution.

## Stop / Escalation
Do not install packages, alter global PATH, upload, or change environment ownership without authorization.

## Required Verification
Harness/guards plus exact build or compiledb command; inspect generated identity.

## Output Contract
Executable/env, command, compile/artifact result, warnings, and unexecuted device/HIL.
