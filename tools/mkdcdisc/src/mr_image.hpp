/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* The MR boot-logo image (Dreamcast license-screen graphic embedded in
 * IP.BIN): format detection, and PNG-to-MR conversion.
 *
 * The conversion itself is makeip's and lives in
 * src/third_party/makeip/mr.cpp, under makeip's own license; what is here is
 * the mkdcdisc side of it, which is what reports a failure to the user.
 *
 * PNG support pulls in libpng, so it is compiled in only when
 * MKDCDISC_WITH_PNG is defined (meson.build sets it when libpng is found).
 * Without it, mkdcdisc still accepts already-converted .mr files; it just
 * can't convert a PNG itself. */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class ImageFormat {
    Unknown, /* Neither of the signatures below -- unreadable or unsupported. */
    Mr,      /* Starts with the "MR" magic: a ready-to-embed MR file. */
    Png,     /* PNG signature; needs convert_png_to_mr() before it can be embedded. */
};

/* Sniffs the file's first bytes; does not validate the rest of the file. */
ImageFormat detect_image_format(const std::string& path);

#ifdef MKDCDISC_WITH_PNG

/* Converts a PNG (any color type/bit depth libpng can decode) into Sega's MR
 * format: a palettized, run-length-compressed image. Returns nullopt on I/O
 * or decode failure, or if the image uses more than 128 distinct colors --
 * MR's palette has no room for more. Reports what went wrong on stderr. */
std::optional<std::vector<uint8_t>> convert_png_to_mr(const std::string& path);

#endif
