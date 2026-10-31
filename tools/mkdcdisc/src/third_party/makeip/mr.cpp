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

/* PNG-to-MR conversion: see mr.hpp. */

#include "mr.hpp"

#ifdef MKDCDISC_WITH_PNG

#include <cstdio>
#include <cstdlib>

#include <png.h>

namespace makeip {

namespace {

struct Color {
    uint8_t r, g, b;
};

/* Run-length compression, from mr_compress(): runs of 2-127 identical bytes
 * cost 2 bytes (0x80|run, value); 128-255 cost 3 (0x81, run, value); 256-382
 * cost 3 (0x82, 0x80|(run-0x100), value); anything else is emitted
 * literally. */
std::vector<uint8_t> mr_compress(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());

    size_t position = 0;
    while(position < in.size()) {
        size_t run = 1;
        while(position + run < in.size() && in[position] == in[position + run] && run < 0x17f) {
            ++run;
        }

        if(run > 0xff) {
            out.push_back(0x82);
            out.push_back((uint8_t) (0x80 | (run - 0x100)));
            out.push_back(in[position]);
        } else if(run > 0x7f) {
            out.push_back(0x81);
            out.push_back((uint8_t) run);
            out.push_back(in[position]);
        } else if(run > 1) {
            out.push_back((uint8_t) (0x80 | run));
            out.push_back(in[position]);
        } else {
            out.push_back(in[position]);
        }

        position += run;
    }

    return out;
}

/* Reads the whole PNG into a flat RGBA buffer, from png_read(). Everything
 * from png_create_read_struct() past the setjmp() has to stick to plain
 * malloc'd/pointer state -- libpng's error handling longjmp()s back here,
 * and only trivial-destructor locals may be live across that jump. */
bool read_png_rgba(const std::string& path, std::vector<uint8_t>& rgba,
                   uint32_t& width, uint32_t& height) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if(!fp) {
        return false;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!png_ptr) {
        std::fclose(fp);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        std::fclose(fp);
        return false;
    }

    unsigned char* raw = NULL;
    png_bytep* row_pointers = NULL;

    if(setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        std::fclose(fp);
        std::free(raw);
        std::free(row_pointers);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    int bit_depth, color_type, interlace_type;
    png_uint_32 png_width, png_height;
    png_get_IHDR(png_ptr, info_ptr, &png_width, &png_height, &bit_depth, &color_type,
        &interlace_type, NULL, NULL);

    raw = (unsigned char*) std::malloc((size_t) png_width * png_height * 4);

    /* Strip to 8-bit/channel, unpack sub-byte pixels, and expand palette/gray/
     * transparency down to RGB(A), so that every PNG ends up as flat 4-byte
     * RGBA regardless of source format. */
    png_set_strip_16(png_ptr);
    png_set_packing(png_ptr);

    if(color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_expand(png_ptr);
    }
    if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand(png_ptr);
    }
    if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_expand(png_ptr);
    }

    png_color_16* image_background;
    if(png_get_bKGD(png_ptr, info_ptr, &image_background)) {
        png_set_background(png_ptr, image_background, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
    }

    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png_ptr, info_ptr);

    row_pointers = (png_bytep*) std::malloc(sizeof(png_bytep) * png_height);
    for(png_uint_32 row = 0; row < png_height; ++row) {
        row_pointers[row] = raw + (size_t) png_width * 4 * row;
    }

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, info_ptr);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    std::fclose(fp);
    std::free(row_pointers);

    width = png_width;
    height = png_height;
    rgba.assign(raw, raw + (size_t) width * height * 4);
    std::free(raw);

    return true;
}

} // namespace

/* From mr_convert_raw(): builds the palette by scanning the pixels in order,
 * indexes the image against it, compresses that, and writes the result out. */
MrStatus png_to_mr(const std::string& path, std::vector<uint8_t>& out) {
    std::vector<uint8_t> rgba;
    uint32_t width = 0, height = 0;

    if(!read_png_rgba(path, rgba, width, height)) {
        return MrStatus::ReadFailed;
    }

    Color palette[MR_MAX_PALETTE_COLORS];
    int palette_count = 0;

    std::vector<uint8_t> indexed((size_t) width * height);

    for(size_t i = 0; i < indexed.size(); ++i) {
        const uint8_t* pixel = &rgba[i * 4];
        int found = -1;

        for(int c = 0; c < palette_count; ++c) {
            if(palette[c].r == pixel[0] && palette[c].g == pixel[1] && palette[c].b == pixel[2]) {
                found = c;
                break;
            }
        }

        if(found < 0) {
            if(palette_count == MR_MAX_PALETTE_COLORS) {
                return MrStatus::TooManyColors;
            }

            palette[palette_count] = {pixel[0], pixel[1], pixel[2]};
            found = palette_count++;
        }

        indexed[i] = (uint8_t) found;
    }

    std::vector<uint8_t> compressed = mr_compress(indexed);

    uint32_t offset = 2 + 7 * 4 + (uint32_t) palette_count * 4;
    uint32_t size = offset + (uint32_t) compressed.size();
    uint32_t zero = 0;

    std::vector<uint8_t> mr;
    mr.reserve(size);

    auto append = [&mr](const void* data, size_t len) {
        const uint8_t* bytes = (const uint8_t*) data;
        mr.insert(mr.end(), bytes, bytes + len);
    };

    /* The MR header: "MR" magic, then seven 4-byte fields (size, reserved,
     * data offset, width, height, reserved, color count), then one BGR+pad
     * quad per palette entry, then the RLE-compressed indexed pixels. */
    append("MR", 2);
    append(&size, 4);
    append(&zero, 4);
    append(&offset, 4);
    append(&width, 4);
    append(&height, 4);
    append(&zero, 4);
    append(&palette_count, 4);

    for(int i = 0; i < palette_count; ++i) {
        uint8_t entry[4] = {palette[i].b, palette[i].g, palette[i].r, 0};
        append(entry, 4);
    }

    append(compressed.data(), compressed.size());

    out = std::move(mr);
    return MrStatus::Ok;
}

} // namespace makeip

#endif /* MKDCDISC_WITH_PNG */
