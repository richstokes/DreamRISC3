/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Progress reporting: how a long-running step tells whoever asked for it how
 * far along it is.
 *
 * Deliberately a plain C type living outside both halves of the program: the
 * work that takes the time is split between the C++ side (draining libisofs
 * into an ISO) and the C disc image writers (framing that ISO into sectors),
 * and neither should have to know that the other end of the callback is a
 * terminal, a GUI, or nothing at all. src/progress.hpp holds the one
 * implementation mkdcdisc itself installs.
 *
 * `done` and `total` are counts in whatever unit the reporting step finds
 * natural -- bytes for the ISO build, sectors for the image writers. Only
 * their ratio is meaningful to the receiver, so a step may not change units
 * mid-run. A `total` of 0 means the size is not known ahead of time. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 for "carry on", non-zero for "stop where you are".
 *
 * The reply is what lets a long step be interrupted without every loop in
 * the program having to know about signals, and without the disc image
 * writers -- a library, with no business reading the program's global state
 * -- having to know about them at all. A step that is told to stop leaves its
 * output unfinished and reports failure; it never returns a truncated result
 * as if it had succeeded. */
typedef int (*progress_fn)(void* user_data, uint64_t done, uint64_t total);

typedef struct {
    progress_fn report;
    void* user_data;
} progress_t;

/* Reports one step's advance, and answers whether it should carry on. A NULL
 * progress_t, and one carrying no callback, both mean "nobody is watching",
 * so callers can report unconditionally rather than guarding every call site;
 * nobody watching also means nobody objecting, hence 0. */
static inline int progress_report(const progress_t* progress,
                                  uint64_t done, uint64_t total) {
    if(progress && progress->report) {
        return progress->report(progress->user_data, done, total);
    }

    return 0;
}

#ifdef __cplusplus
}
#endif
