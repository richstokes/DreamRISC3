/* Helpers for bundled app units. These are small enough to spend code size on
 * speed that the ROM cannot afford: memory access and returns happen inline. */
#include "cpu.h"

#pragma GCC optimize("O2")

#define ALWAYS_INLINE static inline __attribute__((always_inline))

ALWAYS_INLINE u32 ld32i(u32 a)
{
	u8 *page = C->rt[PAGE_INDEX(a)];
	if (LIKELY(page && !(a & 0xFC000003u)))
		return *(u32 *)(page + (a & PAGE_OFFSET));
	return ld32(a);
}

ALWAYS_INLINE u32 ld8i(u32 a)
{
	u8 *page = C->rt[PAGE_INDEX(a)];
	if (LIKELY(page && !(a & 0xFC000000u)))
		return page[a & PAGE_OFFSET];
	return ld8(a);
}

ALWAYS_INLINE void st32i(u32 a, u32 v)
{
	u8 *page = C->wt[PAGE_INDEX(a)];
	if (LIKELY(page && !(a & 0xFC000003u)))
		*(u32 *)(page + (a & PAGE_OFFSET)) = v;
	else
		st32(a, v);
}

ALWAYS_INLINE void st8i(u32 a, u32 v)
{
	u8 *page = C->wt[PAGE_INDEX(a)];
	if (LIKELY(page && !(a & 0xFC000000u)))
		page[a & PAGE_OFFSET] = (u8)v;
	else
		st8(a, v);
}

/* The page holding a whole LDM or STM, or null for the slow helper */
ALWAYS_INLINE u32 *block_at(u8 **table, u32 a, u32 bytes)
{
	u8 *page = table[PAGE_INDEX(a)];
	if (LIKELY(page && !(a & 0xFC000003u) && (a & PAGE_OFFSET) + bytes <= PAGE_OFFSET + 1))
		return (u32 *)(page + (a & PAGE_OFFSET));
	return 0;
}
