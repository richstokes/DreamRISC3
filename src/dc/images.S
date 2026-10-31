! ROM and CMOS images. The ROM comes from the build machine and is never committed.
	.section .rodata
	.balign 32
	.global _rom_image
_rom_image:
	.incbin "build/rom.bin"
	.global _cmos_image
_cmos_image:
	.incbin "assets/cmos.bin"
