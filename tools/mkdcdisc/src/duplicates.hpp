/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* --duplicates-once: Writes byte-identical files into the data track once.
 * Matches mkisofs' -duplicates-once behavior by matching on CONTENT, not inodes.
 *
 * Design: libisofs shares an extent when two IsoFile nodes share the same
 * IsoStream*, so the sharing is arranged by handing out one stream rather
 * than by asking the OS about identity. That holds on every platform for two
 * independent reasons: iso_stream_cmp_ino() short-circuits on pointer
 * equality before it ever asks for a (fs_id, dev_id, ino_id) triple, and the
 * triple is stored per stream OBJECT anyway, so two nodes on one stream read
 * the same values out of it either way. It therefore sidesteps the Windows
 * CRT's missing st_ino rather than depending on it -- an approach built on
 * device/inode numbers would work on Linux and quietly do nothing there.
 *
 * Verified in both identity regimes: the vendored 1.5.9 under MinGW-w64,
 * which reports no identity, and the system 1.5.6 under MSYS2, which reports
 * real dev/ino. Both share the extent, and both keep two same-size files of
 * differing content apart.
 *
 * Safety: Content equality is checked via MD5 followed by a strict
 * byte-by-byte comparison. The digest keeps the cost linear; the direct
 * comparison guarantees hash collisions cannot corrupt the disc. */

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#define LIBISOFS_WITHOUT_LIBBURN yes
#include <libisofs.h>

struct DuplicateStats {
    /* Number of identical file sets. */
    size_t groups = 0;

    /* Number of duplicate files collapsed into shared extents. */
    size_t aliases = 0;

    /* Total bytes saved (before sector alignment rounding). */
    uint64_t bytes_saved = 0;

    /* Warnings for files whose -S/--sort-file weight was overruled by a
     * heavier duplicate in the same set. Logged by the caller to prevent
     * duplicate warnings during multi-pass layouts (e.g., GD-ROM). */
    std::vector<std::string> weight_conflicts;
};

/* Collapses every set of byte-identical regular files under `root` onto a
 * single extent, skipping any node in `excluded`.
 *
 * Must be called AFTER tree population and -S/--sort-file application,
 * but BEFORE iso_image_create_burn_source() (libisofs must not be laying
 * the image out while alias nodes are being rebuilt).
 *
 * `excluded`: A set of node pointers rather than names, because the names are
 * not all knowable here -- -p/--bootstrap can call the boot binary something
 * other than 1ST_READ.BIN, so matching by name would miss it.
 *
 * `weights`: Shared extents inherit the highest sort weight among their
 * duplicates, the rule mkisofs settled on (reassign_link_addresses() and the
 * MAX() beside it, in its write.c). Subordinated weights are recorded in
 * `weight_conflicts` for logging. */
DuplicateStats collapse_duplicate_files(IsoImage* image, IsoDir* root,
                                        const std::set<IsoNode*>& excluded,
                                        const std::map<IsoNode*, int>& weights);

/* CLI wrapper: applies deduplication if --duplicates-once is set and logs
 * the space saved.
 *
 * `area`: Names the filesystem (e.g., "high-density area"). Used for logging
 * and to prevent printing identical stats twice during multi-pass layouts. */
void apply_duplicates_once(const char* area, IsoImage* image, IsoDir* root,
                           const std::set<IsoNode*>& excluded,
                           const std::map<IsoNode*, int>& weights);
