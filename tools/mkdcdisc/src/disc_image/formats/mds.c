/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/*
 * Alcohol 120% (MDS/MDF) disc image writer.
 *
 * The MDS descriptor written here follows the "MEDIA DESCRIPTOR" v1.4
 * layout used by Alcohol 120%:
 *
 *   +------------------------------+ 0x00
 *   | Header (88 bytes)            |
 *   +------------------------------+ 0x58
 *   | Session blocks (24 bytes ea) |
 *   +------------------------------+
 *   | Per session, 80-byte blocks: |
 *   |   A0/A1/A2 lead-in entries   |
 *   |   real track entries         |
 *   |   B0 (+C0/C1 in session 1)   |
 *   +------------------------------+
 *   | Extra blocks (8 bytes per    |
 *   |   80-byte block above; only  |
 *   |   real tracks carry data)    |
 *   +------------------------------+
 *   | Footer (16 bytes) + "*.mdf"  |
 *   +------------------------------+
 *
 * The MDF contains full raw 2352-byte sectors (sync + header + subheader +
 * user data + EDC/ECC for data tracks, plain PCM for audio tracks). Track
 * pregaps are *not* stored in the MDF; they are declared in the extra
 * blocks and synthesized by the burning software, which is the convention
 * used by Alcohol for DAO images.
 *
 * This is a modernized take on the MDS support originally written for
 * img4dc/mds4dc and based on format notes by Henrik Stokseth.
 * With this rewrite, mds4dc is no longer needed and has been retired!
 */

#include <stdlib.h>
#include <string.h>

#include "disc_image/disc_image.h"
#include "disc_image/private.h"
#include "disc_image/raw_sector.h"


#define MDS_BLOCK_SIZE 80
#define MDS_HEADER_SIZE 88
#define MDS_SESSION_BLOCK_SIZE 24
#define MDS_EXTRA_BLOCK_SIZE 8
#define MDS_FOOTER_SIZE 16

/* Track mode bytes used by Alcohol */
#define MDS_MODE_NONE        0x00
#define MDS_MODE_AUDIO       0xA9
#define MDS_MODE_MODE1       0xAA
#define MDS_MODE_MODE2       0xAB
#define MDS_MODE_MODE2_FORM1 0xEC /* Value Alcohol uses for XA Mode 2 Form 1 */
#define MDS_MODE_MODE2_FORM2 0xAD

/* ADR/CTL bytes */
#define MDS_ADR_CTL_AUDIO 0x10
#define MDS_ADR_CTL_DATA  0x14
#define MDS_ADR_CTL_MODE5 0x50

/* Standard gap between two sessions: lead-out of the previous session
 * followed by the lead-in of the next one. */
#define LEAD_IN_SECTORS 4500
#define FIRST_LEAD_OUT_SECTORS 6750
#define NEXT_LEAD_OUT_SECTORS 2250

/* MSF <-> LBA bias mandated by the Red Book */
#define MSF_OFFSET 150

static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t) (v);
    p[1] = (uint8_t) (v >> 8);
}

static void put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t) (v);
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static void put_u64(uint8_t* p, uint64_t v) {
    put_u32(p, (uint32_t) (v & 0xFFFFFFFFu));
    put_u32(p + 4, (uint32_t) (v >> 32));
}

static void lba_to_msf(int32_t lba, uint8_t* m, uint8_t* s, uint8_t* f) {
    uint32_t frames = (uint32_t) (lba + MSF_OFFSET);
    *m = (uint8_t) (frames / (60 * 75));
    *s = (uint8_t) ((frames / 75) % 60);
    *f = (uint8_t) (frames % 75);
}

static uint8_t bcd(uint8_t v) {
    return (uint8_t) (((v / 10) << 4) | (v % 10));
}

/* Number of sectors a track occupies in the MDF: its data (rounded up to
 * whole sectors) plus its post-gap. Pregaps are declared, not stored. */
static size_t track_stored_sectors(const cd_track_t* track) {
    return cd_track_data_size_in_sectors(track) + track->postgap_sectors;
}

static size_t session_lead_out_sectors(const cd_image_t* image, size_t session_idx) {
    (void) image;
    return (session_idx == 0) ? FIRST_LEAD_OUT_SECTORS : NEXT_LEAD_OUT_SECTORS;
}

/* Per-session layout, computed once and shared by the MDS and MDF writers */
typedef struct {
    int32_t session_start;  /* Program area start (first track pregap included, hence usually negative for session 1) */
    int32_t session_end;    /* Program area end (lead-out start) */
    uint16_t first_track;   /* Global number of the first track */
    uint16_t last_track;    /* Global number of the last track */
    size_t block_count;     /* Number of 80-byte blocks for this session */
} mds_session_layout_t;

typedef struct {
    uint32_t start_sector;  /* LBA of the first *data* sector (pregap excluded) */
    uint64_t start_offset;  /* Byte offset of the track inside the MDF */
    uint32_t stored_sectors;
    uint32_t pregap_sectors;
} mds_track_layout_t;

static uint8_t mds_track_mode(const cd_track_t* track) {
    switch (cd_track_mode(track)) {
        case TRACK_MODE_CDDA: return MDS_MODE_AUDIO;
        case TRACK_MODE_MODE1: return MDS_MODE_MODE1;
        case TRACK_MODE_MODE2: return MDS_MODE_MODE2;
        case TRACK_MODE_XA_MODE2_FORM1: return MDS_MODE_MODE2_FORM1;
        case TRACK_MODE_XA_MODE2_FORM2: return MDS_MODE_MODE2_FORM2;
    }
    return MDS_MODE_NONE;
}

static uint8_t mds_track_adr_ctl(const cd_track_t* track) {
    return (cd_track_type(track) == TRACK_TYPE_AUDIO) ? MDS_ADR_CTL_AUDIO : MDS_ADR_CTL_DATA;
}

static bool compute_layout(
        const cd_image_t* image,
        mds_session_layout_t* sessions,
        mds_track_layout_t* tracks) {

    size_t session_count = cd_image_session_count(image);
    uint16_t track_number = 1;
    size_t track_idx = 0;
    int32_t cursor = 0;
    uint64_t mdf_offset = 0;

    for (size_t s = 0; s < session_count; ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        size_t n_tracks = cd_session_track_count(session);

        if (n_tracks == 0) {
            return false;
        }

        if (s > 0) {
            /* Skip the lead-out of the previous session and our lead-in */
            cursor += (int32_t) session_lead_out_sectors(image, s - 1);
            cursor += LEAD_IN_SECTORS;
        }

        cd_track_t* first = cd_session_get_track(session, 0);

        /* The program area opens with the pregap of the first track. For
         * the first session this lands at a negative LBA (-150 with the
         * standard pregap), mirroring a real disc. */
        sessions[s].session_start = (s == 0) ?
            -((int32_t) first->pregap_sectors) :
            cursor;

        sessions[s].first_track = track_number;

        for (size_t t = 0; t < n_tracks; ++t) {
            cd_track_t* track = cd_session_get_track(session, t);

            if (s == 0 && t == 0) {
                /* Track 1's pregap sits at negative LBAs; its data
                 * starts at LBA 0. */
                cursor = 0;
            } else {
                cursor += (int32_t) track->pregap_sectors;
            }

            tracks[track_idx].start_sector = (uint32_t) cursor;
            tracks[track_idx].start_offset = mdf_offset;
            tracks[track_idx].stored_sectors = (uint32_t) track_stored_sectors(track);
            tracks[track_idx].pregap_sectors = (uint32_t) track->pregap_sectors;

            cursor += (int32_t) tracks[track_idx].stored_sectors;
            mdf_offset += (uint64_t) tracks[track_idx].stored_sectors * RAW_SECTOR_SIZE;

            ++track_idx;
            ++track_number;
        }

        sessions[s].last_track = (uint16_t) (track_number - 1);
        sessions[s].session_end = cursor;

        /* A0/A1/A2, the real tracks, then B0 (+ C0/C1 in the first
         * session's lead-in, mirroring what a dump of a pressed
         * multisession disc contains). */
        sessions[s].block_count = 3 + n_tracks + ((s == 0) ? 3 : 1);
    }

    return true;
}

/* -------------------------------------------------------------------- */
/* MDS                                                                   */
/* -------------------------------------------------------------------- */

static void write_lead_in_block(
        FILE* mds,
        uint8_t adr_ctl,
        uint8_t point,
        uint8_t m, uint8_t s, uint8_t f,
        uint8_t zero,
        uint8_t pm, uint8_t ps, uint8_t pf) {

    uint8_t block[MDS_BLOCK_SIZE] = {0};

    block[0x00] = MDS_MODE_NONE;
    block[0x01] = 0; /* No subchannel */
    block[0x02] = adr_ctl;
    block[0x04] = point;
    block[0x05] = m;
    block[0x06] = s;
    block[0x07] = f;
    block[0x08] = zero;
    block[0x09] = pm;
    block[0x0A] = ps;
    block[0x0B] = pf;

    fwrite(block, sizeof(block), 1, mds);
}

static void write_track_block(
        FILE* mds,
        const cd_track_t* track,
        uint16_t track_number,
        const mds_track_layout_t* layout,
        uint32_t extra_offset,
        uint32_t footer_offset) {

    uint8_t block[MDS_BLOCK_SIZE] = {0};
    uint8_t m, s, f;

    lba_to_msf((int32_t) layout->start_sector, &m, &s, &f);

    block[0x00] = mds_track_mode(track);
    block[0x01] = 0; /* No subchannel data in the MDF */
    block[0x02] = mds_track_adr_ctl(track);
    block[0x04] = (uint8_t) track_number; /* Point */
    block[0x09] = m;
    block[0x0A] = s;
    block[0x0B] = f;
    put_u32(block + 0x0C, extra_offset);
    put_u16(block + 0x10, RAW_SECTOR_SIZE);
    block[0x12] = 0x02; /* Unknown, present in Alcohol-generated files */
    put_u32(block + 0x24, layout->start_sector);
    put_u64(block + 0x28, layout->start_offset);
    put_u32(block + 0x30, 1); /* Number of filenames (one MDF) */
    put_u32(block + 0x34, footer_offset);

    fwrite(block, sizeof(block), 1, mds);
}

static bool write_mds(
        const cd_image_t* image,
        FILE* mds,
        const mds_session_layout_t* sessions,
        const mds_track_layout_t* tracks) {

    size_t session_count = cd_image_session_count(image);

    size_t total_blocks = 0;
    for (size_t s = 0; s < session_count; ++s) {
        total_blocks += sessions[s].block_count;
    }

    uint32_t blocks_offset = MDS_HEADER_SIZE + (uint32_t) session_count * MDS_SESSION_BLOCK_SIZE;
    uint32_t extra_offset = blocks_offset + (uint32_t) total_blocks * MDS_BLOCK_SIZE;
    uint32_t footer_offset = extra_offset + (uint32_t) total_blocks * MDS_EXTRA_BLOCK_SIZE;
    uint32_t filename_offset = footer_offset + MDS_FOOTER_SIZE;

    /* Header */
    {
        uint8_t header[MDS_HEADER_SIZE] = {0};

        /* 16 constant bytes into header[MDS_HEADER_SIZE].
         * nosemgrep: insecure-use-memcpy-fn */
        memcpy(header, "MEDIA DESCRIPTOR", 16);
        header[0x10] = 0x01; /* Format version 1.4 */
        header[0x11] = 0x04;
        put_u16(header + 0x12, 0x0000); /* Medium type: CD-ROM */
        put_u16(header + 0x14, (uint16_t) session_count);
        put_u16(header + 0x16, (uint16_t) session_count);
        put_u32(header + 0x50, MDS_HEADER_SIZE); /* Session blocks offset */

        fwrite(header, sizeof(header), 1, mds);
    }

    /* Session blocks */
    {
        uint32_t session_blocks_offset = blocks_offset;

        for (size_t s = 0; s < session_count; ++s) {
            cd_session_t* session = cd_image_get_session(image, s);
            uint8_t block[MDS_SESSION_BLOCK_SIZE] = {0};

            put_u32(block + 0x00, (uint32_t) sessions[s].session_start);
            put_u32(block + 0x04, (uint32_t) sessions[s].session_end);
            put_u16(block + 0x08, (uint16_t) (s + 1));
            block[0x0A] = (uint8_t) sessions[s].block_count;
            block[0x0B] = 3; /* Lead-in blocks before the first track */
            put_u16(block + 0x0C, sessions[s].first_track);
            put_u16(block + 0x0E, sessions[s].last_track);
            put_u32(block + 0x14, session_blocks_offset);

            fwrite(block, sizeof(block), 1, mds);

            session_blocks_offset += (uint32_t) sessions[s].block_count * MDS_BLOCK_SIZE;
            (void) session;
        }
    }

    /* Track blocks */
    {
        size_t track_idx = 0;
        uint16_t track_number = 1;
        size_t session_first_block = 0;

        for (size_t s = 0; s < session_count; ++s) {
            cd_session_t* session = cd_image_get_session(image, s);
            size_t n_tracks = cd_session_track_count(session);
            cd_track_t* first = cd_session_get_track(session, 0);

            uint8_t session_adr_ctl = mds_track_adr_ctl(first);
            uint8_t m, sec, f;

            /* A0: first track number; PSEC flags a CD-XA program area */
            uint8_t a0_psec = (cd_track_type(first) == TRACK_TYPE_DATA) ? 0x20 : 0x00;
            write_lead_in_block(mds, session_adr_ctl, 0xA0,
                                0, 0, 0, 0,
                                (uint8_t) sessions[s].first_track, a0_psec, 0);

            /* A1: last track number */
            write_lead_in_block(mds, session_adr_ctl, 0xA1,
                                0, 0, 0, 0,
                                (uint8_t) sessions[s].last_track, 0, 0);

            /* A2: lead-out start */
            lba_to_msf(sessions[s].session_end, &m, &sec, &f);
            write_lead_in_block(mds, session_adr_ctl, 0xA2,
                                0, 0, 0, 0,
                                m, sec, f);

            /* Real tracks. Each one points at its own slot in the extra
             * block array, which is indexed by block -- not by track --
             * so the three lead-in entries above are counted in. */
            for (size_t t = 0; t < n_tracks; ++t) {
                cd_track_t* track = cd_session_get_track(session, t);
                size_t block_idx = session_first_block + 3 + t;

                write_track_block(mds, track, track_number,
                                  &tracks[track_idx],
                                  extra_offset + (uint32_t) block_idx * MDS_EXTRA_BLOCK_SIZE,
                                  footer_offset);

                ++track_idx;
                ++track_number;
            }

            /* B0: multisession pointer. Points at the next session's
             * program area, or FF:FF:FF in the last (closing) session. */
            if (s + 1 < session_count) {
                lba_to_msf(sessions[s + 1].session_start, &m, &sec, &f);
                write_lead_in_block(mds, MDS_ADR_CTL_MODE5, 0xB0,
                                    m, sec, f, 0x03,
                                    0x4F, 0x3B, 0x4A); /* 79:59:74 */
            } else {
                write_lead_in_block(mds, MDS_ADR_CTL_MODE5, 0xB0,
                                    0xFF, 0xFF, 0xFF, 0x01,
                                    0x4F, 0x3B, 0x4A);
            }

            /* C0/C1: ATIP-style entries, only in the first lead-in.
             * Fixed values lifted from a dump of a pressed disc. */
            if (s == 0) {
                write_lead_in_block(mds, MDS_ADR_CTL_MODE5, 0xC0,
                                    0x46, 0x00, 0x9E, 0,
                                    0x61, 0x22, 0x17); /* 97:34:23 */
                write_lead_in_block(mds, MDS_ADR_CTL_MODE5, 0xC1,
                                    0x48, 0x58, 0xB8, 0,
                                    0x00, 0x00, 0x00);
            }

            session_first_block += sessions[s].block_count;
        }
    }

    /* Extra blocks: one 8-byte slot per block written above, in the same
     * order. Real tracks carry their pregap and length; the A0/A1/A2 and
     * B0/C0/C1 lead-in entries get a zeroed slot.
     *
     * Only tracks reference a slot, so the padding looks wasteful -- but
     * Alcohol and img4dc both size the array this way, and readers exist
     * that ignore extra_offset and recompute a slot address from the block
     * index instead. A per-track array makes those read the wrong slot,
     * and past the end of the file once there is more than one session. */
    {
        static const uint8_t empty[MDS_EXTRA_BLOCK_SIZE] = {0};
        size_t track_idx = 0;

        for (size_t s = 0; s < session_count; ++s) {
            cd_session_t* session = cd_image_get_session(image, s);
            size_t n_tracks = cd_session_track_count(session);

            /* A0/A1/A2 */
            for (size_t i = 0; i < 3; ++i) {
                fwrite(empty, sizeof(empty), 1, mds);
            }

            for (size_t t = 0; t < n_tracks; ++t) {
                uint8_t block[MDS_EXTRA_BLOCK_SIZE];
                put_u32(block + 0x00, tracks[track_idx].pregap_sectors);
                put_u32(block + 0x04, tracks[track_idx].stored_sectors);
                fwrite(block, sizeof(block), 1, mds);
                ++track_idx;
            }

            /* B0, plus C0/C1 in the first session */
            for (size_t i = 3 + n_tracks; i < sessions[s].block_count; ++i) {
                fwrite(empty, sizeof(empty), 1, mds);
            }
        }
    }

    /* Footer. The wildcard filename tells Alcohol to look for an MDF
     * sharing the MDS's base name. */
    {
        uint8_t footer[MDS_FOOTER_SIZE] = {0};
        put_u32(footer + 0x00, filename_offset);
        /* +0x04: 0 = 8-bit filename; remainder reserved */
        fwrite(footer, sizeof(footer), 1, mds);
        fwrite("*.mdf", 6, 1, mds); /* Including the NUL terminator */
    }

    return !ferror(mds);
}

/* -------------------------------------------------------------------- */
/* MDF                                                                   */
/* -------------------------------------------------------------------- */

static bool write_mdf_track_cdda(const cd_track_t* track, FILE* mdf,
                                 cd_write_progress_t* progress) {
    fwrite(track->data, track->data_len, 1, mdf);
    cd_write_sector_tail_padding(mdf, track->data_len, RAW_SECTOR_SIZE);
    cd_write_silent_sectors(mdf, track->postgap_sectors);

    /* One lump: audio is written whole rather than sector by sector, and it is
     * a small part of a Dreamcast image next to the data track. */
    return cd_write_progress_add(progress, track_stored_sectors(track)) == 0;
}

/* Empty XA Mode 2 Form 2 sector, the same filler mkdcdisc uses for gaps
 * in CDI images, here in raw 2352-byte form (so with sync and header). */
static void write_mdf_form2_gap_sectors(FILE* mdf, uint32_t lba, size_t count) {
    static const uint8_t SYNC[12] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
    };

    /* EDC of the constant payload below; the Form 2 EDC does not cover
     * the header, so it stays valid while the MSF changes. */
    static const uint8_t FORM2_EDC[4] = {0x3F, 0x13, 0xB0, 0xBE};

    uint8_t sector[RAW_SECTOR_SIZE] = {0};
    uint8_t m, s, f;

    /* sizeof(SYNC) into sector[RAW_SECTOR_SIZE].
     * nosemgrep: insecure-use-memcpy-fn */
    memcpy(sector, SYNC, sizeof(SYNC));
    sector[0x0F] = 0x02; /* Mode 2 */

    /* Subheader: empty Form 2 sector, duplicated as per the spec */
    sector[0x12] = 0x20;
    sector[0x16] = 0x20;

    /* Offset and length are both sizeof(FORM2_EDC) against RAW_SECTOR_SIZE, so
     * the copy ends exactly on the end of sector[].
     * nosemgrep: insecure-use-memcpy-fn */
    memcpy(sector + RAW_SECTOR_SIZE - sizeof(FORM2_EDC), FORM2_EDC, sizeof(FORM2_EDC));

    for (size_t i = 0; i < count; ++i) {
        lba_to_msf((int32_t) (lba + i), &m, &s, &f);
        sector[0x0C] = bcd(m);
        sector[0x0D] = bcd(s);
        sector[0x0E] = bcd(f);
        fwrite(sector, sizeof(sector), 1, mdf);
    }
}

static bool write_mdf_track_xa_mode2_form1(const cd_track_t* track, uint32_t start_lba, FILE* mdf,
                                           cd_write_progress_t* progress) {
    uint8_t sector[RAW_SECTOR_SIZE];

    uint32_t lba = start_lba;
    size_t bytes_written = 0;

    for (size_t i = 0; i + RAW_SECTOR_USER_DATA_SIZE <= track->data_len;
            i += RAW_SECTOR_USER_DATA_SIZE) {
        raw_sector_encode_form1(sector, track->data + i,
            RAW_SECTOR_USER_DATA_SIZE, lba + MSF_OFFSET);
        fwrite(sector, RAW_SECTOR_SIZE, 1, mdf);
        bytes_written += RAW_SECTOR_USER_DATA_SIZE;
        ++lba;
        if(cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    /* Partial trailing sector, zero padded */
    size_t remaining = track->data_len - bytes_written;
    if (remaining) {
        raw_sector_encode_form1(sector, track->data + bytes_written, remaining,
            lba + MSF_OFFSET);
        fwrite(sector, RAW_SECTOR_SIZE, 1, mdf);
        ++lba;
        if(cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    /* Post-gap */
    write_mdf_form2_gap_sectors(mdf, lba, track->postgap_sectors);
    return cd_write_progress_add(progress, track->postgap_sectors) == 0;
}

static bool write_mdf(
        const cd_image_t* image,
        FILE* mdf,
        const mds_track_layout_t* tracks) {

    size_t session_count = cd_image_session_count(image);
    size_t track_idx = 0;

    /* The MDS descriptor is a few kilobytes at most, so the MDF is all the
     * progress there is to report on. */
    uint64_t total_sectors = 0;
    for (size_t i = 0; i < cd_image_track_count(image); ++i) {
        total_sectors += tracks[i].stored_sectors;
    }

    cd_write_progress_t progress;
    cd_write_progress_init(&progress, image, total_sectors);

    for (size_t s = 0; s < session_count; ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        size_t n_tracks = cd_session_track_count(session);

        for (size_t t = 0; t < n_tracks; ++t) {
            cd_track_t* track = cd_session_get_track(session, t);

            switch (cd_track_mode(track)) {
                case TRACK_MODE_CDDA:
                    if (!write_mdf_track_cdda(track, mdf, &progress)) {
                        return false;
                    }
                    break;
                case TRACK_MODE_XA_MODE2_FORM1:
                    if (!write_mdf_track_xa_mode2_form1(track, tracks[track_idx].start_sector, mdf,
                                                        &progress)) {
                        return false;
                    }
                    break;
                default:
                    /* Not produced by mkdcdisc */
                    return false;
            }

            ++track_idx;
        }
    }

    return !ferror(mdf);
}

/* -------------------------------------------------------------------- */
/* Public API                                                            */
/* -------------------------------------------------------------------- */

bool cd_write_to_mds(const cd_image_t* image, FILE* mds, FILE* mdf) {
    size_t session_count = cd_image_session_count(image);
    size_t track_count = cd_image_track_count(image);

    if (session_count == 0 || session_count > 255 || track_count == 0 || track_count > CD_MAX_TRACKS) {
        return false;
    }

    mds_session_layout_t* sessions = calloc(session_count, sizeof(mds_session_layout_t));
    mds_track_layout_t* tracks = calloc(track_count, sizeof(mds_track_layout_t));

    bool ok = sessions && tracks && compute_layout(image, sessions, tracks);

    ok = ok && write_mds(image, mds, sessions, tracks);
    ok = ok && write_mdf(image, mdf, tracks);

    free(sessions);
    free(tracks);

    return ok;
}

cd_image_t* cd_load_from_mds(FILE* input) {
    /* Not implemented yet */
    (void) input;
    return NULL;
}
