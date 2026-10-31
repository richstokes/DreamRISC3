/* Development harness: runs the recompiled ROM on the Mac with deterministic
 * time and writes PPM screenshots. Not part of the Dreamcast build. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cpu.h"

static uint64_t virtual_us;
static u32 us_step = 40, us_jitter, jitter = 12345;
static uint64_t stop_us;
static const char *shot_path;
static int frames;
static screen_t last;

uint64_t plat_time_us(void)
{
		jitter = jitter * 1103515245u + 12345u;
	return virtual_us += us_step + (jitter >> 16) % (us_jitter + 1);
}

void plat_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void write_shot(void);

void plat_fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "virtual time %.2fs, frames %d\n", virtual_us / 1e6, frames);
	for (int i = 0; i < 15; i++)
		fprintf(stderr, "r%-2d %08X%s", i, R(i), i % 4 == 3 ? "\n" : "  ");
	fprintf(stderr, "\n");
	write_shot();
	{ void mach_dump(void); mach_dump(); }
	{
		extern u32 mach_miss_pc;
		u32 mach_peek(u32 addr);
		FILE *f = fopen("build/miss.txt", "w");
		for (u32 a = (mach_miss_pc & ~3u) - 0x800; f && mach_miss_pc && a < mach_miss_pc + 0x800; a += 4)
			fprintf(f, "%08X\n", mach_peek(a));
		if (f)
			fclose(f);
	}
	exit(1);
}

static u32 rgb(u32 entry)
{
	u32 r = (entry & 15) * 17, g = ((entry >> 4) & 15) * 17, b = ((entry >> 8) & 15) * 17;
	return r | (g << 8) | (b << 16);
}

static void write_shot(void)
{
	const screen_t *s = &last;
	FILE *f;
	if (!shot_path || s->width <= 0 || s->height <= 0)
		return;
	f = fopen(shot_path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
	u32 addr = s->start, line = (u32)(s->width * s->bpp / 8);
	for (int y = 0; y < s->height; y++) {
		for (int x = 0; x < s->width; x++) {
			u32 bit = (u32)x * (u32)s->bpp, a = addr + bit / 8;
			if (a >= s->end)
				a = s->base + (a - s->end);
			u32 v = (s->ram[a] >> (bit & 7)) & ((1u << s->bpp) - 1);
			u32 c = rgb(s->palette[v & 15]);
			if (s->bpp == 8) {
				c = (c & 0x77) | ((v & 0x10) ? 0x88 : 0);
				c |= (rgb(s->palette[v & 15]) & 0x7700) | ((v & 0x20) ? 0x4400 : 0);
				c |= (rgb(s->palette[v & 15]) & 0x770000) | ((v & 0x80) ? 0x880000 : 0);
			}
			int cx = x - s->cursor_x, cy = y - s->cursor_y;
			if (cx >= 0 && cx < 32 && cy >= 0 && cy < s->cursor_h) {
				u32 p = (s->ram[s->cursor_addr + (u32)cy * 8 + (u32)(cx >> 2)] >> ((cx & 3) * 2)) & 3;
				if (p)
					c = rgb(s->cursor_palette[p - 1]);
			}
			putc(c & 255, f);
			putc((c >> 8) & 255, f);
			putc((c >> 16) & 255, f);
		}
		addr += line;
		if (addr >= s->end)
			addr = s->base + (addr - s->end);
	}
	fclose(f);
}

void plat_frame(const screen_t *screen)
{
	last = *screen;
	frames++;
	if (virtual_us >= stop_us) {
		write_shot();
		mach_stop();
	}
}

/* Scripted input: "<seconds> move <dx> <dy>", "<seconds> button <mask>",
 * "<seconds> key <row> <col> <0|1>", "<seconds> shot <path>". */
static FILE *replay;
static double next_time = -1;
static char next_verb[32], next_arg[256];
static int pending_dx, pending_dy, button_mask;

static void replay_read(void)
{
	char line[320];
	next_time = -1;
	while (replay && fgets(line, sizeof(line), replay)) {
		next_arg[0] = 0;
		if (line[0] != '#' && sscanf(line, "%lf %31s %255[^\n]", &next_time, next_verb, next_arg) >= 2)
			return;
		next_time = -1;
	}
}

void plat_poll_input(void)
{
	int dx, dy;
	while (next_time >= 0 && virtual_us / 1e6 >= next_time) {
		int a = 0, b = 0, c = 0;
		sscanf(next_arg, "%d %d %d", &a, &b, &c);
		if (!strcmp(next_verb, "move")) {
			pending_dx += a;
			pending_dy += b;
		} else if (!strcmp(next_verb, "button")) {
			button_mask = a;
		} else if (!strcmp(next_verb, "key")) {
			mach_key(a, b, c);
		} else if (!strcmp(next_verb, "shot")) {
			const char *saved = shot_path;
			shot_path = next_arg;
			write_shot();
			shot_path = saved;
			fprintf(stderr, "sum=%08X ", mach_screen_sum(&last));
			fprintf(stderr, "shot %s at %.2fs cursor %d,%d h=%d addr=%X\n", next_arg, virtual_us / 1e6,
				last.cursor_x, last.cursor_y, last.cursor_h, last.cursor_addr);
		}
		replay_read();
	}
	dx = pending_dx > 16 ? 16 : pending_dx < -16 ? -16 : pending_dx;
	dy = pending_dy > 16 ? 16 : pending_dy < -16 ? -16 : pending_dy;
	pending_dx -= dx;
	pending_dy -= dy;
	mach_mouse(dx, dy, button_mask);
}

static u8 rom[0x200000];
static u32 pages_touched[2];
static u32 page_loads[64];

void plat_rom_read(u32 page, u8 *dest)
{
	memcpy(dest, rom + page * 0x8000, 0x8000);
	pages_touched[page >> 5] |= 1u << (page & 31);
	page_loads[page]++;
}

const ov_header_t *plat_overlay_load(int unit)
{
	extern const ov_header_t *const ov_headers[];
	fprintf(stderr, "overlay %d loaded at %.2fs\n", unit, virtual_us / 1e6);
	return ov_headers[unit];
}

static u8 *read_file(const char *path, size_t want, u8 *dest)
{
	FILE *f = fopen(path, "rb");
	if (!f || fread(dest, 1, want, f) != want) {
		fprintf(stderr, "cannot read %s\n", path);
		exit(1);
	}
	fclose(f);
	return dest;
}

int main(int argc, char **argv)
{
	static u8 cmos[256];
	static const char *chips[4] = {"IC24.ROM", "IC25.ROM", "IC26.rom", "IC27.ROM"};
	char path[1024];
	if (argc < 4) {
		fprintf(stderr, "usage: %s rom_dir cmos.bin seconds [shot.ppm]\n", argv[0]);
		return 2;
	}
	for (int i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "%s/%s", argv[1], chips[i]);
		read_file(path, 0x80000, rom + i * 0x80000);
	}
	read_file(argv[2], 256, cmos);
	stop_us = (uint64_t)(atof(argv[3]) * 1e6);
	shot_path = argc > 4 ? argv[4] : NULL;
	if (getenv("DREAM_US_STEP"))
		us_step = (u32)atoi(getenv("DREAM_US_STEP"));
	if (getenv("DREAM_SEED"))
		jitter = (u32)atoi(getenv("DREAM_SEED"));
	if (getenv("DREAM_US_JITTER"))
		us_jitter = (u32)atoi(getenv("DREAM_US_JITTER"));
	if (getenv("DREAM_REPLAY")) {
		replay = fopen(getenv("DREAM_REPLAY"), "r");
		replay_read();
	}
	mach_init(cmos);
	mach_run(0);
	{
		extern u32 mach_pc_ring[256];
		void mach_dump(void);
		mach_dump();
		for (int i = 0; i < 256; i += 16)
			fprintf(stderr, "%07X ", mach_pc_ring[i]);
		fprintf(stderr, "\nregs:");
		for (int i = 0; i < 15; i++)
			fprintf(stderr, " %08X", R(i));
		fprintf(stderr, "\n");
	}
	{
		u32 mach_peek(u32 addr);
		for (u32 v = 0; v < 0x20; v += 4)
			fprintf(stderr, "vector %02X %08X\n", v, mach_peek(v));
	}
	fprintf(stderr, "overlay loads %u\n", mach_overlay_loads());
	fprintf(stderr, "rom misses %u, pages touched %d\n", mach_rom_misses(),
		__builtin_popcount(pages_touched[0]) + __builtin_popcount(pages_touched[1]));
	fprintf(stderr, "page loads:");
	for (int i = 0; i < 64; i++)
		fprintf(stderr, " %u", page_loads[i]);
	fprintf(stderr, "\n");
	fprintf(stderr, "screen sum %08X\n", mach_screen_sum(&last));
	fprintf(stderr, "stopped at %.2fs virtual, %d frames, %dx%d %dbpp\n", virtual_us / 1e6,
		frames, last.width, last.height, last.bpp);
	return 0;
}
