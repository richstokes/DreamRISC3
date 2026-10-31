/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* libisofs image construction: see iso_builder.hpp. */

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <tuple>

#include "cli.hpp"
#include "input_files.hpp"
#include "iso_builder.hpp"
#include "logging.hpp"
#include "terminal.hpp"
#include "version.h"

/* See volume_label(), the one thing that calls this. */
static std::string sanitize_volume_label(std::string s) {
    for(auto& c: s) {
        unsigned char uc = static_cast<unsigned char>(c);
        c = (std::isalnum(uc) || c == '_') ? std::toupper(uc) : '_';
    }

    if(s.size() > 32) {
        std::cerr << term::warning << "volume label is " << s.size() << " characters, exceeds the "
            "32-character ISO9660 limit; it will be truncated." << std::endl;
        s.resize(32);
    }

    return s;
}

std::string volume_label() {
    static const std::string resolved = sanitize_volume_label(
        OPTS.count("volume-label") ? OPTS["volume-label"][0] : ipbin_title());

    return resolved;
}

std::string mkdcdisc_preparer_id() {
    return std::string("mkdcdisc v") + MKDCDISC_VERSION;
}

/* Replaces every timestamp libisofs would take from the clock with the one
 * SOURCE_DATE_EPOCH names, so that the same inputs produce the same bytes.
 * Does nothing when it is unset, which is the normal case.
 *
 * The volume timestamps go through vol_uuid rather than through the time_t
 * parameters beside it: libisofs converts those with the local timezone,
 * while the uuid string is written as given and recorded as GMT, which is the
 * difference between an image that reproduces on one machine and one that
 * reproduces anywhere. The per-node timestamps have no such escape --
 * iso_write_opts_set_default_timestamp() takes a time_t and libisofs encodes
 * it in local time with the offset beside it -- so a build that has to match
 * another machine's byte for byte needs a fixed TZ as well. See
 * docs/cli-reference.md. */
static void apply_source_date_epoch(IsoWriteOpts* opts) {
    auto epoch = source_date_epoch();
    if(!epoch) {
        return;
    }

    /* "YYYYMMDDhhmmsscc", the 16 decimal digits libisofs copies out of the
     * uuid, in UTC. gmtime() rather than localtime() for the same reason the
     * uuid is used at all. */
    char uuid[17] = {0};
    std::tm utc{};

#ifdef _WIN32
    gmtime_s(&utc, &*epoch);
#else
    gmtime_r(&*epoch, &utc);
#endif

    std::strftime(uuid, sizeof(uuid), "%Y%m%d%H%M%S", &utc);
    uuid[14] = '0';  /* centiseconds, which no epoch in seconds can express */
    uuid[15] = '0';

    iso_write_opts_set_pvd_times(opts, 0, 0, 0, 0, uuid);

    /* 2 means "override every node's mtime with the default below", which is
     * what reaches the directory records and the Rock Ridge TF entries. */
    iso_write_opts_set_replace_timestamps(opts, 2);
    iso_write_opts_set_default_timestamp(opts, *epoch);
}

IsoWriteOpts* create_iso_write_opts(const IPBin& ip_bin, size_t ms_block, bool joliet) {
    IsoWriteOpts* opts = NULL;
    iso_write_opts_new(&opts, 0);
    iso_write_opts_set_joliet(opts, joliet ? 1 : 0);
    iso_write_opts_set_rockridge(opts, 1);
    iso_write_opts_set_system_area(opts, (char*) &ip_bin, 0, 0);
    iso_write_opts_set_ms_block(opts, ms_block);
    /* mkisofs' -full-iso9660-filenames: up to 31 characters in the primary
     * ISO9660 tree instead of strict 8.3. Unlike Joliet/Rock Ridge (which
     * only matter for desktop browsing), this is the tree the Dreamcast's
     * own BIOS/GD-ROM driver actually reads. */
    iso_write_opts_set_iso_level(opts, 2);
    /* By default libisofs force-appends a "." to any file name that has no
     * extension (e.g. "README" -> "README."), per strict ECMA-119. Disable
     * that on both the ECMA-119 and Joliet trees (bit0|bit1) so extension-
     * less files round-trip with their original name. */
    iso_write_opts_set_no_force_dots(opts, 1 | 2);

    apply_source_date_epoch(opts);

    return opts;
}

/* Chunked because a filler sized to whatever is left of the disc would
 * otherwise mean holding the better part of a gigabyte in memory. */
void write_zero_file(const std::filesystem::path& path, std::uintmax_t bytes,
                     const progress_t* progress) {
    std::ofstream f(path, std::ios::binary);
    std::vector<char> chunk(64 * 1024, 0);

    for(std::uintmax_t written = 0; written < bytes;) {
        std::uintmax_t n = std::min<std::uintmax_t>(chunk.size(), bytes - written);
        f.write(chunk.data(), (std::streamsize) n);
        written += n;

        if(progress_report(progress, written, bytes)) {
            break;
        }
    }
}

/* Disposes of a burn source, whether it was drained, abandoned part-way, or
 * never read at all.
 *
 * Letting go of the pointer would not do: libisofs writes from a thread of its
 * own, which holds each file of the disc contents open as it reaches it, and
 * which stays parked on a full ring buffer for as long as nobody empties it.
 * bs_cancel() closes the buffer under that thread and joins it, so once this
 * returns nothing in the staging directory is open any more -- which is the
 * whole point, since that directory is about to be deleted. bs_free_data()
 * then releases the ECMA-119 layout the source was emitting, the reference
 * iso_image_create_burn_source() took on the caller's behalf.
 *
 * Both are safe on a source that already reached EOF: bs_cancel() recognizes
 * an image that "was already consumed", and both join the writer thread only
 * while it is still running. */
static void release_burn_source(struct burn_source* burn_src) {
    if(burn_src->version >= 1 && burn_src->cancel) {
        burn_src->cancel(burn_src);
    }

    if(burn_src->free_data) {
        burn_src->free_data(burn_src);
    }

    free(burn_src);
}

/* Exact rather than an estimate: libisofs lays the whole image out before
 * emitting a byte of it -- it has to, since the ECMA-119 records carry
 * absolute addresses -- so the final size is already known once the source
 * exists. */
static uint64_t burn_source_size(struct burn_source* burn_src) {
    return burn_src->get_size ? (uint64_t) burn_src->get_size(burn_src) : 0;
}

uint64_t measure_burn_source(struct burn_source* burn_src) {
    uint64_t total = burn_source_size(burn_src);
    release_burn_source(burn_src);
    return total;
}

std::vector<uint8_t> read_burn_source(struct burn_source* burn_src, const progress_t* progress,
                                      uint64_t reserve_at_least) {
    uint64_t total = burn_source_size(burn_src);

    std::vector<uint8_t> data;

    /* Claimed in one go rather than grown into. This vector is the largest
     * thing mkdcdisc allocates -- a padded high-density track is most of a
     * gigabyte -- and a vector that doubles its way there holds the old block
     * and the new one at the same time, so its peak is half again the image it
     * ends up with. The size above is exact, so there is nothing to guess at
     * and no reallocation left to survive.
     *
     * Exact is not the same as enough, though: a caller that pads the image
     * afterwards would find a vector with no slack left reallocating for the
     * few hundred kilobytes it adds, and doubling a gigabyte to get them. Such
     * a caller says up front how far it will grow. */
    data.reserve((size_t) std::max(total, reserve_at_least));

    unsigned char buf[2048];
    for(;;) {
        int read = burn_src->read_xt(burn_src, buf, sizeof(buf));

        if(read == 0) { /* EOF: the whole image has been handed over */
            break;
        }

        /* bs_read() reports a failed ring-buffer read as -1, which is every
         * bit as non-zero as a good read: as a length it would ask the vector
         * for SIZE_MAX bytes, and as a loop condition it would never end.
         * There is no partial image worth keeping -- a short data track is a
         * silently corrupt disc -- so give up here and let main() print it. */
        if(read < 0) {
            release_burn_source(burn_src);
            throw std::runtime_error("libisofs could not read the disc contents through to the "
                                     "end of the image");
        }

        data.insert(data.end(), buf, buf + read);

        if(progress_report(progress, data.size(), total)) {
            break;
        }
    }

    release_burn_source(burn_src);
    return data;
}

const char* const PVD_COPYRIGHT_FILE = "COPYRIGH.TXT";
const char* const PVD_ABSTRACT_FILE = "ABSTRACT.TXT";
const char* const PVD_BIBLIO_FILE = "BIBLIOGR.TXT";

/* The stream takes ownership of the buffer and frees it when the image is
 * released. */
bool add_iso_file_from_memory(IsoDir* parent, const char* name, const std::string& content) {
    unsigned char* buf = (unsigned char*) malloc(content.size() ? content.size() : 1);
    if(!buf) {
        return false;
    }
    std::memcpy(buf, content.data(), content.size());

    IsoStream* stream = NULL;
    if(iso_memory_stream_new(buf, content.size(), &stream) < 0) {
        free(buf);
        return false;
    }

    IsoFile* file = NULL;
    if(iso_tree_add_new_file(parent, name, stream, &file) < 0) {
        iso_stream_unref(stream);
        return false;
    }

    return true;
}

/* --copyright/--abstract/--biblio each supply one of them as a host file.
 * `generate_defaults` is set for GD-ROM builds, which want all three present
 * the way pressed discs have them, and clear for CDI/MDS/NRG -- otherwise
 * every existing CDI build would silently gain three new files. The
 * --gdi-disable-* flags opt back out of one of the three, for the builds that
 * want their low-density area to carry only what they put there. */
VolumeTextFiles add_volume_text_files(IsoDir* root, const IPBin& ip_bin,
                                             bool generate_defaults) {
    std::string title = trim_ipbin_field(ip_bin.meta.game_name, sizeof(ip_bin.meta.game_name));
    std::string author = trim_ipbin_field(ip_bin.meta.company_name, sizeof(ip_bin.meta.company_name));
    std::string date = trim_ipbin_field(ip_bin.meta.release_date, sizeof(ip_bin.meta.release_date));
    std::string year = date.size() >= 4 ? date.substr(0, 4) : date;

    VolumeTextFiles added;

    /* The generated defaults are CRLF-terminated single lines, the shape
     * every reference dump uses -- Namco's COPYRIGH.TXT reads "Namco Museum
     * - TM & Copyright (c) 2000 Namco Ltd. All Rights Reserved". */
    const std::tuple<const char*, const char*, const char*, std::string, bool*> wanted[] = {
        {"copyright", "gdi-disable-copyright", PVD_COPYRIGHT_FILE,
            title + " - Copyright (c) " + year + " " + author + ". All Rights Reserved\r\n",
            &added.copyright},
        {"abstract", "gdi-disable-abstract", PVD_ABSTRACT_FILE,
            title + "\r\n",
            &added.abstract},
        {"biblio", "gdi-disable-biblio", PVD_BIBLIO_FILE,
            title + " - Created with " + mkdcdisc_preparer_id() + "\r\n",
            &added.biblio},
    };

    for(const auto& [option, disable_option, filename, generated, flag]: wanted) {
        /* Checked before the option that supplies the file, not after: the
         * flag disables the whole mechanism, so a --copyright given alongside
         * it loses (main() says so), and the PVD field stays empty even if the
         * disc contents happen to provide a file of that name. */
        if(OPTS.count(disable_option)) {
            continue;
        }

        std::optional<std::string> content;

        if(OPTS.count(option)) {
            /* Readability was checked during option validation. */
            content = read_file_contents(OPTS[option][0]);
        }

        if(!content && generate_defaults) {
            content = generated;
        }

        if(!content) {
            continue;
        }

        if(add_iso_file_from_memory(root, filename, *content)) {
            *flag = true;
            continue;
        }

        /* Most likely the disc contents already supply that name. The file is
         * on the volume either way, so the identifier field stays accurate --
         * but say which copy won, since it isn't the one just asked for. */
        IsoNode* existing = NULL;
        if(iso_dir_get_node(root, filename, &existing) == 1) {
            *flag = true;
            if(logging::enabled(logging::Level::Normal)) {
                std::cerr << term::warning << "the disc contents already provide " << filename
                          << "; keeping that copy" << (OPTS.count(option) ? " over --" : "")
                          << (OPTS.count(option) ? option : "") << "." << std::endl;
            }
        }
    }

    return added;
}

/* The d-character filter that mangles COPYRIGH.TXT is ecma119.c's
 * "str2d_char(..., image->copyright_file_id, ...)", and the nearest thing to
 * a relaxation switch, iso_write_opts_set_relaxed_vol_atts(), only covers the
 * volume and volume set identifiers.
 *
 * Patching the bytes rather than the vendored libisofs is deliberate: that
 * copy is only compiled for MinGW-w64, while Linux and macOS link the system
 * libisofs (see meson.build), so a local fix would not travel. */
void patch_pvd_file_ids(std::vector<uint8_t>& iso, const VolumeTextFiles& present) {
    const size_t PVD = 16 * 2048; /* the 32K system area precedes it */
    const size_t FIELD_LEN = 37;

    const std::tuple<size_t, const char*, bool> fields[] = {
        {702, PVD_COPYRIGHT_FILE, present.copyright},
        {739, PVD_ABSTRACT_FILE, present.abstract},
        {776, PVD_BIBLIO_FILE, present.biblio},
    };

    if(iso.size() < PVD + 776 + FIELD_LEN) {
        return;
    }
    if(std::memcmp(&iso[PVD + 1], "CD001", 5) != 0) {
        return;
    }

    /* A field is only meaningful when the file it names is actually on the
     * volume, so an absent file leaves it blank rather than dangling. */
    for(const auto& [offset, name, enabled]: fields) {
        if(!enabled) {
            continue;
        }

        uint8_t* field = &iso[PVD + offset];
        std::memset(field, ' ', FIELD_LEN);
        std::memcpy(field, name, std::min(std::strlen(name), FIELD_LEN));
    }
}

/* A both-endian 32-bit sector count at offset 80. On all 17 dumps checked the
 * field equals track 1 plus its 150-sector pregap plus track 2 (18 Wheeler
 * 450+150+526 = 1126, Namco 606+150+526 = 1282). Patched here for the same
 * reason as the file identifiers -- there is no libisofs setter for it. */
void patch_pvd_volume_space(std::vector<uint8_t>& iso, uint32_t sectors) {
    const size_t PVD = 16 * 2048;

    if(iso.size() < PVD + 88) {
        return;
    }
    if(std::memcmp(&iso[PVD + 1], "CD001", 5) != 0) {
        return;
    }

    uint8_t* field = &iso[PVD + 80];
    for(int i = 0; i < 4; ++i) {
        field[i] = (uint8_t) ((sectors >> (8 * i)) & 0xFF);       /* little-endian */
        field[7 - i] = (uint8_t) ((sectors >> (8 * i)) & 0xFF);   /* big-endian */
    }
}

/* Shared by the CDI/MDS/NRG data track and by both GD-ROM tracks, which
 * differ only in what they then put in the tree and how they are written
 * out. */
IsoImageRef new_iso_image(const IPBin& ip_bin, const std::string& volume_label) {
    IsoImage* iso = NULL;
    iso_image_new(volume_label.c_str(), &iso);

    iso_image_set_system_id(iso,
        trim_ipbin_field(ip_bin.meta.hardware_id, sizeof(ip_bin.meta.hardware_id)).c_str());
    iso_image_set_publisher_id(iso,
        trim_ipbin_field(ip_bin.meta.company_name, sizeof(ip_bin.meta.company_name)).c_str());
    iso_image_set_data_preparer_id(iso, mkdcdisc_preparer_id().c_str());
    iso_image_set_application_id(iso,
        trim_ipbin_field(ip_bin.meta.game_name, sizeof(ip_bin.meta.game_name)).c_str());

    iso_tree_set_follow_symlinks(iso, 0);
    iso_tree_set_ignore_hidden(iso, 0);
    iso_tree_set_ignore_special(iso, 0);

    return IsoImageRef(iso);
}
