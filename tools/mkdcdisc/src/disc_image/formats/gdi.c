/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/*
 * GD-ROM (.gdi) disc image writer.
 *
 * Unlike CDI/MDS/NRG, a GDI image isn't a single container file: it's a
 * plain-text track descriptor (the .gdi itself) plus one raw file per
 * track.
 *
 * Data tracks are written either as raw 2352-byte Mode 1 sectors (sync +
 * header + user data + EDC/ECC) or as plain 2048-byte user data. Raw is the
 * default: it is what real GD-ROM dumps overwhelmingly use. Audio tracks are
 * always 2352-byte PCM. The track filename follows the sector format, the
 * usual convention for .gdi sets:
 *
 *   track??.bin  raw 2352-byte Mode 1 data
 *   track??.iso  plain 2048-byte data
 *   track??.raw  CD-DA audio
 *
 * Each .gdi line is: track number, LBA, CTRL, sector size, filename, and a
 * trailing offset that is always 0. Line endings are CRLF.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "disc_image/disc_image.h"
#include "disc_image/private.h"
#include "disc_image/raw_sector.h"

#define GDI_CTRL_AUDIO 0
#define GDI_CTRL_DATA  4

#define AUDIO_SECTOR_SIZE RAW_SECTOR_SIZE

/* Mode 1 raw sector: 12 sync + 4 header + 2048 user + 4 EDC + 8 spare +
 * 276 P/Q parity. raw_sector_encode_mode1() lays all of that out; see
 * raw_sector.h. */

/* A sector header carries its address as MSF, and MSF counts from the start
 * of the lead-in rather than from LBA 0: FAD = LBA + 150. build_address()
 * in edc_ecc.c converts whatever it is given straight to MSF without adding
 * this itself, so the caller has to. */
#define CD_FAD_OFFSET 150

static bool join_track_path(const char* track_dir, const char* filename, char* out, size_t out_size) {
    int n;

    if(track_dir && track_dir[0] != '\0' && strcmp(track_dir, ".") != 0) {
        n = snprintf(out, out_size, "%s/%s", track_dir, filename);
    } else {
        n = snprintf(out, out_size, "%s", filename);
    }

    return n > 0 && (size_t) n < out_size;
}

/* Writes data verbatim, zero-padding the final sector when the payload isn't
 * a whole number of sectors. Used for plain 2048-byte data tracks and for
 * CD-DA, neither of which carries any per-sector framing. */
static bool write_plain(FILE* f, const uint8_t* data, size_t data_len, size_t sector_size,
                        cd_write_progress_t* progress) {
    /* Chunked rather than written in a single call, so a multi-hundred-
     * megabyte track is reported on more than once. The bytes that reach the
     * file are the same either way. */
    const size_t chunk_bytes = 512 * sector_size;
    size_t written = 0;

    while(written < data_len) {
        size_t n = data_len - written;
        if(n > chunk_bytes) {
            n = chunk_bytes;
        }

        fwrite(data + written, 1, n, f);
        written += n;

        /* A short final chunk leaves a partial sector uncounted here; the
         * padding below completes it. */
        if(cd_write_progress_add(progress, n / sector_size)) {
            return false;
        }
    }

    if(data_len % sector_size) {
        cd_write_sector_tail_padding(f, data_len, sector_size);
        if(cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    return !ferror(f);
}

/* Writes a data track as raw 2352-byte Mode 1 sectors. track_lba is the
 * track's own start LBA, needed because each sector's header encodes its
 * absolute address on the disc. */
static bool write_raw_mode1(FILE* f, const uint8_t* data, size_t data_len,
                            size_t sector_count, uint32_t track_lba,
                            cd_write_progress_t* progress) {
    for(size_t s = 0; s < sector_count; ++s) {
        uint8_t sector[CD_GDI_SECTOR_SIZE_RAW];

        /* Anything past the payload is zeroed: a short final sector is padded
         * inside the user-data area, before framing, so the EDC and ECC still
         * cover a full, valid sector. Sectors past the payload entirely -- the
         * track can be longer than its data -- come out empty but valid. */
        size_t offset = s * CD_GDI_SECTOR_SIZE_COOKED;
        const uint8_t* chunk = offset < data_len ? data + offset : NULL;
        size_t chunk_len = chunk ? data_len - offset : 0;

        raw_sector_encode_mode1(sector, chunk, chunk_len,
            track_lba + (uint32_t) s + CD_FAD_OFFSET);

        fwrite(sector, 1, sizeof(sector), f);
        if(cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    return !ferror(f);
}

static bool write_track_file(const cd_track_t* track, const char* path, bool is_data,
                             uint32_t data_sector_size, uint32_t track_lba,
                             cd_write_progress_t* progress) {
    /* nosemgrep: toctou-race-condition -- a track file beside the .gdi the
     * caller named, on the same footing as the writers in disc_layout.cpp: no
     * privilege boundary, so no race worth closing. See the note there. */
    FILE* f = fopen(path, "wb");
    if(!f) {
        return false;
    }

    const uint8_t* data = cd_track_data(track);
    size_t data_len = cd_track_data_size_in_bytes(track);
    bool ok;

    if(is_data && data_sector_size == CD_GDI_SECTOR_SIZE_RAW) {
        ok = write_raw_mode1(f, data, data_len, cd_track_data_size_in_sectors(track), track_lba,
                             progress);
    } else {
        ok = write_plain(f, data, data_len, is_data ? CD_GDI_SECTOR_SIZE_COOKED : AUDIO_SECTOR_SIZE,
                         progress);
    }

    fclose(f);
    return ok;
}

bool cd_write_to_gdi(const cd_image_t* image, FILE* gdi_file, const char* track_dir,
                     uint32_t data_sector_size) {
    size_t total_tracks = cd_image_track_count(image);
    if(!gdi_file || total_tracks == 0 || total_tracks > CD_MAX_TRACKS) {
        return false;
    }

    if(data_sector_size != CD_GDI_SECTOR_SIZE_RAW && data_sector_size != CD_GDI_SECTOR_SIZE_COOKED) {
        return false;
    }

    fprintf(gdi_file, "%u\r\n", (unsigned) total_tracks);

    /* A .gdi's track files hold no gap sectors at all -- the gaps are implied
     * by the LBAs the descriptor gives -- so only the tracks' own sectors are
     * ever written. */
    uint64_t total_sectors = 0;
    for(size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        for(size_t t = 0; t < cd_session_track_count(session); ++t) {
            total_sectors += cd_track_data_size_in_sectors(cd_session_get_track(session, t));
        }
    }

    cd_write_progress_t progress;
    cd_write_progress_init(&progress, image, total_sectors);

    uint32_t lba = 0;
    unsigned track_number = 0;
    bool ok = true;

    for(size_t s = 0; ok && s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        size_t n_tracks = cd_session_track_count(session);

        for(size_t t = 0; ok && t < n_tracks; ++t) {
            cd_track_t* track = cd_session_get_track(session, t);
            ++track_number;

            uint32_t placed_lba = cd_track_placement_lba(track);

            if(placed_lba != 0) {
                /* The caller positioned this track itself. Required once GD-DA
                 * is in play: consecutive audio tracks sit flush against one
                 * another with no pregap at all -- 0 on all 78 audio-to-audio
                 * boundaries across the reference dumps -- while data/audio
                 * boundaries keep their 150 sectors. The running walk below
                 * applies one pregap rule per track and cannot express that. */
                lba = placed_lba;
            } else if(track_number == 1) {
                lba = 0;
            } else if(t == 0) {
                /* First track of a later session: the fixed high-density
                 * zone start. There is no Red Book-style lead-in/lead-out
                 * gap on a GD-ROM between the low- and high-density zones
                 * -- the gap is a fixed physical constant, not something
                 * computed from the previous session's length. */
                lba = CD_GD_START_LBA;
            } else {
                lba += (uint32_t) cd_track_pregap_sectors(track);
            }

            bool is_data = cd_track_type(track) == TRACK_TYPE_DATA;
            uint32_t sector_size = is_data ? data_sector_size : AUDIO_SECTOR_SIZE;
            uint32_t ctrl = is_data ? GDI_CTRL_DATA : GDI_CTRL_AUDIO;

            const char* ext = !is_data ? "raw"
                : (data_sector_size == CD_GDI_SECTOR_SIZE_RAW ? "bin" : "iso");

            char filename[32];
            snprintf(filename, sizeof(filename), "track%02u.%s", track_number, ext);

            char path[1024];
            if(!join_track_path(track_dir, filename, path, sizeof(path))) {
                ok = false;
                break;
            }

            ok = write_track_file(track, path, is_data, data_sector_size, lba, &progress);
            if(!ok) {
                break;
            }

            fprintf(gdi_file, "%u %u %u %u %s 0\r\n", track_number, lba, ctrl, sector_size, filename);

            lba += (uint32_t) cd_track_data_size_in_sectors(track);
        }
    }

    return ok && !ferror(gdi_file);
}

cd_image_t* cd_load_from_gdi(FILE* input) {
    (void) input;
    return NULL;
}
