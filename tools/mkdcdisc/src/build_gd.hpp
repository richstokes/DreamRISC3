/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Building a GD-ROM: a .gdi descriptor plus one file per track, laid out the
 * way a pressed Dreamcast disc is.
 *
 * The disc has two physically distinct zones. The low-density area holds
 * a boot image and its own small ISO9660 filesystem; the high-density area,
 * pinned at LBA 45000, holds the real filesystem and, when GD-DA is present,
 * splits into two data tracks with the audio between them. */

#include <filesystem>

bool build_gd(const std::filesystem::path& input_dir);
