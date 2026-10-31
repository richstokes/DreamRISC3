/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Assembling finished bytes into a disc: which sessions and tracks a layout
 * needs, how long each one is, and writing the result out in the format the
 * user asked for. Nothing here knows how the ISO was produced. */

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "disc_image/disc_image.h"

/* Red Book nominal capacities, in 75-sectors-per-second terms: 74 minutes is
 * the size data track padding fills out to, and 80 minutes the largest blank
 * that can be assumed to exist. Longer CD-R does exist (90- and 99-minute),
 * which is why passing 80 is warned about rather than refused. */
static const size_t CD_74MIN_SECTORS = 333000;
static const size_t CD_80MIN_SECTORS = 360000;

/* The first (audio) session: the CDDA tracks given via -c, or a blank filler
 * when none are. `log` off silences it for the MSINFO-only computation. */
bool add_audio_session(cd_session_t* session0, bool log = true);

/* The data session(s) holding `iso_data`: a single Audio/Data data session,
 * or (data_data) a Data/Data pair; the full ISO followed by a duplicated
 * "boot head" session. */
void add_data_track_sessions(cd_image_t* img, const std::vector<uint8_t>& iso_data, bool data_data);

/* The two full-ISO sessions of -L/--disc-layout merged-data. */
void add_merged_data_sessions(cd_image_t* img,
                            const std::vector<uint8_t>& iso1_data,
                            const std::vector<uint8_t>& iso2_data);

/* Mirrors cd_session_length_in_sectors() for a session holding one data
 * track, without allocating the track data. Used to print the session 2
 * MSINFO from the already-built session 1 ISO's size alone. */
size_t data_session_length_in_sectors(uintmax_t data_size_bytes, bool is_first_session);

/* How much filler the data track needs to reach the target disc size. */
size_t estimate_padding_sectors(const cd_image_t* image_so_far,
                                const std::filesystem::path& data_dir);

/* Writes `img` out per -F/--format (or the output extension), then frees it.
 * What it names in the closing "Wrote ..." is output_files.hpp's answer, the
 * same one the build plan promised. */
bool write_disc_image(cd_image_t* img, const std::string& output_path);

/* Reads a pre-built ISO supplied via -t/--external-data-track. */
bool read_iso_file(const std::filesystem::path& path, std::vector<uint8_t>& out_data);
