/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Decoding an audio file into the raw 2352-byte CD-DA sectors a disc track
 * carries. Assembling those tracks into a session is disc_layout's job. */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/* One CD-DA sector: 588 stereo 16-bit frames, 1/75 s of audio. The writers
 * spell the same number RAW_SECTOR_SIZE (src/disc_image/raw_sector.h), but
 * that header declares C functions with no extern "C" guard, so the C++ side
 * keeps its own name for the constant rather than including it. */
inline constexpr size_t CDDA_SECTOR_SIZE = 2352;

/* 44.1 kHz 16-bit stereo PCM, stripped of any WAV header. Returns nullopt and
 * reports why when the file cannot be used. */
std::optional<std::vector<uint8_t>> wav_to_cdda(const std::string& filename);
std::optional<std::vector<uint8_t>> raw_to_cdda(const std::string& filename);

/* Either of the above, picked by looking at the file itself rather than its
 * extension. */
std::optional<std::vector<uint8_t>> load_audio_track(const std::string& filename);
