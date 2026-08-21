MEMORY
{
  /* Portenta H7 M7 product slot: 0x08040000..0x08100000. */
  FLASH (rx) : ORIGIN = 0x08040000, LENGTH = 0x000C0000
  DTCMRAM (rwx) : ORIGIN = 0x20000000 + (((166 * 4) + 7) & 0xFFFFFFF8), LENGTH = 128K - (((166 * 4) + 7) & 0xFFFFFFF8)
  RAM (xrw) : ORIGIN = 0x24000000, LENGTH = 0x80000
  RAM_D2 (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3 (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
  ITCMRAM (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
}

/*
 * D2 is one physical SRAM seen at 0x30000000 by M7 and 0x10000000 by M4.
 * The paired M4 linker stops at physical 0x30040000.  The final 32 KiB is
 * therefore owned exclusively by the M7 network DMA/lwIP sections.
 */
__csm_m4_d2_physical_end__ = 0x30040000;
__csm_m7_network_d2_start__ = 0x30040000;
__csm_m7_network_d2_end__ = 0x30048000;

/*
 * Product M7 excludes RPC/OpenAMP. Keep the framework compatibility symbols
 * as an empty range after the dedicated lwIP heap; map guards reject any
 * OpenAMP allocation.
 */
__OPENAMP_region_start__ = 0x3800F800;
__OPENAMP_region_end__ = 0x3800F800;
ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)
        KEEP(*(.init))
        KEEP(*(.fini))
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)
        *(.rodata*)
        KEEP(*(.eh_frame*))
        *ltrans0*.o(.rodata*)
        *ltrans1*.o(.rodata*)
        *ltrans2*.o(.rodata*)
        *ltrans3*.o(.rodata*)
        *ltrans4*.o(.rodata*)
        *lib*.o(.rodata*)
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH
    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;
    __etext = .;
    _sidata = .;

    .data : AT (__etext)
    {
        __data_start__ = .;
        _sdata = .;
        *(vtable)
        *(.data*)
        . = ALIGN(8);
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
        . = ALIGN(8);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
        . = ALIGN(8);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);
        KEEP(*(.jcr*))
        . = ALIGN(8);
        __data_end__ = .;
        _edata = .;
    } > RAM

    .uninitialized (NOLOAD):
    {
        . = ALIGN(32);
        __uninitialized_start = .;
        *(.uninitialized)
        KEEP(*(.keep.uninitialized))
        . = ALIGN(32);
        __uninitialized_end = .;
    } > RAM

    .bss :
    {
        . = ALIGN(8);
        __bss_start__ = .;
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
        _ebss = .;
    } > RAM

    /*
     * Pinned lwIP heap. The patched Mbed archive places lwip_ram_heap in the
     * dedicated input section instead of D2 .ethusbram. The fixed envelope
     * makes a profile/config drift fail at link time.
     */
    .csm_lwip_heap_d3 0x38000400 (NOLOAD):
    {
        __csm_lwip_heap_start__ = .;
        KEEP(*(.csm_lwip_heap_d3))
        __csm_lwip_heap_object_end__ = .;
        . = ABSOLUTE(0x3800A800);
        __csm_lwip_heap_end__ = .;
    } > RAM_D3

    /* REV.B fixed, non-cacheable M7/M4 control and health exchange. */
    .csm_control_ipc_d3 0x3800A800 (NOLOAD):
    {
        __csm_control_ipc_start__ = .;
        KEEP(*(.csm_control_ipc_d3))
        . = ABSOLUTE(0x3800B800);
        __csm_control_ipc_end__ = .;
    } > RAM_D3

    /* M4 producer / M7 consumer CAN1 raw ring: 512 fixed 32-byte entries. */
    .csm_can1_raw_ring_d3 0x3800B800 (NOLOAD):
    {
        __csm_can1_raw_ring_start__ = .;
        KEEP(*(.csm_can1_raw_ring_d3))
        . = ABSOLUTE(0x3800F800);
        __csm_can1_raw_ring_end__ = .;
    } > RAM_D3

    .pdm_section 0x3800FC00 (NOLOAD):
    {
        *(.pdm_buffer)
    } > RAM_D3

    _dtcm_lma = __etext + SIZEOF(.data);
    .dtcm : AT(_dtcm_lma)
    {
        _sdtcm = .;
        *(.dtcm*)
        _edtcm = .;
    } > DTCMRAM

    /*
     * Raw CPU-only Wi-Fi queue arena. Queue cursors/atomics remain in D1;
     * NOLOAD bytes are never read before a producer commits a descriptor.
     */
    .wifi_tx_queue_dtcm (NOLOAD):
    {
        . = ALIGN(32);
        __wifi_tx_queue_start__ = .;
        KEEP(*(.wifi_tx_queue_dtcm))
        . = ALIGN(16);
        __wifi_tx_queue_end__ = .;
    } > DTCMRAM

    /*
     * Other explicitly initialized CPU-only arenas belong here. Startup code
     * does not zero this NOLOAD section. DMA and M4-shared objects are
     * forbidden here.
     */
    .csm_dtcm_bss (NOLOAD):
    {
        . = ALIGN(32);
        __csm_dtcm_bss_start__ = .;
        KEEP(*(.csm_dtcm_bss))
        KEEP(*(.csm_dtcm_bss.*))
        . = ALIGN(32);
        __csm_dtcm_bss_end__ = .;
    } > DTCMRAM

    .heap (COPY):
    {
        __end__ = .;
        PROVIDE(end = .);
        *(.heap*)
        . = ORIGIN(RAM) + LENGTH(RAM) - 0x400;
        __HeapLimit = .;
    } > RAM

    .stack_dummy (COPY):
    {
        *(.stack*)
    } > RAM
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    _estack = __StackTop;
    __StackLimit = __StackTop - 0x400;
    PROVIDE(__stack = __StackTop);
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")

    .lwip_sec (NOLOAD):
    {
        . = ABSOLUTE(0x30040000);
        __csm_lwip_actual_start__ = .;
        *(.RxDecripSection)
        . = ABSOLUTE(0x30040100);
        *(.TxDecripSection)
        . = ABSOLUTE(0x30040400);
        *(.RxArraySection)
        . = ABSOLUTE(0x30044000);
        *(.ethusbram)
        __csm_lwip_actual_end__ = .;
    } > RAM_D2 AT> FLASH

    ASSERT(__csm_dtcm_bss_end__ <= ORIGIN(DTCMRAM) + LENGTH(DTCMRAM),
           "CSM DTCM arena overflow")
    ASSERT(__wifi_tx_queue_end__ - __wifi_tx_queue_start__ == 0xD000,
           "Wi-Fi live FIFO storage size drift")
    ASSERT(__csm_dtcm_bss_end__ <=
               ORIGIN(DTCMRAM) + LENGTH(DTCMRAM) - 0x10000,
           "CSM DTCM arenas consumed safety reserve")
    ASSERT(__csm_lwip_actual_start__ >= __csm_m7_network_d2_start__,
           "M7 network section overlaps M4-owned D2")
    ASSERT(__csm_lwip_actual_end__ <= __csm_m7_network_d2_end__,
           "M7 network section exceeds reserved D2 window")
    ASSERT(__csm_lwip_heap_object_end__ <= __csm_lwip_heap_end__,
           "lwIP heap exceeds fixed D3 envelope")
    ASSERT(__csm_lwip_heap_object_end__ - __csm_lwip_heap_start__ == 0xA013,
           "pinned lwIP heap object missing or wrong size")
    ASSERT(SIZEOF(.csm_lwip_heap_d3) == 0xA400,
           "lwIP D3 heap envelope size drift")
    ASSERT(SIZEOF(.csm_control_ipc_d3) == 0x1000,
           "Control/Health IPC D3 envelope size drift")
    ASSERT(SIZEOF(.csm_can1_raw_ring_d3) == 0x4000,
           "CAN1 raw ring D3 envelope size drift")
    ASSERT(__csm_control_ipc_start__ == 0x3800A800 &&
           __csm_control_ipc_end__ == 0x3800B800,
           "Control/Health IPC address drift")
    ASSERT(__csm_can1_raw_ring_start__ == 0x3800B800 &&
           __csm_can1_raw_ring_end__ == 0x3800F800,
           "CAN1 raw ring address drift")
    ASSERT(__OPENAMP_region_start__ == __OPENAMP_region_end__,
           "OpenAMP must remain excluded from the M7 product image")
}
