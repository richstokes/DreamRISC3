/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_PATCH_H
#define _WINPATCH_PATCH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Convert backslashes (`\`) to forward slashes (`/`) in-place
// Windows supports both but Unix-style is preferred for cross-platform compatibility
static inline
void convert_path_to_unix(char *path) {
    if (path == NULL)
        return;
    
    char *p = path;
    while (*p) {
        if (*p == '\\') {
            *p = '/';
        }
        p++;
    }
}

#endif /* _WINPATCH_PATCH_H */
