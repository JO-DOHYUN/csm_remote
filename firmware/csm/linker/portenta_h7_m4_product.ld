MEMORY
{
    /* Portenta H7 M4 product slot selected by this board profile. */
    FLASH (rx) : ORIGIN = 0x08100000, LENGTH = 0x00100000
    /*
     * 0x10000000 is the M4 alias of physical D2 at 0x30000000.  Stop at
     * 0x10040000 so the final physical 32 KiB remains exclusively owned by
     * M7 lwIP/DMA.  The offset reserves the ROM startup vector table.
     */
    RAM_D2 (rwx) : ORIGIN = 0x10000000 + (((166 * 4) + 7) & 0xFFFFFFF8), LENGTH = 0x40000 - (((166 * 4) + 7) & 0xFFFFFFF8)
    RAM_D3 (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

    REGION_ALIAS("RAM", RAM_D2);

__csm_m4_d2_end__ = 0x10040000;
__OPENAMP_region_start__ = 0x38000400;
__OPENAMP_region_end__ = 0x38000400 + LENGTH(RAM_D3) - 1K;
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

    .pdm_section 0x3800FC00 (NOLOAD):
    {
        *(.pdm_buffer)
    } > RAM_D3

    .heap (NOLOAD):
    {
        __end__ = .;
        PROVIDE(end = .);
        *(.heap*)
        . = ORIGIN(RAM) + LENGTH(RAM) - 0x400;
        __HeapLimit = .;
    } > RAM

    .stack_dummy (NOLOAD):
    {
        *(.stack*)
    } > RAM
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    _estack = __StackTop;
    __StackLimit = __StackTop - 0x400;
    PROVIDE(__stack = __StackTop);
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")
    ASSERT(__StackTop <= __csm_m4_d2_end__,
           "M4 D2 region overlaps M7 network window")
}
