/* Dreamcast platform layer: KallistiOS video, Maple input and startup for the
 * natively recompiled RISC OS 3.11. */
#include <kos.h>
#include <dc/sq.h>
#include <arch/cache.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../cpu.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

extern const u8 cmos_image[];

#define MAX_LINE_BYTES 160
#define MAX_LINES 256
#define CURSOR_LINE_BYTES 8

static u8 shadow[MAX_LINES][MAX_LINE_BYTES];
static uint16_t row[640] __attribute__((aligned(32)));
static uint16_t lut[256];
static u32 last_pal[20];
static int last_w, last_h, last_bpp, frame_count;
static int old_cursor_y, old_cursor_h;
#ifdef HW_REPLAY
static screen_t replay_screen;
#endif

uint64_t plat_time_us(void)
{
	return timer_us_gettime64();
}

void plat_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void plat_fatal(const char *fmt, ...)
{
	char text[160];
	va_list ap;
	extern void mach_dump(void);
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	printf("FATAL: %s", text);
	mach_dump();
	bfont_draw_str(vram_s + 640 * 8 + 8, 640, 1, text);
	for (;;)
		thd_sleep(1000);
}

/* Files live on the disc. Under dcload the host serves them over /pc instead. */
static file_t open_data(const char *name)
{
	char path[192];
	file_t f;
	snprintf(path, sizeof(path), "/cd/%s", name);
	f = fs_open(path, O_RDONLY);
	if (f == FILEHND_INVALID) {
#ifndef PC_ROOT
#define PC_ROOT ""
#endif
		snprintf(path, sizeof(path), "/pc%s/%s", PC_ROOT, name);
		f = fs_open(path, O_RDONLY);
	}
	if (f == FILEHND_INVALID)
		plat_fatal("cannot open %s\n", name);
	return f;
}

static file_t rom_file = FILEHND_INVALID;

void plat_rom_read(u32 page, u8 *dest)
{
	if (rom_file == FILEHND_INVALID)
		rom_file = open_data("rom.bin");
	/* The read may bypass the cache, so leave no stale lines behind it */
	dcache_purge_range((uintptr_t)dest, 0x8000);
	fs_seek(rom_file, (off_t)page * 0x8000, SEEK_SET);
	if (fs_read(rom_file, dest, 0x8000) != 0x8000)
		plat_fatal("short read of ROM page %lu\n", (unsigned long)page);
}

/* Filled from disc; each unit is linked to run at this address. */
u8 overlay_region[OVERLAY_BYTES] __attribute__((aligned(32)));

const ov_header_t *plat_overlay_load(int unit)
{
	char name[16];
	const ov_header_t *h = (const ov_header_t *)overlay_region;
	file_t f;
	ssize_t got;
	snprintf(name, sizeof(name), "ov_%d.bin", unit);
	f = open_data(name);
	/* The read may bypass the cache, so leave no stale lines behind it */
	dcache_purge_range((uintptr_t)overlay_region, OVERLAY_BYTES);
	got = fs_read(f, overlay_region, OVERLAY_BYTES);
	fs_close(f);
	if (got <= 0 || h->magic != OV_MAGIC)
		plat_fatal("bad overlay %s\n", name);
	dcache_flush_range((uintptr_t)overlay_region, (size_t)got);
	icache_flush_range((uintptr_t)overlay_region, (size_t)got);
	return h;
}

static inline uint16_t vidc_565(u32 e)
{
	u32 r = e & 15, g = (e >> 4) & 15, b = (e >> 8) & 15;
	return (uint16_t)((r << 12) | (r << 8 & 0x800) | (g << 7) | (g << 3 & 0x60) | (b << 1) | (b >> 3));
}

static void build_lut(const screen_t *s)
{
	int i;
	for (i = 0; i < (1 << s->bpp); i++) {
		u32 e = s->palette[i & 15];
		if (s->bpp == 8)
			e = (e & 0x737) | ((i & 0x10) >> 1) | ((i & 0x60) << 1) | ((i & 0x80) << 4);
		lut[i] = vidc_565(e);
	}
}

static void expand_line(const screen_t *s, const u8 *src, int width)
{
	int x;
	switch (s->bpp) {
	case 4:
		for (x = 0; x < width; x += 2) {
			u8 v = src[x >> 1];
			row[x] = lut[v & 15];
			row[x + 1] = lut[v >> 4];
		}
		break;
	case 8:
		for (x = 0; x < width; x++)
			row[x] = lut[src[x]];
		break;
	default:
		for (x = 0; x < width; x++) {
			u32 bit = (u32)x * (u32)s->bpp;
			row[x] = lut[(src[bit >> 3] >> (bit & 7)) & ((1u << s->bpp) - 1)];
		}
		break;
	}
}

static void overlay_cursor(const screen_t *s, int y, int width)
{
	int line = y - s->cursor_y, x;
	u32 addr;
	if (line < 0 || line >= s->cursor_h)
		return;
	addr = s->cursor_addr + (u32)line * CURSOR_LINE_BYTES;
	for (x = 0; x < 32; x++) {
		u32 v = (s->ram[addr + (u32)(x >> 2)] >> ((x & 3) * 2)) & 3;
		int px = s->cursor_x + x;
		if (v && px >= 0 && px < width)
			row[px] = vidc_565(s->cursor_palette[v - 1]);
	}
}

static inline void put_row(int y, int height, int width)
{
	int first = y * 480 / height, last = (y + 1) * 480 / height, d;
	for (d = first; d < last; d++)
		sq_cpy(vram_s + d * 640, row, (size_t)width * 2);
}

/* Redraw only lines that changed, plus the lines under the pointer. */
void plat_frame(const screen_t *s)
{
#ifdef HW_REPLAY
	replay_screen = *s;
#endif
#ifdef HW_LOG
	/* Hardware runs have no video capture: one line every 5s reports progress.
	 * dcload printf runs with interrupts off, so never log more often. */
	static uint64_t next_log;
	static u32 log_frames, log_polls;
	static int dumped;
	uint64_t now = timer_us_gettime64();
	log_frames++;
	if (now >= next_log) {
		u32 polls = mach_polls();
		printf("hb t=%llu frames=%lu polls=%lu mode=%dx%dx%d sum=%08lX\n",
			(unsigned long long)(now / 1000000), (unsigned long)log_frames,
			(unsigned long)(polls - log_polls), s->width, s->height, s->bpp,
			(unsigned long)mach_screen_sum(s));
		{
			extern u32 mach_pc_ring[256];
			extern void mach_dump(void);
			printf("hb pcs %07lX %07lX %07lX %07lX\n", (unsigned long)mach_pc_ring[0],
				(unsigned long)mach_pc_ring[85], (unsigned long)mach_pc_ring[170],
				(unsigned long)mach_pc_ring[255]);
			if (s->bpp == 1 && now > 30000000 && !dumped) {
				dumped = 1;
				mach_dump();
			}
		}
		log_polls = polls;
		next_log = now + 5000000;
	}
#endif
	u32 addr = s->start, line_bytes = (u32)(s->width * s->bpp / 8);
	int width = s->width > 640 ? 640 : s->width;
	int height = s->height > MAX_LINES * 2 ? MAX_LINES * 2 : s->height;
	int y, full;
	if (++frame_count & 1)
		return;
	if (width <= 0 || height <= 0 || line_bytes == 0)
		return;
	full = width != last_w || height != last_h || s->bpp != last_bpp ||
	       memcmp(last_pal, s->palette, sizeof(last_pal));
	if (full) {
		memcpy(last_pal, s->palette, sizeof(last_pal));
		last_w = width;
		last_h = height;
		last_bpp = s->bpp;
		build_lut(s);
		vid_clear(0, 0, 0);
	}
	for (y = 0; y < height && y < MAX_LINES; y++) {
		const u8 *src = s->ram + addr;
		int cursor_now = y >= s->cursor_y && y < s->cursor_y + s->cursor_h;
		int cursor_old = y >= old_cursor_y && y < old_cursor_y + old_cursor_h;
		u32 n = line_bytes > MAX_LINE_BYTES ? MAX_LINE_BYTES : line_bytes;
		if (full || cursor_now || cursor_old || memcmp(shadow[y], src, n)) {
			memcpy(shadow[y], src, n);
			expand_line(s, src, width);
			overlay_cursor(s, y, width);
			put_row(y, height, width);
		}
		addr += line_bytes;
		if (addr >= s->end)
			addr = s->base + (addr - s->end);
	}
	old_cursor_y = s->cursor_y;
	old_cursor_h = s->cursor_h;
}

/* USB HID code to Archimedes keyboard row and column; 0xFF is unmapped. */
#define K(r, c) (((r) << 4) | (c))
static const u8 hid_map[0x64] = {
	[0x04] = K(3,12), [0x05] = K(5,2), [0x06] = K(5,0), [0x07] = K(3,14), [0x08] = K(2,9),
	[0x09] = K(3,15), [0x0A] = K(4,0), [0x0B] = K(4,1), [0x0C] = K(2,14), [0x0D] = K(4,2),
	[0x0E] = K(4,3), [0x0F] = K(4,4), [0x10] = K(5,4), [0x11] = K(5,3), [0x12] = K(2,15),
	[0x13] = K(3,0), [0x14] = K(2,7), [0x15] = K(2,10), [0x16] = K(3,13), [0x17] = K(2,11),
	[0x18] = K(2,13), [0x19] = K(5,1), [0x1A] = K(2,8), [0x1B] = K(4,15), [0x1C] = K(2,12),
	[0x1D] = K(4,14), [0x1E] = K(1,1), [0x1F] = K(1,2), [0x20] = K(1,3), [0x21] = K(1,4),
	[0x22] = K(1,5), [0x23] = K(1,6), [0x24] = K(1,7), [0x25] = K(1,8), [0x26] = K(1,9),
	[0x27] = K(1,10), [0x28] = K(4,7), [0x29] = K(0,0), [0x2A] = K(1,14), [0x2B] = K(2,6),
	[0x2C] = K(5,15), [0x2D] = K(1,11), [0x2E] = K(1,12), [0x2F] = K(3,1), [0x30] = K(3,2),
	[0x31] = K(3,3), [0x33] = K(4,5), [0x34] = K(4,6), [0x35] = K(1,0), [0x36] = K(5,5),
	[0x37] = K(5,6), [0x38] = K(5,7), [0x39] = K(5,13), [0x3A] = K(0,1), [0x3B] = K(0,2),
	[0x3C] = K(0,3), [0x3D] = K(0,4), [0x3E] = K(0,5), [0x3F] = K(0,6), [0x40] = K(0,7),
	[0x41] = K(0,8), [0x42] = K(0,9), [0x43] = K(0,10), [0x44] = K(0,11), [0x45] = K(0,12),
	[0x47] = K(0,14), [0x48] = K(0,15), [0x49] = K(1,15), [0x4A] = K(2,0), [0x4B] = K(2,1),
	[0x4C] = K(3,4), [0x4D] = K(3,5), [0x4E] = K(3,6), [0x4F] = K(6,4), [0x50] = K(6,2),
	[0x51] = K(6,3), [0x52] = K(5,9), [0x53] = K(2,2), [0x54] = K(2,3), [0x55] = K(2,4),
	[0x56] = K(2,5), [0x57] = K(4,11), [0x58] = K(6,7), [0x59] = K(5,10), [0x5A] = K(5,11),
	[0x5B] = K(5,12), [0x5C] = K(4,8), [0x5D] = K(4,9), [0x5E] = K(4,10), [0x5F] = K(3,7),
	[0x60] = K(3,8), [0x61] = K(3,9), [0x62] = K(6,5), [0x63] = K(6,6),
};

static const struct { u32 mask; int row, col; } modifiers[] = {
	{0x01, 3, 11}, {0x02, 4, 12}, {0x04, 5, 14},
	{0x10, 6, 1}, {0x20, 5, 8}, {0x40, 6, 0},
};

static u8 key_down[0x64];
static u32 mod_down;

static void poll_keyboard(void)
{
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
	kbd_state_t *k = dev ? kbd_get_state(dev) : NULL;
	u32 mods, changed;
	int i;
	if (!k)
		return;
	for (i = 4; i < 0x64; i++) {
		int down = k->key_states[i].is_down != 0;
		if (down != key_down[i] && hid_map[i]) {
			key_down[i] = (u8)down;
			mach_key(hid_map[i] >> 4, hid_map[i] & 15, down);
		}
	}
	mods = k->last_modifiers.raw;
	changed = mods ^ mod_down;
	for (i = 0; i < 6; i++)
		if (changed & modifiers[i].mask)
			mach_key(modifiers[i].row, modifiers[i].col, !!(mods & modifiers[i].mask));
	mod_down = mods;
}

#ifdef HW_REPLAY
/* Scripted input for unattended hardware tests, generated from a .replay file */
typedef struct { u32 ms; char verb; int a, b; } replay_t;
#include "replay.h"
static u32 replay_pos;
static int replay_dx, replay_dy, replay_buttons;

static void replay_step(int *dx, int *dy, int *buttons)
{
	u32 now = (u32)(timer_us_gettime64() / 1000);
	while (replay_pos < sizeof(replay_events) / sizeof(replay_events[0]) &&
	       now >= replay_events[replay_pos].ms) {
		const replay_t *e = &replay_events[replay_pos++];
		if (e->verb == 'm') {
			replay_dx += e->a;
			replay_dy += e->b;
		} else if (e->verb == 'b') {
			replay_buttons = e->a;
		} else {
			printf("replay checkpoint %d sum=%08lX\n", e->a,
				(unsigned long)mach_screen_sum(&replay_screen));
		}
	}
	*dx = replay_dx > 16 ? 16 : replay_dx < -16 ? -16 : replay_dx;
	*dy = replay_dy > 16 ? 16 : replay_dy < -16 ? -16 : replay_dy;
	replay_dx -= *dx;
	replay_dy -= *dy;
	*buttons = replay_buttons;
}
#endif

void plat_poll_input(void)
{
	int dx = 0, dy = 0, buttons = 0;
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MOUSE);
	poll_keyboard();
	if (dev) {
		mouse_state_t *m = maple_dev_status(dev);
		if (m) {
			dx = m->dx;
			dy = m->dy;
			m->dx = m->dy = 0;
			buttons = ((m->buttons & MOUSE_LEFTBUTTON) ? 1 : 0) |
			          ((m->buttons & MOUSE_RIGHTBUTTON) ? 2 : 0) |
			          ((m->buttons & MOUSE_SIDEBUTTON) ? 4 : 0);
		}
	}
	dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
	if (dev) {
		cont_state_t *c = maple_dev_status(dev);
		if (c) {
			dx += c->joyx / 24;
			dy += c->joyy / 24;
			if (c->buttons & CONT_DPAD_LEFT) dx -= 4;
			if (c->buttons & CONT_DPAD_RIGHT) dx += 4;
			if (c->buttons & CONT_DPAD_UP) dy -= 4;
			if (c->buttons & CONT_DPAD_DOWN) dy += 4;
			if (c->buttons & CONT_A) buttons |= 1;
			if (c->buttons & CONT_B) buttons |= 2;
			if (c->buttons & CONT_X) buttons |= 4;
		}
	}
#ifdef HW_REPLAY
	replay_step(&dx, &dy, &buttons);
#endif
	mach_mouse(dx, dy, buttons);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	vid_set_mode(DM_640x480, PM_RGB565);
	vid_clear(0, 0, 0);
	mach_init(cmos_image);
	mach_run(0);
	return 0;
}
