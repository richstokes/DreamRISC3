/* SPDX-License-Identifier: MIT
 * Copyright (C) 2006 Mickaël Cardoso (SiZiOUS)
 * Copyright (C) 2026 Simulant and contributors
 *
 * Originally written for img4dc, which was released under the GPLv3, and
 * relicensed under MIT here by its author. This file is not covered by the CDDL
 * notices of the schilytools code it wraps -- see COPYRIGHT. */

#ifndef __LIBEDC_H__
#define __LIBEDC_H__

/* The one header the rest of mkdcdisc includes to reach this library, and the
 * only thing in here that is not upstream's.
 *
 * ecc.h is upstream's and declares everything through __PR(), which upstream
 * expects the including .c file to have picked up from <schily/mconfig.h>
 * beforehand -- edc_ecc.c does exactly that. Consumers outside this directory
 * come through here instead, so that they neither have to know about the schily
 * namespace nor carry an include path for it: these two are quoted, so they
 * resolve relative to this file wherever it is included from.
 *
 * What that hands the caller is do_encode_L2() and the MODE_* sector types.
 * Nothing is wrapped or renamed on the way: an earlier edc_encode_sector()
 * helper lived here, but it hard-coded MODE_2_FORM_1 and so could not serve the
 * GD-ROM writer, which encodes Mode 1. Sector assembly belongs to the callers
 * in src/disc_image/ instead. */
#include "schily/mconfig.h"
#include "schily/utypes.h"

#include "ecc.h"

#endif /* __LIBEDC_H__ */
