# Passive Deferred CAN Front-End Init

## Reason
The field product must support two-bus ACK-capable observe after a stable host session, while USB power-up / CDC enumeration must not initialize or reconfigure the CAN front-end while the board is already attached to vehicle CAN.

## Decision
The single CSM product build, `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`, defers both MCP2515 and built-in CAN front-end initialization until CDC/DTR session stability plus the configured quiet window. It emits `CAN_FRONTEND_PRESESSION_HOLD`, then `CAN_FRONTEND_SESSION_READY` after deferred initialization succeeds.

## Expected Gain
This removes boot-time CAN front-end setup from the USB attach window and prevents stale pre-session payload replay. It preserves ACK-capable observe after the host session is stable.

## Rollback Rule
Rollback only if external analyzer/scope evidence proves the deferred-init sequence causes worse hotplug disturbance than the previous boot-time initialization. Any rollback must keep host TX/control/downlink compiled out of the passive product.
