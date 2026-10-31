/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* MR boot-logo image handling: see mr_image.hpp. */

#include "mr_image.hpp"
#include "terminal.hpp"

#include <cstring>
#include <fstream>
#include <iostream>

#ifdef MKDCDISC_WITH_PNG
#include "third_party/makeip/mr.hpp"
#endif

ImageFormat detect_image_format(const std::string& path) {
    unsigned char data[4] = {0};

    std::ifstream f(path, std::ios::binary);
    if(!f || !f.read((char*) data, sizeof(data))) {
        return ImageFormat::Unknown;
    }

    if(std::memcmp(data, "MR", 2) == 0) {
        return ImageFormat::Mr;
    }
    if(std::memcmp(data + 1, "PNG", 3) == 0) {
        return ImageFormat::Png;
    }

    return ImageFormat::Unknown;
}

#ifdef MKDCDISC_WITH_PNG

/* The conversion itself is makeip's, and says nothing on its own; what a
 * failure reads like is decided here, in mkdcdisc's own voice, so that the
 * ported code stays a plain function of its input. */
std::optional<std::vector<uint8_t>> convert_png_to_mr(const std::string& path) {
    std::vector<uint8_t> mr;

    switch(makeip::png_to_mr(path, mr)) {
        case makeip::MrStatus::Ok:
            return mr;

        case makeip::MrStatus::ReadFailed:
            std::cerr << term::error << "unable to read PNG file: " << path << std::endl;
            return std::nullopt;

        case makeip::MrStatus::TooManyColors:
            std::cerr << term::error << path << " uses more than " << makeip::MR_MAX_PALETTE_COLORS
                      << " colors; MR license images must be palettized to at most "
                      << makeip::MR_MAX_PALETTE_COLORS << " colors." << std::endl;
            return std::nullopt;
    }

    return std::nullopt;
}

#endif /* MKDCDISC_WITH_PNG */
