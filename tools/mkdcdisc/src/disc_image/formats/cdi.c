/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/*
 * Padus DiscJuggler (CDI) disc image writer.
 *
 * A CDI holds everything in one file, but it is written (and read) back
 * to front. The raw track data comes first with no file header at all, a
 * descriptor is appended at the end, and the very last four bytes give the
 * distance from the descriptor's first byte to EOF (its own size plus those
 * four bytes), so a reader seeks backwards from the end of the file:
 *
 *   +------------------------------+ 0x00
 *   | Track data, back to back:    |
 *   |   audio: raw 2352-byte PCM   |
 *   |   data : 2336-byte sectors   |
 *   |   (pregaps ARE stored here)  |
 *   +------------------------------+ EOF - descriptor_size
 *   | Session count (u8)           |
 *   | Per session:                 |
 *   |   session header (7 bytes)   |
 *   |   per track:                 |
 *   |     track header (88 bytes)  |
 *   |     track descriptor         |
 *   |       (180 or 188 bytes)     |
 *   | Terminator: a session header |
 *   |   declaring 0 tracks, one    |
 *   |   more track header, and the |
 *   |   disc info block (79 bytes) |
 *   +------------------------------+ EOF - 4
 *   | descriptor_size (u32)        |
 *   +------------------------------+ EOF
 *
 * Every integer is little-endian (unlike NRG, which is big-endian), so the
 * descriptor is written as packed structs rather than field by field.
 *
 * Data tracks are stored as 2336-byte sectors -- subheader + user data +
 * EDC/ECC, with the 12-byte sync and the 4-byte header dropped (`read_mode`
 * 1); audio tracks as full 2352-byte PCM (`read_mode` 2). Pregaps and
 * postgaps are physically present in the file, unlike MDS/MDF where they are
 * only declared and synthesized by the burning software. Gap sectors on a
 * data track are empty XA Mode 2 Form 2 sectors, the same filler the MDS and
 * NRG writers use. Since the sector header is not stored, and Form 1 EDC
 * covers only the subheader and user data while the P/Q parity is computed
 * over a zeroed header, the address handed to raw_sector_encode_form1() never reaches
 * the file: the writer feeds it a track-relative sector number rather than
 * an absolute LBA. CD-DA tracks are padded with silence up to 302 stored
 * sectors (data + postgap), the same ~4-second floor mkdcdisc uses for its
 * silent placeholder audio track.
 *
 * The 88-byte track header repeats before *every* track descriptor even
 * though most of its contents are disc-wide (total track count, the 32-byte
 * space-padded image filename, a 360000-sector/80-minute medium length). It
 * opens with two 10-byte markers, which is what a reader keys on to locate
 * track entries.
 *
 * A track descriptor is 188 bytes on the last track of a session and 180
 * bytes otherwise: the trailing 8 bytes (a next-track flag and a repeat of
 * the start LBA) are only emitted when the track closes its session, and
 * `session_type` likewise carries the session's mode only there. Its
 * meaningful fields are the pregap and sector counts, `track_mode`
 * (0 = Audio, 1 = Mode 1, 2 = Mode 2), `read_mode` (sector size),
 * `control` (0 audio, 4 data), the session and track numbers, the start LBA
 * of the first *data* sector (the pregap occupies the LBAs just before it,
 * so track 1 of session 1 starts at 0 with its pregap at -150), and the
 * track length with and without the pregap. The rest is padding carrying
 * values observed in DiscJuggler-produced images.
 *
 * The closing disc info block records the total sector count, a 32-byte
 * space-padded volume id, empty EAN-13/CD-TEXT fields, and an image version
 * of 0x80000006 -- "CDI v3.5" in cdirip's terms, the version in which the
 * trailing u32 is a size counted back from EOF rather than an absolute file
 * offset.
 *
 * Padus never published a spec; the layout above is reverse engineered, and
 * follows what cdirip parses and what the cdi4dc lineage has long written.
 */

#include <string.h>

#include "disc_image/disc_image.h"
#include "disc_image/private.h"
#include "disc_image/raw_sector.h"


static void write_mode2_pregap(FILE* output, size_t sectors) {
    const uint8_t pregap_header[] = {
        0x00, 0x00, 0x20, 0x00,
        0x00, 0x00, 0x20, 0x00
    };

    const uint8_t pregap_edc [] = {
        0x3F, 0x13, 0xB0, 0xBE
    };

    uint8_t pregap_sector[2336] = {0};
    /* sizeof(pregap_header) into a 2336-byte sector.
     * nosemgrep: insecure-use-memcpy-fn */
    memcpy(pregap_sector, pregap_header, sizeof(pregap_header));
    /* Offset and length are both sizeof() on the same two arrays, so the copy
     * ends exactly on the end of pregap_sector[].
     * nosemgrep: insecure-use-memcpy-fn */
    memcpy(
        pregap_sector + sizeof(pregap_sector) - sizeof(pregap_edc),
        pregap_edc,
        sizeof(pregap_edc)
    );

    for(int i = 0; i < sectors; ++i) {
        fwrite(pregap_sector, sizeof(pregap_sector), 1, output);
    }
}

/* The floor every CD-DA track is padded up to, data plus postgap: ~4 seconds,
 * the same length as mkdcdisc's silent placeholder audio track. */
#define CDDA_MIN_STORED_SECTORS 302

/* What a track costs in the file, which is what progress is counted in.
 * Pregaps and postgaps are physically stored in a CDI, unlike MDS/MDF. */
static size_t cdi_track_stored_sectors(const cd_track_t* track) {
    size_t data_and_postgap = cd_track_data_size_in_sectors(track) + track->postgap_sectors;

    if(cd_track_mode(track) == TRACK_MODE_CDDA && data_and_postgap < CDDA_MIN_STORED_SECTORS) {
        data_and_postgap = CDDA_MIN_STORED_SECTORS;
    }

    return track->pregap_sectors + data_and_postgap;
}

static uint64_t cdi_stored_sectors(const cd_image_t* image) {
    uint64_t total = 0;

    for(size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        for(size_t t = 0; t < cd_session_track_count(session); ++t) {
            total += cdi_track_stored_sectors(cd_session_get_track(session, t));
        }
    }

    return total;
}

static bool write_track_cdda(const cd_track_t* track, FILE* output, cd_write_progress_t* progress) {
    cd_write_silent_sectors(output, track->pregap_sectors);
    if(cd_write_progress_add(progress, track->pregap_sectors)) {
        return false;
    }

    size_t sectors_written = 0;

    fwrite(track->data, track->data_len, 1, output);  /* Write the data raw */
    cd_write_sector_tail_padding(output, track->data_len, RAW_SECTOR_SIZE);

    sectors_written += cd_track_data_size_in_sectors(track);

    cd_write_silent_sectors(output, track->postgap_sectors);
    sectors_written += track->postgap_sectors;

    if(sectors_written < CDDA_MIN_STORED_SECTORS) {
        cd_write_silent_sectors(output, CDDA_MIN_STORED_SECTORS - sectors_written);
        sectors_written = CDDA_MIN_STORED_SECTORS;
    }

    /* One lump: audio is written whole rather than sector by sector, and it is
     * a small part of a Dreamcast image next to the data track. */
    return cd_write_progress_add(progress, sectors_written) == 0;
}

static bool write_track_xa_mode2_form1(const cd_track_t* track, FILE* output,
                                       cd_write_progress_t* progress) {
    size_t sectors_written = track->pregap_sectors;
    write_mode2_pregap(output, sectors_written);
    if(cd_write_progress_add(progress, track->pregap_sectors)) {
        return false;
    }

    /* Assembled whole, then stored from the subheader on: the sync field and
     * header are not kept in a CDI data track. */
    uint8_t sector[RAW_SECTOR_SIZE];
    uint8_t* stored = sector + RAW_SECTOR_FORM1_SUBHEADER_OFFSET;

    size_t bytes_written = 0;
    for(size_t i = 0; i + RAW_SECTOR_USER_DATA_SIZE <= track->data_len;
            i += RAW_SECTOR_USER_DATA_SIZE) {
        raw_sector_encode_form1(sector, track->data + i,
            RAW_SECTOR_USER_DATA_SIZE, (uint32_t) sectors_written);

        fwrite(stored, 1, RAW_SECTOR_FORM1_STORED_SIZE, output);
        bytes_written += RAW_SECTOR_USER_DATA_SIZE;
        sectors_written++;
        if(cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    size_t remaining = track->data_len - bytes_written;
    if(remaining) {
        raw_sector_encode_form1(sector, track->data + bytes_written, remaining,
            (uint32_t) sectors_written);
        fwrite(stored, 1, RAW_SECTOR_FORM1_STORED_SIZE, output);
        sectors_written++;
        if(cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    /* Write the post-gap */
    write_mode2_pregap(output, track->postgap_sectors);
    return cd_write_progress_add(progress, track->postgap_sectors) == 0;
}

static void write_cdi_header(const cd_image_t* image, FILE* output, const char* filename) {

#define TRACK_MARKER {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}

    uint8_t session_count = cd_image_session_count(image);
    uint8_t total_tracks = cd_image_track_count(image);

    uint32_t header_offset = ftell(output) - 1;

    fwrite(&session_count, sizeof(session_count), 1, output);

    struct {
        uint8_t marker0[10];
        uint8_t marker1[10];
        uint8_t settings[3];
        uint8_t total_tracks;
        uint8_t filename_length;
        uint8_t filename[32];
        uint8_t unknown0[11];
        uint32_t unknown1;
        uint32_t unknown2;
        uint32_t unknown3;
        uint32_t max_cd_length;
        uint32_t unknown4;
    } __attribute__((packed)) track_header = {
        TRACK_MARKER, TRACK_MARKER,
        {0xAB, 0x0, 0x10},
        total_tracks,
        32,
        {0}, /* Fill the filename with spaces in a mo */
        {0}, 2, 0, 0x80000000, /* Unknowns */
        360000,
        0x980000
    };

    struct {
        uint8_t padding;
        uint16_t track_count;
        uint32_t unknown;
    } __attribute__((packed)) session_header = {0, 0, 0};

    memset(track_header.filename, 0x20, sizeof(track_header.filename));
    /* A fixed-width CDI record field preceded by its own length (filename_length
     * above), written out whole and never read back as a C string: the missing
     * terminator on a full-width name is the format, not an oversight.
     * nosemgrep: insecure-use-string-copy-fn */
    strncpy((char*) track_header.filename, filename, sizeof(track_header.filename));

    for(int j = 0; j < session_count; ++j) {
        cd_session_t* s = cd_image_get_session(image, j);

        session_header.track_count = cd_session_track_count(s);

        fwrite(&session_header, sizeof(session_header), 1, output);

        for(int i = 0; i < session_header.track_count; ++i) {
            fwrite(&track_header, sizeof(track_header), 1, output);

            cd_track_t* t = cd_session_get_track(s, i);

            uint32_t track_mode = (cd_track_mode(t) == TRACK_MODE_CDDA) ? 0 :
                (cd_track_mode(t) == TRACK_MODE_MODE1) ? 1 : 2;

            /*
             *    0: Mode1,        800h, 2048
                  1: Mode2,        920h, 2336
                  2: Audio,        930h, 2352
                  3: Raw+PQ,       940h, 2352+16 non-interleaved (P=only 1bit)
                  4: Raw+PQRSTUVW, 990h, 2352+96 interleaved */
            uint32_t read_mode = (cd_track_type(t) == TRACK_TYPE_AUDIO) ? 2 : 1; /* (0 = 2048, 1 = 2336, 2 = 2352)  */

            uint32_t track_length = cd_track_data_size_in_sectors(t) + cd_track_postgap_sectors(t);

            struct {
                uint16_t index_count;
                uint32_t pregap_sectors;
                uint32_t sector_count;
                uint8_t unknown0[6];
                uint32_t track_mode;
                uint32_t unknown1;
                uint32_t session_number;
                uint32_t track_number;
                uint32_t start_lba;
                uint32_t total_length;  /* Including pregap (+postgap?) */
                uint8_t unknown2[16];
                uint32_t read_mode;
                uint32_t control;
                uint8_t unknown3;
                uint32_t total_length2; /* ??? */
                uint32_t unknown4;
                uint8_t isrc_code[12];
                uint32_t isrc_valid;
                uint8_t unknown5;
                uint8_t unknown6[8];
                uint32_t unknown7;
                uint32_t unknown8;
                uint32_t unknown9;
                uint32_t unknown10;
                uint32_t audio_frequency;
                uint8_t unknown11[42];
                uint32_t unknown12;
                uint8_t unknown13[12];
                uint8_t session_type; /* ONLY if last track of a session (else 0) (0=Audio/CD-DA, 1=Mode1/CD-ROM, 2=Mode2/CD-XA) */
                uint8_t unknown14[5];
                uint8_t has_next_track;
                uint8_t unknown15;
                uint32_t unknown16; /* Address of last track in a session? */
            } __attribute__((packed)) track_data = {
                2,
                cd_track_pregap_sectors(t),
                track_length,
                {0},
                track_mode,
                0,
                j,
                i,
                cd_track_start_lba(t),
                track_length + cd_track_pregap_sectors(t),
                {0},
                read_mode,
                (cd_track_type(t) == TRACK_TYPE_AUDIO) ? 0 : 4,
                0,
                track_length + cd_track_pregap_sectors(t),
                0,
                {0},
                0,
                0, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 1, 0x00000080, 0x00000002, 0x00000010, 44100, {0}, 0xFFFFFFFF, {0},   // Unknowns
                (t->next_track) ? 0 : track_mode,
                {0},
                t->next_track != NULL,
                0,
                cd_track_start_lba(t) };

            /* The final 8 bytes are only written on the last track of the session! */
            int size = (t->next_track) ? sizeof(track_data) - 8 : sizeof(track_data);
            fwrite(&track_data, size, 1, output);
        }
    }

    uint32_t end_lba = 0;
    for(int k = 0; k < session_count; ++k) {
        end_lba += cd_session_length_in_sectors(cd_image_get_session(image, k));
    }

    struct {
        uint32_t total_sectors;
        uint8_t vol_id_length;
        uint8_t vol_id[32];
        uint8_t unknown0;
        uint32_t unknown1;
        uint32_t unknown2;
        uint8_t ean_13_code[13];
        uint32_t ean_code_valid;
        uint8_t cd_text_length;
        uint32_t unknown3;
        uint32_t unknown4;
        uint8_t unknown5[3];
        uint32_t image_version;
    } __attribute__((packed)) disc_info = {
        end_lba, 32, {0}, 0, 1, 1, {0}, 0, 0, 0, 0, {0}, 0x80000006
    };

    memset(disc_info.vol_id, 0x20, sizeof(disc_info.vol_id));
    /* Fixed-width and length-prefixed like track_header.filename above.
     * nosemgrep: insecure-use-string-copy-fn */
    strncpy((char*) disc_info.vol_id, filename, sizeof(disc_info.vol_id));

    session_header.track_count = 0;
    fwrite(&session_header, sizeof(session_header), 1, output);
    fwrite(&track_header, sizeof(track_header), 1, output);
    fwrite(&disc_info, sizeof(disc_info), 1, output);

    header_offset = (ftell(output) - 1) - header_offset + 4;
    fwrite(&header_offset, sizeof(header_offset), 1, output);
}

bool cd_write_to_cdi(const cd_image_t* image, FILE* output, const char* filename) {
    /* Same Red Book limits enforced by cd_write_to_mds(): at most 255 sessions
     * and 99 tracks (BCD 01-99). Also guards against wrapping the CDI header's
     * uint8_t session/track counts. */
    if(cd_image_session_count(image) == 0 || cd_image_session_count(image) > 255 ||
       cd_image_track_count(image) == 0 || cd_image_track_count(image) > CD_MAX_TRACKS) {
        return false;
    }

    /* First of all we need to write the track data. This apparently is
     * written in sectors, but ignoring the first 16 bytes of header on
     * data tracks, but including the subheader and error correction/detection */

    cd_write_progress_t progress;
    cd_write_progress_init(&progress, image, cdi_stored_sectors(image));

    for(size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        for(size_t t = 0; t < cd_session_track_count(session); ++t) {
            cd_track_t* track = cd_session_get_track(session, t);

            switch(cd_track_mode(track)) {
            case TRACK_MODE_CDDA:
                if(!write_track_cdda(track, output, &progress)) {
                    return false;
                }
            break;
            case TRACK_MODE_XA_MODE2_FORM1:
                if(!write_track_xa_mode2_form1(track, output, &progress)) {
                    return false;
                }
            break;
            default:
                /* Not produced by mkdcdisc */
                return false;
            }
        }
    }

    write_cdi_header(image, output, filename);
    return !ferror(output);
}

cd_image_t* cd_load_from_cdi(FILE* input) {
    return NULL;
}
