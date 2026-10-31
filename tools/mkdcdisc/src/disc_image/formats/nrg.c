/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/*
 * Nero Burning ROM (NRG) disc image writer, v2/DAO flavor only.
 *
 * mkdcdisc only ever needs Disc-At-Once master images (full cue sheet,
 * sector-accurate pregaps and session boundaries), so this writer produces
 * `CUEX`/`DAOX`/`SINF` chunks exclusively. It never emits the Track-At-Once
 * `ETNF`/`ETN2` chunks: TAO has no cue sheet and imposes automatic 2-second
 * inter-track gaps, which is incompatible with the Dreamcast's need for a
 * controlled MSINFO, a -150 pregap on track 1, and precise session
 * boundaries.
 *
 * Unlike CDI/MDS, every multi-byte integer in an NRG file is big-endian.
 * There is no file header: raw track sectors (including their pregap, which
 * IS physically stored here, unlike the "declared only" MDS/MDF pregap) are
 * written back-to-back first, followed by a trailer (a singly-linked chain
 * of chunks), followed by a 12-byte footer at the very end of the file:
 *
 *   "NER5" (4 bytes) + 8-byte big-endian absolute offset of the first chunk
 *
 * Each chunk is framed as: 4-byte ASCII ID + 4-byte big-endian payload
 * length + payload. The chain is walked by jumping to `offset + 8 + size`
 * and ends at an `END!` chunk (empty payload).
 *
 * Per session, a CUEX and a DAOX chunk are written (in that order); once
 * every session has its CUEX/DAOX pair, a SINF chunk follows for *each*
 * session (grouped together, not interleaved with the CUEX/DAOX pairs),
 * then a single image-wide MTYP, then the closing END! -- that is:
 * CUEX,DAOX,CUEX,DAOX,...,SINF,SINF,...,MTYP,END!
 *
 *   CUEX - cue sheet: 8-byte entries of
 *            {mode(u8) track(u8) index(u8) reserved(u8) lba(u32 BE, signed)}
 *          (n+1)*16 bytes = 2*(n+1) entries for n tracks:
 *            - 1 session-header entry: {session_mode, 0x00, 0x00, 0x00,
 *              session_start_lba} where session_start_lba is the first
 *              track's pregap-start LBA (-150, i.e. 0xFFFFFF6A, for session
 *              1's first track).
 *            - per real track, an index-0/index-1 pair: pregap start LBA
 *              then track data start LBA.
 *            - 1 session-trailer entry: {session_mode, 0xAA, 0x01, 0x00,
 *              session_end_lba} (a single entry, not an index0/1 pair).
 *          Every entry -- header, per-track, and trailer alike -- carries
 *          the bare session mode byte (0x01/0x41), with no extra flag bits.
 *          In particular, do not OR per-track entries with 0x20: IsoBuster
 *          then silently refuses to recognize any filesystem on the disc.
 *
 *   DAOX - Disc-At-Once track descriptors (64-bit offset variant): a
 *          22-byte common header
 *            {chunk_size(u32 BE) upc[13] pad(u8) toc_type(u16 BE)
 *             first_track(u8) last_track(u8)}
 *          toc_type is 0x0000 for an audio session, 0x2001 for a Mode 2/
 *          Form 1 data session (this file's only two session kinds),
 *          then one 42-byte record per track:
 *            {isrc[10] sector_size(u32 BE) mode(u32 BE)
 *             index0(u64 BE) index1(u64 BE) end_of_track(u64 BE)}
 *          index0/index1/end_of_track are *byte offsets into this file's
 *          track-data area* (not LBAs): start of pregap, start of real
 *          track data, and end of track (exclusive). Only the high byte of
 *          `mode` (`mode_code`) is actually interpreted by readers: 0x06 for
 *          full raw 2352-byte Mode 2 Form 1/2 sectors (what mkdcdisc always
 *          writes for its data track -- *not* 0x03, which per libmirage/
 *          cdemu means *cooked* 2336-byte Mode 2 Form 2), 0x07 for Audio.
 *          A different, unrelated value space from the CUEX `mode` byte
 *          above or the TAO-only `ETNF`/`ETN2` mode codes.
 *
 *   SINF - session info: a single big-endian u32, the track count of the
 *          session. Lets a reader quickly total up sessions/tracks without
 *          parsing the heavier CUEX/DAOX chunks.
 *
 * The trailing MTYP chunk is a big-endian u32, always value 1, regardless
 * of session layout.
 *
 * Nero never published an official NRG spec. The layout documented here
 * follows the ArchiveTeam NRG format page and the open readers that parse
 * the format in practice -- GNU libcdio (`lib/driver/image/nrg.{c,h}`),
 * cdemu/libmirage's `image-nrg` plugin, and `nerorip`
 * (https://gitlab.com/scallopedllama/nerorip) -- and matches what Nero
 * Burning ROM itself writes for a Dreamcast Audio/Data disc.
 *
 * LBA numbering for CUEX reuses cd_track_start_lba()/cd_track_pregap_
 * sectors(), the same accessors cdi.c already relies on for its track
 * descriptors, so NRG and CDI agree on how multi-session LBAs are counted.
 */

#include <stdlib.h>
#include <string.h>

#include "disc_image/disc_image.h"
#include "disc_image/private.h"
#include "disc_image/raw_sector.h"


#define NRG_FOOTER_SIZE 12       /* "NER5" + 8-byte big-endian trailer offset */

#define NRG_CUEX_ENTRY_SIZE 8
#define NRG_DAOX_COMMON_SIZE 22
#define NRG_DAOX_TRACK_SIZE 42

/* Per-track "mode" codes used inside DAOX records: only the high byte
 * (`mode_code`) is actually interpreted by readers, the rest is padding.
 * This is unrelated to the CUEX mode byte or the TAO ETNF/ETN2 codes.
 *
 * mode_code is *not* a bare "Mode1/Mode2/Audio" enum: 0x03 means Mode 2
 * *Form 2*, *cooked* 2336-byte sectors, while full raw 2352-byte Mode 2
 * Form 1/2 sectors (sync+header+subheader+data+EDC/ECC, exactly what
 * write_track_xa_mode2_form1() below produces via raw_sector_encode_form1(),
 * the same assembly cdi.c/mds.c use) is mode_code 0x06. Declaring 0x03 while writing
 * 2352-byte raw sectors and a sector_size of 2352 is an internally
 * inconsistent combination that real readers reject. */
#define NRG_DAOX_MODE_XA_MODE2_FORM1 0x06000001
#define NRG_DAOX_MODE_AUDIO          0x07000001

/* Table-of-contents type, a 16-bit big-endian field in the DAOX common
 * header: 0x0000 for audio, 0x2001 for Mode 2/Form 1 data. The field is a
 * full 16 bits wide -- the low byte is part of the value, not a separate
 * "close_cd" flag. Getting this wrong for the data session (0x2000 instead
 * of 0x2001) is silent on audio-only images, where the value is 0x0000
 * either way, but breaks readers on an actual data session: IsoBuster then
 * reports no filesystem found at all. */
#define NRG_TOC_MODE2 0x2001
#define NRG_TOC_AUDIO 0x0000

/* Mode byte used both by CUEX entries and as the DAOX common header's
 * toc_type-adjacent session mode (nerorip's MODE2/AUDIO) */
#define NRG_CUEX_TYPE_AUDIO 0x01
#define NRG_CUEX_TYPE_DATA  0x41

#define NRG_TRACK_LEADOUT 0xAA

/* MSF <-> LBA bias mandated by the Red Book */
#define MSF_OFFSET 150

static void put_u32be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) (v);
}

static void put_u64be(uint8_t* p, uint64_t v) {
    put_u32be(p, (uint32_t) (v >> 32));
    put_u32be(p + 4, (uint32_t) (v & 0xFFFFFFFFu));
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

/* -------------------------------------------------------------------- */
/* Track data area layout                                                */
/* -------------------------------------------------------------------- */

/* Byte offsets of a track within the NRG's track-data area (which starts
 * at file offset 0 and holds pregap + data + postgap, back-to-back, for
 * every track of every session in order -- no lead-in/lead-out padding is
 * physically stored). LBA numbering (for CUEX) is handled separately via
 * cd_track_start_lba(), the same accessor cdi.c uses. */
typedef struct {
    uint64_t pregap_offset;
    uint64_t start_offset;
    uint64_t end_offset;
} nrg_track_layout_t;

static bool compute_layout(const cd_image_t* image, nrg_track_layout_t* tracks) {
    size_t track_idx = 0;
    uint64_t offset = 0;

    for (size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        size_t n_tracks = cd_session_track_count(session);

        if (n_tracks == 0) {
            return false;
        }

        for (size_t t = 0; t < n_tracks; ++t) {
            cd_track_t* track = cd_session_get_track(session, t);

            uint64_t pregap_bytes = (uint64_t) cd_track_pregap_sectors(track) * RAW_SECTOR_SIZE;
            uint64_t data_bytes = (uint64_t) cd_track_data_size_in_sectors(track) * RAW_SECTOR_SIZE;
            uint64_t postgap_bytes = (uint64_t) cd_track_postgap_sectors(track) * RAW_SECTOR_SIZE;

            tracks[track_idx].pregap_offset = offset;
            offset += pregap_bytes;

            tracks[track_idx].start_offset = offset;
            offset += data_bytes + postgap_bytes;

            tracks[track_idx].end_offset = offset;

            ++track_idx;
        }
    }

    return true;
}

/* -------------------------------------------------------------------- */
/* Raw sector data                                                       */
/* -------------------------------------------------------------------- */

static bool write_track_cdda(const cd_track_t* track, FILE* output,
                             cd_write_progress_t* progress) {
    cd_write_silent_sectors(output, cd_track_pregap_sectors(track));

    fwrite(track->data, track->data_len, 1, output);
    cd_write_sector_tail_padding(output, track->data_len, RAW_SECTOR_SIZE);

    cd_write_silent_sectors(output, cd_track_postgap_sectors(track));

    /* One lump: audio is written whole rather than sector by sector, and it is
     * a small part of a Dreamcast image next to the data track. */
    return cd_write_progress_add(progress, cd_track_total_size_in_sectors(track)) == 0;
}

/* Empty XA Mode 2 Form 2 sectors, used to fill data-track pregaps/postgaps,
 * with a correct per-sector MSF stamp (mirrors mds.c's MDF gap filler). */
static void write_mode2_form2_gap_sectors(FILE* output, int32_t lba, size_t count) {
    static const uint8_t SYNC[12] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
    };

    /* EDC of the constant payload below; the Form 2 EDC does not cover the
     * header, so it stays valid while the MSF changes. */
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
        lba_to_msf(lba + (int32_t) i, &m, &s, &f);
        sector[0x0C] = bcd(m);
        sector[0x0D] = bcd(s);
        sector[0x0E] = bcd(f);
        fwrite(sector, sizeof(sector), 1, output);
    }
}

static bool write_track_xa_mode2_form1(const cd_track_t* track, FILE* output,
                                       cd_write_progress_t* progress) {
    uint32_t start_lba = cd_track_start_lba(track);
    int32_t pregap_lba = (int32_t) start_lba - (int32_t) cd_track_pregap_sectors(track);

    write_mode2_form2_gap_sectors(output, pregap_lba, cd_track_pregap_sectors(track));
    if (cd_write_progress_add(progress, cd_track_pregap_sectors(track))) {
        return false;
    }

    uint8_t sector[RAW_SECTOR_SIZE];

    uint32_t lba = start_lba;
    size_t bytes_written = 0;

    for (size_t i = 0; i + RAW_SECTOR_USER_DATA_SIZE <= track->data_len;
            i += RAW_SECTOR_USER_DATA_SIZE) {
        raw_sector_encode_form1(sector, track->data + i,
            RAW_SECTOR_USER_DATA_SIZE, lba + MSF_OFFSET);
        fwrite(sector, RAW_SECTOR_SIZE, 1, output);
        bytes_written += RAW_SECTOR_USER_DATA_SIZE;
        ++lba;
        if (cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    size_t remaining = track->data_len - bytes_written;
    if (remaining) {
        raw_sector_encode_form1(sector, track->data + bytes_written, remaining,
            lba + MSF_OFFSET);
        fwrite(sector, RAW_SECTOR_SIZE, 1, output);
        ++lba;
        if (cd_write_progress_add(progress, 1)) {
            return false;
        }
    }

    write_mode2_form2_gap_sectors(output, (int32_t) lba, cd_track_postgap_sectors(track));
    return cd_write_progress_add(progress, cd_track_postgap_sectors(track)) == 0;
}

/* -------------------------------------------------------------------- */
/* Trailer chunks                                                        */
/* -------------------------------------------------------------------- */

static void write_cuex_chunk(FILE* output, const cd_session_t* session, uint16_t first_track_number) {
    size_t n_tracks = cd_session_track_count(session);
    uint32_t payload_size = (uint32_t) ((n_tracks + 1) * 2 * NRG_CUEX_ENTRY_SIZE);

    fwrite("CUEX", 4, 1, output);

    uint8_t len_be[4];
    put_u32be(len_be, payload_size);
    fwrite(len_be, 4, 1, output);

    cd_track_t* first_track = cd_session_get_track(session, 0);
    uint8_t session_type = (cd_track_type(first_track) == TRACK_TYPE_AUDIO) ? NRG_CUEX_TYPE_AUDIO : NRG_CUEX_TYPE_DATA;

    uint8_t entry[NRG_CUEX_ENTRY_SIZE];

    /* Session-header entry: session mode + the session's start LBA (the
     * first track's pregap start -- e.g. 0xFFFFFF6A/-150 for session 1). */
    {
        int32_t first_start_lba = (int32_t) cd_track_start_lba(first_track);
        int32_t session_start_lba = first_start_lba - (int32_t) cd_track_pregap_sectors(first_track);

        entry[0] = session_type;
        entry[1] = 0;
        entry[2] = 0;
        entry[3] = 0;
        put_u32be(entry + 4, (uint32_t) session_start_lba);
        fwrite(entry, sizeof(entry), 1, output);
    }

    int32_t leadout_lba = 0;

    for (size_t t = 0; t < n_tracks; ++t) {
        cd_track_t* track = cd_session_get_track(session, t);

        /* Every entry -- header, per-track, and trailer alike -- carries the
         * bare session mode byte (0x01/0x41), with no extra flag bits set. */
        uint8_t type = (cd_track_type(track) == TRACK_TYPE_AUDIO) ? NRG_CUEX_TYPE_AUDIO : NRG_CUEX_TYPE_DATA;
        int32_t start_lba = (int32_t) cd_track_start_lba(track);
        int32_t pregap_lba = start_lba - (int32_t) cd_track_pregap_sectors(track);

        entry[0] = type;
        entry[1] = (uint8_t) (first_track_number + t);
        entry[2] = 0; /* index 0: pregap start */
        entry[3] = 0;
        put_u32be(entry + 4, (uint32_t) pregap_lba);
        fwrite(entry, sizeof(entry), 1, output);

        entry[2] = 1; /* index 1: track data start */
        put_u32be(entry + 4, (uint32_t) start_lba);
        fwrite(entry, sizeof(entry), 1, output);

        leadout_lba = start_lba
            + (int32_t) cd_track_data_size_in_sectors(track)
            + (int32_t) cd_track_postgap_sectors(track);
    }

    /* Session-trailer entry: session mode, fixed 0xAA/0x01 marker, and the
     * session's end (lead-out) LBA. A single entry, not an index0/1 pair. */
    entry[0] = session_type;
    entry[1] = NRG_TRACK_LEADOUT;
    entry[2] = 0x01;
    entry[3] = 0;
    put_u32be(entry + 4, (uint32_t) leadout_lba);
    fwrite(entry, sizeof(entry), 1, output);
}

static void write_daox_chunk(
        FILE* output,
        const cd_session_t* session,
        const nrg_track_layout_t* tracks,
        size_t track_offset,
        uint16_t first_track,
        uint16_t last_track) {

    size_t n_tracks = cd_session_track_count(session);
    uint32_t payload_size = (uint32_t) (NRG_DAOX_COMMON_SIZE + n_tracks * NRG_DAOX_TRACK_SIZE);

    fwrite("DAOX", 4, 1, output);

    uint8_t len_be[4];
    put_u32be(len_be, payload_size);
    fwrite(len_be, 4, 1, output);

    cd_track_t* first = cd_session_get_track(session, 0);
    uint16_t toc_type = (cd_track_type(first) == TRACK_TYPE_AUDIO) ? NRG_TOC_AUDIO : NRG_TOC_MODE2;

    uint8_t common[NRG_DAOX_COMMON_SIZE] = {0};
    put_u32be(common, payload_size); /* Repeats the chunk size; UPC (13 bytes) + 1 pad byte left zeroed */
    common[18] = (uint8_t) (toc_type >> 8);
    common[19] = (uint8_t) (toc_type & 0xFF);
    common[20] = (uint8_t) first_track;
    common[21] = (uint8_t) last_track;
    fwrite(common, sizeof(common), 1, output);

    for (size_t t = 0; t < n_tracks; ++t) {
        cd_track_t* track = cd_session_get_track(session, t);
        const nrg_track_layout_t* layout = &tracks[track_offset + t];

        uint8_t rec[NRG_DAOX_TRACK_SIZE] = {0}; /* rec[0..9]: ISRC, left zeroed */

        uint32_t mode = (cd_track_type(track) == TRACK_TYPE_AUDIO) ? NRG_DAOX_MODE_AUDIO : NRG_DAOX_MODE_XA_MODE2_FORM1;

        put_u32be(rec + 10, RAW_SECTOR_SIZE);
        put_u32be(rec + 14, mode);
        put_u64be(rec + 18, layout->pregap_offset);
        put_u64be(rec + 26, layout->start_offset);
        put_u64be(rec + 34, layout->end_offset);

        fwrite(rec, sizeof(rec), 1, output);
    }
}

static void write_sinf_chunk(FILE* output, uint32_t track_count) {
    fwrite("SINF", 4, 1, output);

    uint8_t len_be[4];
    put_u32be(len_be, 4);
    fwrite(len_be, 4, 1, output);

    uint8_t val_be[4];
    put_u32be(val_be, track_count);
    fwrite(val_be, 4, 1, output);
}

/* Media type chunk, written once for the whole image (not per session).
 * Real Nero-compatible files carry it with value 1 regardless of session
 * layout. */
static void write_mtyp_chunk(FILE* output) {
    fwrite("MTYP", 4, 1, output);

    uint8_t len_be[4];
    put_u32be(len_be, 4);
    fwrite(len_be, 4, 1, output);

    uint8_t val_be[4];
    put_u32be(val_be, 1);
    fwrite(val_be, 4, 1, output);
}

static void write_end_chunk(FILE* output) {
    fwrite("END!", 4, 1, output);

    uint8_t len_be[4] = {0, 0, 0, 0};
    fwrite(len_be, 4, 1, output);
}

static uint64_t write_nrg_trailer(const cd_image_t* image, FILE* output, const nrg_track_layout_t* tracks) {
    uint64_t trailer_offset = (uint64_t) ftell(output);

    uint16_t track_number = 1;
    size_t track_offset = 0;

    /* CUEX/DAOX pairs for every session first, then every session's SINF
     * grouped together, matching real Nero output (see the file-level
     * comment above). */
    for (size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        size_t n_tracks = cd_session_track_count(session);
        uint16_t first_track = track_number;
        uint16_t last_track = (uint16_t) (track_number + n_tracks - 1);

        write_cuex_chunk(output, session, first_track);
        write_daox_chunk(output, session, tracks, track_offset, first_track, last_track);

        track_number = (uint16_t) (track_number + n_tracks);
        track_offset += n_tracks;
    }

    for (size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        write_sinf_chunk(output, (uint32_t) cd_session_track_count(session));
    }

    write_mtyp_chunk(output);

    write_end_chunk(output);

    return trailer_offset;
}

/* -------------------------------------------------------------------- */
/* Public API                                                            */
/* -------------------------------------------------------------------- */

bool cd_write_to_nrg(const cd_image_t* image, FILE* output) {
    /* Same Red Book limits enforced by cd_write_to_cdi()/cd_write_to_mds():
     * at most 255 sessions and 99 tracks. */
    if (cd_image_session_count(image) == 0 || cd_image_session_count(image) > 255 ||
        cd_image_track_count(image) == 0 || cd_image_track_count(image) > CD_MAX_TRACKS) {
        return false;
    }

    size_t track_count = cd_image_track_count(image);
    nrg_track_layout_t* tracks = calloc(track_count, sizeof(nrg_track_layout_t));

    if (!tracks || !compute_layout(image, tracks)) {
        free(tracks);
        return false;
    }

    /* Pregaps and postgaps are stored in the file, so every sector of every
     * track counts; the trailer chunks are a few hundred bytes. */
    uint64_t total_sectors = 0;
    for (size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);
        for (size_t t = 0; t < cd_session_track_count(session); ++t) {
            total_sectors += cd_track_total_size_in_sectors(cd_session_get_track(session, t));
        }
    }

    cd_write_progress_t progress;
    cd_write_progress_init(&progress, image, total_sectors);

    for (size_t s = 0; s < cd_image_session_count(image); ++s) {
        cd_session_t* session = cd_image_get_session(image, s);

        for (size_t t = 0; t < cd_session_track_count(session); ++t) {
            cd_track_t* track = cd_session_get_track(session, t);

            switch (cd_track_mode(track)) {
                case TRACK_MODE_CDDA:
                    if (!write_track_cdda(track, output, &progress)) {
                        free(tracks);
                        return false;
                    }
                    break;
                case TRACK_MODE_XA_MODE2_FORM1:
                    if (!write_track_xa_mode2_form1(track, output, &progress)) {
                        free(tracks);
                        return false;
                    }
                    break;
                default:
                    /* Not produced by mkdcdisc */
                    free(tracks);
                    return false;
            }
        }
    }

    uint64_t trailer_offset = write_nrg_trailer(image, output, tracks);
    free(tracks);

    uint8_t footer[NRG_FOOTER_SIZE];
    /* 4 constant bytes into footer[NRG_FOOTER_SIZE].
     * nosemgrep: insecure-use-memcpy-fn */
    memcpy(footer, "NER5", 4);
    put_u64be(footer + 4, trailer_offset);
    fwrite(footer, sizeof(footer), 1, output);

    return !ferror(output);
}

cd_image_t* cd_load_from_nrg(FILE* input) {
    (void) input;
    return NULL;
}
