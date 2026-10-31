/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* How long a GD-ROM's high-density data track is, and how much of it the
 * filesystem fills. Arithmetic only: the disc-format constants that decide it,
 * and the one decision that reads them.
 *
 * Split out of build_gd.cpp, and deliberately depending on nothing -- not the
 * options, not libisofs, not even <iostream> -- so that a unit test can reach
 * it directly. Covering it through the CLI instead would mean feeding mkdcdisc
 * a gigabyte to move the answer by one sector, which is both slow and too
 * blunt to land on the boundaries that matter. */

#include <cstddef>

#include "disc_image/disc_image.h"

namespace gd_track {

/* GD-ROM's fixed physical high-density-zone window: the filesystem starts at
 * CD_GD_START_LBA and the zone ends here. */
inline constexpr size_t GD_END_LBA = 549150;

/* Every data track on a pressed GD-ROM ends with 150 unused sectors: its last
 * file stops exactly that far before the track's end. Checked against 24 data
 * tracks across 18 commercial dumps (three different mastering tools) without
 * a single exception, and matched by GDIbuilder, which places the boot binary
 * to end at GD_END_LBA - 150.
 *
 * Those sectors are the track's run-out. A drive that treats them as such
 * cannot reliably read them back, so content placed there (1ST_READ.BIN above
 * all, which sorts last) is at risk on real media even though emulators, which
 * don't model the run-out, read it back happily. */
inline constexpr size_t TRACK_END_RESERVED_SECTORS = 150;

/* Red Book minimum track length (150-sector pregap + 150), enforced even on a
 * --gdi-truncate build: GD-ROM tooling (e.g. GD-ROM Explorer) rejects a
 * high-density track shorter than this. */
inline constexpr size_t MIN_HD_SECTORS = 300;

/* MIN_HD_SECTORS floors the high-density zone as a whole, which is the whole
 * story only while the zone is one track. A GD-DA disc splits it into three --
 * a data track, the audio, a closing data track -- and a drive sees the pieces,
 * not the total: no track on any of the 25 reference dumps is shorter than
 * MIN_HD_SECTORS, and GD-ROM Explorer refuses an image where one is.
 *
 * Each of the two data tracks ends with its own TRACK_END_RESERVED_SECTORS of
 * run-out, so clearing the minimum means this much filesystem on each side of
 * the spacer. build_gd() reserves it with a filler at either end. */
inline constexpr size_t MIN_SPLIT_SIDE_SECTORS = MIN_HD_SECTORS - TRACK_END_RESERVED_SECTORS;

/* What those two fillers cost the zone, which is nothing at all without GD-DA. */
inline constexpr size_t split_reserve(size_t spacer_sectors) {
    return spacer_sectors > 0 ? 2 * MIN_SPLIT_SIDE_SECTORS : 0;
}

/* How many GD-DA tracks the high-density area takes. Sega's rule for a GD (the
 * "Rules governing the content and format of a GD" section of the GD Workshop
 * help shipped with the Katana SDK) allows up to 95 audio tracks, followed by
 * another Mode 1 track.
 *
 * The structure says the same number twice over: the area is tracks 3 to 99,
 * 97 of them, and the audio sits between an opening data track and the closing
 * one, leaving 95; and the TOC written into the system area has room for
 * exactly 97 entries (see write_gdi_hda_toc()).
 *
 * Past the limit that TOC is what breaks, silently: its entry array stops being
 * filled while the `last` field it publishes keeps counting, so the image
 * announces tracks that have no entry. */
inline constexpr size_t MAX_GDDA_TRACKS = 95;

/* A full-size high-density track, and the part of it a filesystem may use. */
inline constexpr size_t FULL_TRACK_SECTORS = GD_END_LBA - CD_GD_START_LBA;
inline constexpr size_t MAX_CONTENT_SECTORS = FULL_TRACK_SECTORS - TRACK_END_RESERVED_SECTORS;

/* What plan_track() concluded. When `fits` is false the two sizes are
 * meaningless and the caller reports the overflow instead; that only ever
 * happens on a padded build, since a truncated one is sized to whatever it is
 * given. */
struct Plan {
    bool fits;
    size_t track_sectors;          /* physical length of the track */
    size_t content_target_sectors; /* how much of it the filesystem fills */
};

/* Sizes the high-density track around `content_sectors` of filesystem plus
 * `spacer_sectors` reserved for the GD-DA tracks (0 when there are none).
 *
 * A padded build always produces the full physical track, so the filesystem
 * is grown to fill it and the only question is whether it fits. A truncated
 * build (--gdi-truncate) instead shrinks the track to the content, floored at
 * the minimum track length, and always fits: the high-density area then ends
 * far short of GD_END_LBA, describing a disc no pressed GD-ROM could be.
 *
 * Whether that boots is up to whatever reads the .gdi, which is why the flag
 * is documented as non-standard rather than as a size/compatibility trade with
 * a known answer. An ODE that takes its TOC from the .gdi accepts it; nullDC
 * and Demul both drop straight back to the BIOS menu on a truncated image and
 * boot the same disc built without the flag. So a truncated build is not the
 * one to reach for when an emulator is what will read it. Either way the
 * track's last TRACK_END_RESERVED_SECTORS stay clear of the filesystem.
 *
 * What nullDC objects to is not the size: it refuses a track of 135000 sectors
 * (30 min) and one of 400000 (89 min, the size of a commercial GD-ROM) exactly
 * as it refuses one of 300, and boots only the full 504150. The criterion is
 * that the high-density area reaches GD_END_LBA, so no floor short of the full
 * size buys anything.
 *
 * Worth knowing because Sega's rules require the area to hold at least 30
 * minutes, which reads like a floor worth offering as an option. It is not:
 * that minimum is a pressing constraint, and a build floored there boots no
 * better than a 4-second one. */
inline Plan plan_track(size_t content_sectors, size_t spacer_sectors, bool truncate) {
    /* The filesystem spans the audio too, via the spacer, so both budgets
     * account for it -- and, where there is a spacer, for the fillers that keep
     * the two data tracks either side of it above the minimum length. */
    const size_t occupied_sectors =
        content_sectors + spacer_sectors + split_reserve(spacer_sectors);

    if(!truncate) {
        if(occupied_sectors > MAX_CONTENT_SECTORS) {
            return {false, 0, 0};
        }

        return {true, FULL_TRACK_SECTORS, MAX_CONTENT_SECTORS};
    }

    /* Just enough to hold the content plus its reserved run-out, but never
     * below the minimum track length. */
    const size_t wanted = occupied_sectors + TRACK_END_RESERVED_SECTORS;
    const size_t track_sectors = wanted > MIN_HD_SECTORS ? wanted : MIN_HD_SECTORS;

    return {true, track_sectors, track_sectors - TRACK_END_RESERVED_SECTORS};
}

}  /* namespace gd_track */
