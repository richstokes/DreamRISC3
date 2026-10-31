/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_H
#define _WINPATCH_H

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <sys/types.h>
#include <errno.h>

#include "winpatch/_patch.h"

#include "winpatch/types.h"
#include "winpatch/pwd.h"
#include "winpatch/grp.h"
#include "winpatch/langinfo.h"
#include "winpatch/fnmatch.h"
#include "winpatch/lstat.h"
#include "winpatch/unistd.h"

#define _POSIX_C_SOURCE 200809L
#define Libburnia_timezonE timezone

#define HAVE_ZLIB 1
#define HAVE_ICONV 1
#define HAVE_LIBACL 0
#define HAVE_XATTR 0
#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_UNISTD_H 1

/* Workaround for Windows inode bug (see `fs_local.c` for details)
 * '0' acts as an upstream sentinel value: `iso_file_source_stream_new()`
 * checks `if (fs_id == 0)` to fall back to its internal counter. */
#define WINPATCH_ISO_LOCAL_FS_NO_STAT_ID 0

#endif /* _WINPATCH_H */
