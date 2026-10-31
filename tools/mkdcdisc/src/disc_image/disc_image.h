/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "progress.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Usage
 *
 * cd_image_t* image = cd_new_image();
 * cd_session_t* session = cd_new_session(image);
 * cd_track_t* track = cd_new_track(session, TRACK_TYPE_AUDIO, audio_data, audio_len);
 *
 * FILE* f = fopen("myimage.mds", "wb");
 * cd_write_to_cdi(image, f);
 * fclose(f);
 **/

typedef enum {
    TRACK_TYPE_AUDIO,
    TRACK_TYPE_DATA
} track_type_t;

typedef enum {
    TRACK_MODE_CDDA,
    TRACK_MODE_MODE1,
    TRACK_MODE_MODE2,
    TRACK_MODE_XA_MODE2_FORM1,
    TRACK_MODE_XA_MODE2_FORM2,
} track_mode_t;

/* GD-ROM high-density zone start LBA: the fixed physical offset at which
 * the high-density (data) session begins on a real GD-ROM. Shared between
 * the ISO9660 build (as libisofs' ms_block) and the .gdi writer (as the
 * high-density track's LBA), which is why it lives here rather than being
 * a private constant of either. */
#define CD_GD_START_LBA 45000

/* Red Book's ceiling, which every format here inherits: a track number is a
 * two-digit BCD field in the table of contents, so a disc holds tracks 1 to 99
 * and no more. A GD-ROM keeps that numbering, spending 2 on the low-density
 * area and leaving 97 for the high-density one.
 *
 * Every writer refuses an image carrying more, which is a backstop rather than
 * the diagnostic: by then the disc has been built and there is nothing to say
 * but that writing failed. main() is where a track count that cannot work is
 * turned down, with a message. */
#define CD_MAX_TRACKS 99

struct _cd_session_t;
struct _cd_track_t;
struct _cd_image_t;

typedef struct _cd_session_t cd_session_t;
typedef struct _cd_track_t cd_track_t;
typedef struct _cd_image_t cd_image_t;

cd_image_t* cd_new_image();
void cd_free_image(cd_image_t** img);

size_t cd_image_session_count(const cd_image_t* img);
size_t cd_image_track_count(const cd_image_t* img);
size_t cd_image_length_in_sectors(const cd_image_t* img);

cd_session_t* cd_image_get_session(const cd_image_t* img, size_t idx);

void cd_image_set_volume_name(cd_image_t* img, const char* name);
const char* cd_image_volume_name(const cd_image_t* img);

/* Attaches a progress callback, reported to in sectors while the image is
 * being written out. Set on the image rather than passed to each
 * cd_write_to_*() because it is the same answer for all four formats, and
 * because writing an image is the only thing here that takes long enough to
 * report on.
 *
 * The image only borrows it: it must outlive every write. NULL (the default)
 * reports nothing. */
void cd_image_set_progress(cd_image_t* img, const progress_t* progress);
const progress_t* cd_image_progress(const cd_image_t* img);

cd_session_t* cd_new_session(cd_image_t* img);
size_t cd_session_track_count(const cd_session_t* session);
cd_track_t* cd_session_get_track(const cd_session_t* session, size_t idx);

size_t cd_track_pregap_sectors(const cd_track_t* track);
bool cd_track_set_pregap_sectors(cd_track_t* track, size_t pregap);
size_t cd_track_postgap_sectors(const cd_track_t* track);
bool cd_track_set_postgap_sectors(cd_track_t* track, size_t pregap);

/* Session length including lead in and lead out */
size_t cd_session_length_in_sectors(const cd_session_t* session);

/* Total sectors used to store data, including pregap */
size_t cd_session_data_length_in_sectors(const cd_session_t* session);

/* `data` is copied into the track. Pass NULL for a zero-filled track of
 * `data_len` bytes, which is what cd_new_track_blank() does: it avoids
 * having to materialise the zeros anywhere first. */
cd_track_t* cd_new_track(
    cd_session_t* session,
    track_type_t type,
    const uint8_t* data,
    const uint32_t data_len
);

/* As cd_new_track(), but the track points at the caller's bytes instead of
 * taking a copy of them, and does not free them.
 *
 * For the one track that makes the difference: a GD-ROM's high-density data
 * track is most of a gigabyte, and copying it means holding it twice, once in
 * the ISO the builder assembled and once here, for as long as it takes to
 * write the image out. The writers only ever read a track's bytes -- every
 * one of them either fwrite()s the buffer or feeds it to
 * raw_sector_encode_form1() as a source -- so there is nothing for the copy to
 * protect.
 *
 * The obligation is the caller's, and it is absolute: `data` must stay put,
 * unmoved and unfreed, until cd_free_image() has released the image. A
 * std::vector that outlives the write qualifies; one that might be resized in
 * between does not, since that moves its buffer. Anything shorter-lived --
 * a local assembled on the way past, say -- must use cd_new_track() and be
 * copied. NULL is not accepted here; cd_new_track_blank() covers that. */
cd_track_t* cd_new_track_borrowed(
    cd_session_t* session,
    track_type_t type,
    const uint8_t* data,
    const uint32_t data_len
);

cd_track_t* cd_new_track_blank(
    cd_session_t* session,
    track_type_t type,
    const uint32_t data_len
);

track_type_t cd_track_type(const cd_track_t* track);
uint8_t* cd_track_data(const cd_track_t* track);

size_t cd_track_data_size_in_bytes(const cd_track_t* track);
size_t cd_track_data_size_in_sectors(const cd_track_t* track);

size_t cd_track_total_size_in_sectors(const cd_track_t* track);

track_mode_t cd_track_mode(const cd_track_t* track);
void cd_track_set_mode(cd_track_t* track, track_mode_t mode);




/* Where a track sits, derived from the session layout: every preceding
 * session's length, then every preceding track's data plus its pregap and
 * postgap. This is what the CDI and NRG writers record. */
uint32_t cd_track_start_lba(const cd_track_t* track);

/* Overrides that derivation with a caller-chosen LBA; 0 means "no override".
 *
 * Only the GD-ROM writer honours it, because only a GD-ROM needs positions
 * that one pregap-per-track walk cannot express: its GD-DA tracks sit flush
 * against one another with no pregap, while the data/audio boundaries around
 * them keep the usual 150 sectors. Zero is safe as the "unset" marker since
 * only the first track of an image can legitimately start at LBA 0, and the
 * derivation already places it there. */
void cd_track_set_placement_lba(cd_track_t* track, uint32_t lba);
uint32_t cd_track_placement_lba(const cd_track_t* track);

bool cd_write_to_cdi(const cd_image_t* image, FILE* output, const char* filename);
bool cd_write_to_mds(const cd_image_t* image, FILE* mds, FILE* mdf);
bool cd_write_to_nrg(const cd_image_t* image, FILE* output);

/* Sector format of a GD-ROM image's data tracks, passed to
 * cd_write_to_gdi(). Raw is what real GD-ROM dumps use and is mkdcdisc's
 * default; the cooked form holds plain user data with no sector framing. */
#define CD_GDI_SECTOR_SIZE_RAW    2352 /* Mode 1: sync+header+data+EDC/ECC */
#define CD_GDI_SECTOR_SIZE_COOKED 2048 /* user data only */

/* Writes a GD-ROM disc image: the .gdi text descriptor to gdi_file, and the
 * track files into track_dir (no trailing slash required; "" and "." both
 * mean "current directory"). data_sector_size must be one of the two
 * CD_GDI_SECTOR_SIZE_* values above and decides both the framing and the
 * track filenames: track??.bin for raw data, track??.iso for cooked data,
 * track??.raw for CD-DA (always 2352-byte PCM). */
bool cd_write_to_gdi(const cd_image_t* image, FILE* gdi_file, const char* track_dir,
                     uint32_t data_sector_size);

cd_image_t* cd_load_from_cdi(FILE* input);
cd_image_t* cd_load_from_mds(FILE* input);
cd_image_t* cd_load_from_nrg(FILE* input);
cd_image_t* cd_load_from_gdi(FILE* input);

#ifdef __cplusplus
}
#endif
