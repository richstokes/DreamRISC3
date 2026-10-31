/* IP creator (makeip)
 *
 * Copyright (C) 2000, 2001, 2002, 2019, 2020 The KOS Team and contributors.
 * All rights reserved.
 *
 * This code was contributed to KallistiOS (KOS) by Andress Antonio Barajas
 * (BBHoodsta). It was originally made by Marcus Comstedt (zeldin). Some
 * portions of code were made by Andrew Kieschnick (ADK/Napalm). Heavily
 * updated by SiZiOUS. Bootstrap replacement (IP.TMPL) was made by Jacob
 * Alberty (LiENUS).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

/* PNG-to-MR conversion, taken from makeip's mr.c (png_read(),
 * mr_convert_raw() and mr_compress()). MR is Sega's palettized,
 * run-length-compressed image format, the one the license screen displays.
 *
 * This file is compiled only in a build with libpng; src/mr_image.cpp is what
 * the rest of mkdcdisc talks to, and it is there that a failure below is
 * turned into something a user reads. See README.md. */

#include <cstdint>
#include <string>
#include <vector>

#ifdef MKDCDISC_WITH_PNG

namespace makeip {

/* An MR palette is 128 entries, and there is no deeper format to fall back
 * on: an image with more distinct colors cannot be represented at all. */
constexpr int MR_MAX_PALETTE_COLORS = 128;

enum class MrStatus {
    Ok,
    ReadFailed,    /* The file could not be opened, or libpng refused it. */
    TooManyColors, /* More than MR_MAX_PALETTE_COLORS distinct RGB triplets. */
};

/* Converts a PNG -- any color type and bit depth libpng can decode -- into MR,
 * appending nothing and leaving `out` untouched unless MrStatus::Ok is
 * returned. */
MrStatus png_to_mr(const std::string& path, std::vector<uint8_t>& out);

} // namespace makeip

#endif /* MKDCDISC_WITH_PNG */
