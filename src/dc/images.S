! CMOS defaults. The ROM is read from the disc at boot and is never committed.
	.section .rodata
	.balign 4
	.global _cmos_image
_cmos_image:
	.incbin "assets/cmos.bin"
