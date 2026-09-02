# Pinned Portenta H7 Mbed product archive

This directory is the reproducible source contract for the CSM production
`libmbed.a`.  The normal PlatformIO package is never modified in place.

The product archive is built from:

1. the pinned Mbed OS commit in `manifest.json`;
2. the exact ArduinoCore-mbed 4.3.1 patch tree;
3. the CSM patches in `patches/`;
4. `mbed_app.product.json`.

`tools/build_pinned_mbed.ps1` rebuilds the network slice, overlays its 67
objects onto the hash-pinned Arduino Mbed 4.3.1 archive, and writes the
generated artifact manifest. Replacement-object debug sections are stripped
before packaging; release code and relocation data remain unchanged. Product PlatformIO environments resolve
`libmbed.a` from the checked artifact directory and force-include the narrow
`csm_product_mbed_overrides.h` application profile. Arduino's complete target
configuration remains authoritative, so core and archive ABI do not drift;
the installed framework is never modified.

PlatformIO links both Arduino core and Mbed with `--whole-archive`. The
repacked archive therefore removes `mstd_mutex.o`, whose identical source is
owned and compiled by FrameworkArduino, preventing duplicate strong symbols.

Release observability remains in the canonical CSM link-reliability record.
Deep worker diagnostics are compile-time optional and do not place logging,
allocation, or socket work in the real-time producer path.
