# Wi-Fi AP init S1 result — 2026-07-30

## Applied boundary

- Portenta WHD `cyhal_sdio_send_cmd()` no longer enters `while (1)` on an SDIO
  command error; it returns the failure to the AP/Wi-Fi worker boundary.
- The pinned Mbed overlay explicitly replaces `cyhal_sdio.c.obj`.
- Product D3 is `Normal, shareable, non-cacheable` (`TEX=1,C=0,B=0`), and the
  runtime setup does not globally disable/re-enable the MPU.

## One executed gate

`portenta_h7_m7_mid_feeder_uart_j4_remote_product_wifi` rebuilt successfully.

- pinned `libmbed.a`: `FC5704A9044957242F25B22D160E5B56157A3906888821574E42A3D37DE10C59`
- overlay archive contains `cyhal_sdio.o`: pass
- M7 `firmware.bin` SHA-256:
  `A3225B41A1ACFA6A91FC6945498EFAE0222ECDEA8F6BB97074899F7E5A769851`
- M7 binary size: 365,820 B

## Not executed / result

Upload to the physically enumerated `COM7` was attempted once and failed before
DFU transfer (`OSError(22): no such device`). A subsequent 1200-bps touch could
not open COM7. Therefore AP start, watchdog-reset absence, USB continuity, and
CAN continuity are **not claimed**. No additional upload retry was made while
the owner was away.

## Next physical gate

When COM7/DFU is reachable, upload this exact binary once and observe a single
fresh boot for at least 30 seconds: AP visible, no boot-sequence increment,
`BeginAccessPoint` returned or an explicit Wi-Fi error, and uninterrupted USB
and CAN health evidence.
