/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_LSTAT_H
#define _WINPATCH_LSTAT_H

#include <sys/stat.h>
#include <windows.h>
#include <errno.h>

#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#endif

#ifndef S_IFLNK
#define S_IFLNK 0120000  // Symbolic link
#endif

#ifndef S_IFSOCK
#define S_IFSOCK 0140000  // Socket
#endif

#ifndef S_ISSOCK
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#endif

#define lstat(path, buf) stat(path, buf)

#endif /* _WINPATCH_LSTAT_H */
