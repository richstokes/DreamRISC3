/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#ifndef _WINPATCH_PWD_H
#define _WINPATCH_PWD_H

#include <sys/types.h>
#include "types.h"

// Dummy structures for Windows
struct passwd {
    char *pw_name;      // username
    char *pw_passwd;    // password (unused)
    uid_t pw_uid;       // user ID
    gid_t pw_gid;       // group ID
    char *pw_gecos;     // full name
    char *pw_dir;       // home directory
    char *pw_shell;     // shell
};

// Static variables for dummy data
static struct passwd dummy_pwd = {
    "user",         // pw_name
    "",             // pw_passwd
    1000,           // pw_uid
    1000,           // pw_gid
    "Default User", // pw_gecos
    "/home/user",   // pw_dir
    "/bin/sh"       // pw_shell
};

// Dummy functions
static inline
struct passwd* getpwnam(const char *name) {
    (void)name; // avoid unused parameter warning
    return &dummy_pwd;
}

static inline
struct passwd* getpwuid(uid_t uid) {
    (void)uid;
    return &dummy_pwd;
}

#endif /* _WINPATCH_PWD_H */
