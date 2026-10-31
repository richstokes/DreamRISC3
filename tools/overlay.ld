/* One overlay unit, run from the region reserved in the main ELF. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh4)
SECTIONS
{
	. = REGION;
	.text : {
		KEEP(*(.ovhdr))
		*(.text .text.*)
		*(.rodata .rodata.*)
	}
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.debug*) }
}
