ENTRY(entry)

SECTIONS
{
	. = 0x400000 + SIZEOF_HEADERS;                /* can be observed by gdb or objdump -d */

	segment1 : { *(.text) *(.data) *(.rodata) }   /* can be observed by readelf -S */

	/DISCARD/ : { *(.comment) }
}