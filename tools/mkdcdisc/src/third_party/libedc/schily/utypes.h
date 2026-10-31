/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Stand-in for schilytools' <schily/utypes.h> -- see mconfig.h in this
 * directory for why these exist at all.
 *
 * Upstream declares a full set of fixed-width and convenience types; edc_ecc.c
 * uses these three. */

#ifndef MKDCDISC_SCHILY_UTYPES_H
#define MKDCDISC_SCHILY_UTYPES_H

#include <stdint.h>

typedef unsigned char	Uchar;
typedef uint32_t	UInt32_t;

/* UConst is the third name edc_ecc.c needs, but it is defined in mconfig.h
 * instead -- see the comment there. */

#endif /* MKDCDISC_SCHILY_UTYPES_H */
