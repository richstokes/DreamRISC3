/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Building a CD-shaped Dreamcast disc.
 *
 * "CD" rather than "CDI": the output format is whatever -F/--format asks for
 * (DiscJuggler, Alcohol or Nero) and the 3 entry points below differ
 * by where the filesystem comes from, not by what they write. Nor is this
 * only MIL-CD: that is the default Audio/Data layout, while -L/--disc-layout's
 * other two values produce Data/Data discs, which exist precisely because later
 * Dreamcast revisions dropped MIL-CD support. */

#include <filesystem>

/* Standard mode: mkdcdisc builds the ISO9660 filesystem and the IP.BIN
 * itself, from the contents staged in `input_dir`. */
bool build_cd(const std::filesystem::path& input_dir);

/* -t/--external-data-track: wrap an ISO already built by mkisofs, IP.BIN
 * embedded via its -G switch. mkdcdisc touches neither the filesystem nor the
 * IP.BIN here, it only builds the disc structure around them. */
bool build_cd_from_external_iso(const std::filesystem::path& iso_path);

/* -L/--disc-layout merged-data: two pre-built ISOs as two full data sessions.
 * Session 2 must already merge session 1's files with its own (mkisofs -M)
 * and carry its own IP.BIN. */
bool build_cd_from_merged_data_isos(const std::filesystem::path& iso1_path,
                                    const std::filesystem::path& iso2_path);
