/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

#include "disc_image.h"

struct _cd_track_t {
    cd_session_t* session;

    track_type_t type;
    track_mode_t mode;

    uint8_t* data;
    size_t data_len;

    /* False when the bytes belong to the caller (cd_new_track_borrowed), in
     * which case destroy_track() leaves them alone. Nothing in here writes
     * through `data`, so pointing at someone else's buffer is safe; only the
     * freeing had to learn the difference. */
    bool owns_data;

    size_t pregap_sectors;
    size_t postgap_sectors;

    /* Caller-imposed start LBA, or 0 for "derive it". Distinct from
     * cd_track_start_lba(), which computes a position from the session
     * layout; see the header for why GD-ROM needs an override. */
    uint32_t placement_lba;

    cd_track_t* next_track;
};

struct _cd_session_t {
    cd_image_t* image;
    cd_track_t* first_track;
    cd_session_t* next_session;
};

struct _cd_image_t {
    char volume_name[255];
    cd_session_t* first_session;

    /* Borrowed, may be NULL; see cd_image_set_progress(). */
    const progress_t* progress;
};

/* A write in flight, against the sector count the writer knows it is going to
 * store. Every format stores a different set of sectors -- CDI keeps the
 * pregaps, MDF only declares them, a .gdi track file has neither -- so each
 * writer counts its own total and then just advances it as it goes, rather
 * than sharing one notion of "the size of the image". */
typedef struct {
    const progress_t* progress;
    uint64_t written;
    uint64_t total;
} cd_write_progress_t;

static inline void cd_write_progress_init(cd_write_progress_t* p,
                                          const cd_image_t* image,
                                          uint64_t total_sectors) {
    p->progress = cd_image_progress(image);
    p->written = 0;
    p->total = total_sectors;
}

/* Non-zero when the write has been asked to stop (a Ctrl+C, typically): the
 * writers pass that up as a plain failure, leaving the output unfinished
 * rather than pretending to have written an image. */
static inline int cd_write_progress_add(cd_write_progress_t* p, uint64_t sectors) {
    p->written += sectors;
    return progress_report(p->progress, p->written, p->total);
}

/* Zero filler, written straight to the stream at its current position.
 *
 * Every format needs both of these -- the tail padding was spelled out in all
 * four writers and the silent run in three of them -- so they are spelled out
 * here instead. Neither reports errors: the writers around them check ferror()
 * once, when the file is finished, rather than after each call.
 *
 * Sectors counted towards progress stay the caller's business, since each
 * writer stores a different set of them (see cd_write_progress_t above). */

/* Fills the current sector out to its boundary when data_len bytes stopped
 * partway into one, and does nothing when they did not. A raw CD-DA file is
 * any number of bytes long; a track on a disc is a whole number of sectors,
 * so the tail is completed with silence.
 *
 * sector_size is the caller's stored sector size -- 2352 for CD-DA, 2048 for
 * a cooked .gdi data track -- and must not exceed a full raw sector. */
void cd_write_sector_tail_padding(FILE* f, size_t data_len, size_t sector_size);

/* `count` silent CD-DA sectors: pregaps, postgaps, and the minimum length a
 * CDI pads a short audio track up to. */
void cd_write_silent_sectors(FILE* f, size_t count);
