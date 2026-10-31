/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_GRP_H
#define _WINPATCH_GRP_H

#include <stddef.h>
#include <sys/types.h>
#include "types.h"

struct group {
    char *gr_name;      // group name
    char *gr_passwd;    // group password (unused)
    gid_t gr_gid;       // group ID
    char **gr_mem;      // group members
};

static struct group dummy_grp = {
    "users",        // gr_name
    "",             // gr_passwd
    1000,           // gr_gid
    NULL            // gr_mem
};

static inline
struct group* getgrnam(const char *name) {
    (void)name;
    return &dummy_grp;
}

static inline
struct group* getgrgid(gid_t gid) {
    (void)gid;
    return &dummy_grp;
}

#endif /* _WINPATCH_GRP_H */
