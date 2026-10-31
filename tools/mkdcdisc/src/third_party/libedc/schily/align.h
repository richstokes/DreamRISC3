/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Stand-in for schilytools' <schily/align.h> -- see mconfig.h in this
 * directory for why these exist at all. */

#ifndef MKDCDISC_SCHILY_ALIGN_H
#define MKDCDISC_SCHILY_ALIGN_H

#include <stdint.h>

/* True when `a` is aligned for a type whose size minus one is the mask `s`.
 *
 * edc_ecc.c calls it once, as xaligned(inout + 12, sizeof (UInt32_t) - 1), to
 * choose between scrambling the sector a byte at a time and doing it 32 bits at
 * a time. Both loops compute the same bytes, so this only has to be right, not
 * quick -- but it does have to be right in the true-when-aligned direction, or
 * the wide loop would run on a misaligned pointer. */
#define xaligned(a, s)	((((uintptr_t)(a)) & (s)) == 0)

#endif /* MKDCDISC_SCHILY_ALIGN_H */
