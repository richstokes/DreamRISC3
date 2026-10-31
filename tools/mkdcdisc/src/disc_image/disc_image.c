/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#include <stdlib.h>
#include <string.h>

#include "disc_image.h"
#include "private.h"
#include "raw_sector.h"

/* strncpy leaves the destination unterminated when the source fills it, so the
 * copy stops one byte short and the terminator is written by hand. Callers pass
 * a filename stem, which a filesystem will happily let reach 255 bytes. */
static void set_volume_name(cd_image_t* img, const char* name) {
    /* nosemgrep: insecure-use-string-copy-fn -- terminated on the next line */
    strncpy(img->volume_name, name, sizeof(img->volume_name) - 1);
    img->volume_name[sizeof(img->volume_name) - 1] = '\0';
}

cd_image_t* cd_new_image() {
    cd_image_t* img = (cd_image_t*) malloc(sizeof(cd_image_t));
    img->first_session = NULL;
    img->progress = NULL;
    set_volume_name(img, "UNNAMED");
    return img;
}

void cd_image_set_progress(cd_image_t* img, const progress_t* progress) {
    img->progress = progress;
}

const progress_t* cd_image_progress(const cd_image_t* img) {
    return img->progress;
}

void cd_image_set_volume_name(cd_image_t* img, const char* name) {
    set_volume_name(img, name);
}

const char* cd_image_volume_name(const cd_image_t* img) {
    return img->volume_name;
}

static void destroy_track(cd_track_t** t) {
    /* Borrowed bytes belong to the caller, who is still using them: see
     * cd_new_track_borrowed(). */
    if((*t)->owns_data) {
        free((*t)->data);
    }

    free(*t);
    t = NULL;
}

static void destroy_session(cd_session_t** s) {
    free(*s);
    s = NULL;
}

void cd_free_image(cd_image_t** img) {
    cd_session_t* s = (*img)->first_session;
    while(s) {
        cd_track_t* t = s->first_track;
        s->first_track = NULL;

        while(t) {
            cd_track_t* nt = t->next_track;
            destroy_track(&t);
            t = nt;
        }

        cd_session_t* ns = s->next_session;
        destroy_session(&s);
        s = ns;
    }

    free(*img);
    *img = NULL;
}

size_t cd_image_session_count(const cd_image_t* img) {
    size_t c = 0;
    cd_session_t* s = img->first_session;

    while(s) {
        c++;
        s = s->next_session;
    }

    return c;
}

size_t cd_image_length_in_sectors(const cd_image_t* img) {
    cd_session_t* s = img->first_session;
    size_t total = 0;
    while(s) {
        total += cd_session_length_in_sectors(s);
        s = s->next_session;
    }

    return total;
}

size_t cd_session_length_in_sectors(const cd_session_t* session) {
    bool is_first_session = session->image->first_session == session;
    const size_t LEAD_IN_SIZE = 4500;
    const size_t LEAD_OUT_SIZE = (is_first_session) ? 6750 : 2250;

    size_t total = LEAD_IN_SIZE + LEAD_OUT_SIZE;

    cd_track_t* t = session->first_track;
    while(t) {
        size_t track_sectors = cd_track_data_size_in_sectors(t);

        total += t->pregap_sectors;
        total += track_sectors;
        total += t->postgap_sectors;

        t = t->next_track;
    }

    return total;
}

size_t cd_session_data_length_in_sectors(const cd_session_t* session) {
    size_t total = 0;

    cd_track_t* t = session->first_track;
    while(t) {
        size_t track_sectors = cd_track_data_size_in_sectors(t);
        total += t->pregap_sectors;
        total += track_sectors;
        total += t->postgap_sectors;
        t = t->next_track;
    }

    return total;
}

cd_session_t* cd_image_get_session(const cd_image_t* img, size_t idx) {
    cd_session_t* found = img->first_session;

    while(idx > 0) {
        if(found) {
            found = found->next_session;
        }

        --idx;
    }

    return found;
}

size_t cd_image_track_count(const cd_image_t* img) {
    size_t c = 0;
    cd_session_t* s = img->first_session;

    while(s) {
        c += cd_session_track_count(s);
        s = s->next_session;
    }

    return c;
}

cd_session_t* cd_new_session(cd_image_t* img) {
    cd_session_t* s = (cd_session_t*) malloc(sizeof(cd_session_t));

    s->image = img;
    s->next_session = NULL;
    s->first_track = NULL;

    cd_session_t* it = img->first_session;
    if(!it) {
        img->first_session = s;
    } else {
        while(it->next_session) {
            it = it->next_session;
        }

        it->next_session = s;
    }
    return s;
}

size_t cd_session_track_count(const cd_session_t* session) {
    cd_track_t* t = session->first_track;
    size_t c = 0;
    while(t) {
        ++c;
        t = t->next_track;
    }
    return c;
}

cd_track_t* cd_session_get_track(const cd_session_t* session, size_t idx) {
    cd_track_t* found = session->first_track;

    while(idx > 0) {
        if(found) {
            found = found->next_track;
        }

        --idx;
    }

    return found;
}

/* The shared body of cd_new_track() and cd_new_track_borrowed(): everything
 * about a track except where its bytes come from. */
static cd_track_t* new_track(cd_session_t* session, track_type_t type, uint32_t data_len) {
    cd_track_t* t = (cd_track_t*) malloc(sizeof(cd_track_t));
    memset(t, 0, sizeof(cd_track_t));

    t->session = session;
    t->type = type;
    t->mode = (type == TRACK_TYPE_AUDIO) ? TRACK_MODE_CDDA : TRACK_MODE_XA_MODE2_FORM1;
    t->data_len = data_len;
    t->placement_lba = 0;
    t->pregap_sectors = 150;
    t->postgap_sectors = 0;

    cd_track_t* i = session->first_track;
    if(!i) {
        session->first_track = t;
    } else {
        while(i->next_track) {
            i = i->next_track;
        }

        i->next_track = t;
    }
    return t;
}

cd_track_t* cd_new_track(cd_session_t* session, track_type_t type, const uint8_t* data, const uint32_t data_len) {
    cd_track_t* t = new_track(session, type, data_len);

    t->data = malloc(data_len);
    t->owns_data = true;

    if(data) {
        /* Destination is the malloc(data_len) above, same length.
         * nosemgrep: insecure-use-memcpy-fn */
        memcpy(t->data, data, data_len);
    } else {
        memset(t->data, 0, data_len);
    }

    return t;
}

cd_track_t* cd_new_track_borrowed(cd_session_t* session, track_type_t type, const uint8_t* data, const uint32_t data_len) {
    cd_track_t* t = new_track(session, type, data_len);

    /* Cast away const because cd_track_data() has always handed out a mutable
     * pointer and the writers take it as one. None of them writes through it
     * -- see the header -- so the constness the caller had is not actually
     * being discarded, only the type that recorded it. */
    t->data = (uint8_t*) data;
    t->owns_data = false;

    return t;
}

cd_track_t* cd_new_track_blank(cd_session_t* session, track_type_t type, const uint32_t data_len) {
    /* Zero-filled straight into the track's own buffer. This used to stage the
     * data through a variable-length array, which put the whole track on the
     * stack: a 526-sector audio track is 1.2 MB, close enough to a typical
     * stack limit to be a hazard, and needlessly copied twice. */
    return cd_new_track(session, type, NULL, data_len);
}

track_type_t cd_track_type(const cd_track_t* track) {
    return track->type;
}

uint8_t* cd_track_data(const cd_track_t* track) {
    return track->data;
}

size_t cd_track_data_size_in_bytes(const cd_track_t* track) {
    return track->data_len;
}

size_t cd_track_total_size_in_sectors(const cd_track_t* track) {
    return track->pregap_sectors + track->postgap_sectors + cd_track_data_size_in_sectors(track);
}

track_mode_t cd_track_mode(const cd_track_t* track) {
    return track->mode;
}

void cd_track_set_mode(cd_track_t* track, track_mode_t mode) {
    track->mode = mode;
}

void cd_track_set_placement_lba(cd_track_t* track, uint32_t lba) {
    track->placement_lba = lba;
}

uint32_t cd_track_placement_lba(const cd_track_t* track) {
    return track->placement_lba;
}

uint32_t cd_track_start_lba(const cd_track_t* track) {
    cd_image_t* img = track->session->image;
    cd_session_t* s = img->first_session;

    size_t lba = 0;

    while(s && s != track->session) {
        lba += cd_session_length_in_sectors(s);
        s = s->next_session;
    }

    cd_track_t* t = track->session->first_track;
    while(t && t != track) {
        lba += cd_track_data_size_in_sectors(t) + t->pregap_sectors + t->postgap_sectors;
        t = t->next_track;
    }

    return lba;
}

size_t cd_track_pregap_sectors(const cd_track_t* track) {
    return track->pregap_sectors;
}

size_t cd_track_postgap_sectors(const cd_track_t* track) {
    return track->postgap_sectors;
}

size_t cd_track_data_size_in_sectors(const cd_track_t* track) {
    track_mode_t mode = cd_track_mode(track);
    size_t sector_user_data = (mode == TRACK_MODE_CDDA) ? 2352 :
        (mode == TRACK_MODE_MODE1) ? 2048 :
        (mode == TRACK_MODE_MODE2) ? 2336 :
        (mode == TRACK_MODE_XA_MODE2_FORM1) ? 2048 : 2324;

    size_t track_sectors = track->data_len / sector_user_data;
    if(track->data_len % sector_user_data) {
        track_sectors++;
    }

    return track_sectors;
}

bool cd_track_set_pregap_sectors(cd_track_t* track, size_t pregap) {
    track->pregap_sectors = pregap;
    return true;
}

bool cd_track_set_postgap_sectors(cd_track_t* track, size_t postgap) {
    track->postgap_sectors = postgap;
    return true;
}

/* One sector of silence, which is both what a gap sector holds and the most
 * padding a partial sector can ever need. Shared by the two helpers below so
 * that neither puts 2352 bytes on the stack per call. */
static const uint8_t ZERO_SECTOR[RAW_SECTOR_SIZE] = {0};

/* See private.h for both of these. */
void cd_write_sector_tail_padding(FILE* f, size_t data_len, size_t sector_size) {
    size_t remainder = data_len % sector_size;
    if(remainder) {
        fwrite(ZERO_SECTOR, 1, sector_size - remainder, f);
    }
}

void cd_write_silent_sectors(FILE* f, size_t count) {
    for(size_t i = 0; i < count; ++i) {
        fwrite(ZERO_SECTOR, 1, sizeof(ZERO_SECTOR), f);
    }
}
