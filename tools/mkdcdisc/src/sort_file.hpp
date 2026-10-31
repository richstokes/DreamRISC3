/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* mkisofs-style sort files: a list of paths and weights that decide where
 * each file lands on the disc, which is what makes loading order tunable.
 *
 * libisofs ranks HIGH weights towards LOW block addresses -- "the greater
 * this value is, the closer from the beginning of image the file will be
 * written" -- so a large weight pulls a file towards the inner edge, not the
 * outer one. */

#include <map>

#define LIBISOFS_WITHOUT_LIBBURN yes
#include <libisofs.h>

/* Reads the sort file named by `option` (nothing to do when that option was
 * not given) and applies its weights to `root` and everything under it,
 * matching on the path each node will have on the disc; a "/" entry weights
 * the root itself.
 *
 * The weights are read fresh on every call and never outlive it, so each disc
 * area passes its own option and the two cannot bleed into one another -- the
 * high-density filesystem is also built twice per run, which shared state
 * would make order-dependent.
 *
 * Returns the weight applied to each node it reached. --duplicates-once needs
 * the values, not just the names: deduplicated files share one extent and so
 * one position, and the rule mkisofs settled on is that the shared extent
 * takes the highest weight of the set. Working that out means being able to
 * read the weights back, and libisofs has no getter for them. */
std::map<IsoNode*, int> apply_sort_file(const char* option, IsoDir* root);
