/* Machine state and helpers shared by recompiled ROM code and native drivers. */
#ifndef CPU_H
#define CPU_H
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;

#define PSR_I (1u << 27)
#define PSR_F (1u << 26)
#define MODE_USR 0
#define MODE_FIQ 1
#define MODE_IRQ 2
#define MODE_SVC 3
#define PC_MASK 0x3FFFFFCu

/* The first 16 words are reachable with SH-4 short displacements, so they hold
 * the registers and flags used most: r0-r7, r12-r14, flags, budget. */
typedef struct {
	union {
		u32 s[20];
		struct {
			u32 pad_low[11];
			u32 fn, fz, fc, fv; /* N is bit 31 of fn, Z means fz==0, C and V are 0 or 1 */
			s32 budget;
			u32 pad_high[4];
		};
	};
	u32 cur;            /* PC reported if a memory access aborts */
	u32 psr;            /* I, F and mode bits in their R15 positions */
	u8 **rt, **wt;      /* page tables for the current privilege */
	u32 usr[7], fiq[7], irq[2], svc[2]; /* banked r8-r14 */
} cpu_t;

#define SLOT(n) ((n) < 8 ? (n) : (n) < 12 ? (n) + 8 : (n) - 4)
#define R(n) (C->s[SLOT(n)])

/* Needs -ffixed-r14 everywhere: without it GCC still borrows r14 as a scratch
 * register inside functions, and helpers called meanwhile see the wrong C. */
#ifdef __sh__
register cpu_t *C asm("r14");
#define CPU_BASE_REG 1
#else
extern cpu_t cpu_state;
#define C (&cpu_state)
#endif
extern cpu_t cpu_state;

#define TAIL(call) __attribute__((musttail)) return call

#define F_N ((s32)C->fn < 0)
#define F_Z (C->fz == 0)
#define F_C (C->fc)
#define F_V (C->fv)

u32 R15_FULL(u32 pc);

typedef void (*block_fn)(u32);
typedef struct { u32 key; block_fn fn; } block_t;
extern const block_t block_table[], ram_table[];
extern const u32 block_table_mask, ram_table_mask;

/* Code overlays: one unit from disc occupies the overlay region at a time. Nothing
 * on the native stack refers to a block, so any dispatch can swap units. */
#define OV_MAGIC 0x4F564C59u
#ifdef __sh__
#define OV_SECTION __attribute__((section(".ovhdr")))
#else
#define OV_SECTION
#endif
#define OVERLAY_BYTES 0x110000u
typedef struct { u32 magic; const block_t *table; u32 mask; const u32 *guard; } ov_header_t;
typedef struct { u32 start, end, unit; } ov_range_t;
extern const ov_range_t ov_ranges[];
extern const u32 ov_range_count, ov_units;

/* Bundled apps. Each is an overlay unit, but its code runs at its load address in
 * RAM, which other tasks share, so entry is by address and instruction word. */
typedef struct { u32 start, end, entry, entry_word, unit, frame_us; } ov_app_t;
extern const ov_app_t ov_apps[];
extern const u32 ov_app_count, rom_ext_pages;

void dispatch(u32 pc);
void mach_poll(u32 pc);
void swi_insn(u32 pc);
void undef_insn(u32 pc);
void psr_write(u32 value);
u32 usr_get(int reg);
void usr_set(int reg, u32 value);

u32 ld32(u32 addr);
u32 ld8(u32 addr);
void st32(u32 addr, u32 value);
void st8(u32 addr, u32 value);
u32 ldm_regs(u32 addr, u32 mask);
void stm_regs(u32 addr, u32 mask);
u32 ld32T(u32 addr, u32 pc);
u32 ld8T(u32 addr, u32 pc);
void st32T(u32 addr, u32 value, u32 pc);
void st8T(u32 addr, u32 value, u32 pc);

#define PAGE_SHIFT 15
#define PAGE_OFFSET 0x7FFFu
#define PAGE_INDEX(a) (((a) >> PAGE_SHIFT) & 0x7FF)
#define LIKELY(x) __builtin_expect(!!(x), 1)

static inline u32 shift_r(int kind, u32 v, u32 s)
{
	if (!s)
		return v;
	switch (kind) {
	case 0: return s > 31 ? 0 : v << s;
	case 1: return s > 31 ? 0 : v >> s;
	case 2: return (u32)((s32)v >> (s > 31 ? 31 : s));
	}
	s &= 31;
	return s ? (v >> s) | (v << (32 - s)) : v;
}

static inline u32 shift_c(int kind, u32 v, u32 s, u32 *carry)
{
	if (!s)
		return v;
	switch (kind) {
	case 0: *carry = s > 32 ? 0 : (v >> (32 - s)) & 1; break;
	case 1: *carry = s > 32 ? 0 : (v >> (s - 1)) & 1; break;
	case 2: *carry = s > 31 ? v >> 31 : (v >> (s - 1)) & 1; break;
	default: *carry = (v >> ((s - 1) & 31)) & 1; break;
	}
	return shift_r(kind, v, s);
}

/* Native machine layer (mach.c) */
void mach_init(const u8 *cmos_image);
void mach_run(u32 start_pc);
void mach_stop(void);
void mach_key(int row, int col, int down);
void mach_mouse(int dx, int dy, int buttons);

/* Screen description handed to the platform on each frame */
typedef struct {
	int width, height, bpp;
	const u8 *ram;
	u32 ram_size, start, base, end; /* physical byte offsets; wraps end to base */
	const u32 *palette;              /* VIDC 13-bit entries; 16 logical + border */
	u32 cursor_palette[3];
	u32 cursor_addr;                 /* physical */
	int cursor_x, cursor_y, cursor_h;
} screen_t;

u32 mach_screen_sum(const screen_t *s);
u32 mach_polls(void);
u32 mach_rom_misses(void);
u32 mach_overlay_loads(void);

/* Floppy driver (fdc.c) */
void fdc_init(void);
void fdc_tick(uint64_t now);
void fdc_write(u32 addr, u32 value);
u32 fdc_read(u32 addr);
void fdc_latch_a(u32 value);
void fdc_latch_b(u32 value);
int fdc_ready(void);
u8 *fdc_image(void);
void mach_fiq(u32 bits);
void mach_fiq_clear(u32 bits);

/* Provided by each platform */
uint64_t plat_time_us(void);
void plat_rom_read(u32 page, u8 *dest); /* one 32KB ROM page; may block on disc */
const ov_header_t *plat_overlay_load(int unit); /* may block on disc */
u8 *plat_disc_load(u32 bytes); /* the ADFS image in RAM, or NULL for an empty drive */
void plat_frame(const screen_t *screen);
void plat_poll_input(void);
void plat_log(const char *fmt, ...);
void plat_fatal(const char *fmt, ...);
#endif
