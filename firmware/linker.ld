ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 512K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .text : { KEEP(*(.isr_vector)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : { . = ALIGN(4); _data_start = .; *(.data*) . = ALIGN(4); _data_end = .; } > RAM AT > FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(4); _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    ASSERT(_bss_end < _stack_top - 4096, "RAM exhausted: reserve 4 KiB exception stack")
}
