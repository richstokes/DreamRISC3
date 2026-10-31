/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* The libisofs side of building a data track: creating an image with the
 * right Primary Volume Descriptor, draining it to bytes, and the few PVD
 * fields libisofs will not write the way a Dreamcast disc needs. */

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#define LIBISOFS_WITHOUT_LIBBURN yes
#include <libisofs.h>

#include "ip_boot.hpp"
#include "progress.h"

/* The three files an ISO9660 volume can reference from its PVD. Every one of
 * the 18 reference GD-ROM dumps sets all three fields in its low-density
 * area, and 16 use exactly these 8.3 names -- note COPYRIGH.TXT, truncated,
 * not COPYRIGHT.TXT.
 *
 * mkdcdisc always writes them under these names rather than reusing the
 * basename of whatever --copyright/--abstract/--biblio points at: libisofs
 * transliterates names to 8.3 as it writes, so a supplied name could end up
 * mangled and no longer match the identifier field, which must name the file
 * exactly. A fixed name cannot drift. */
extern const char* const PVD_COPYRIGHT_FILE;
extern const char* const PVD_ABSTRACT_FILE;
extern const char* const PVD_BIBLIO_FILE;

/* Which of the three a given volume actually carries. GD-ROM builds carry all
 * three; the other formats only get the ones asked for, so a plain CDI build
 * keeps producing exactly the files it always did. */
struct VolumeTextFiles {
    bool copyright = false;
    bool abstract = false;
    bool biblio = false;

    bool any() const { return copyright || abstract || biblio; }
};

/* The label the ISO9660 volume will carry: -V/--volume-label when given,
 * otherwise the IP.BIN title. Sanitized either way -- uppercased, everything
 * outside [A-Z0-9_] replaced with '_', and truncated to the 32 characters
 * ISO9660 allows, which is warned about.
 *
 * Resolved on the first call and remembered, so that warning is printed once
 * however many times the answer is asked for: the -v 3 build plan reports the
 * label before the volume carrying it is built, and a GD-ROM builds two
 * volumes. */
std::string volume_label();

/* The ISO9660 Data Preparer Identifier: always mkdcdisc itself, since no
 * IP.BIN field corresponds to it. */
std::string mkdcdisc_preparer_id();

/* Owns the reference iso_image_new() hands out, so that every way out of a
 * builder releases it -- including the early returns that a bare unref at the
 * end of a function would skip, and the exception read_burn_source() can now
 * throw.
 *
 * Letting it expire does not pull the image out from under a burn source built
 * on it: iso_image_create_burn_source() takes a reference of its own and the
 * image lives until that source is released. So a builder is free to let this
 * go as soon as it has handed the tree over -- which is what makes a scope
 * guard the right shape here, rather than an unref placed by hand after the
 * last use. */
class IsoImageRef {
public:
    explicit IsoImageRef(IsoImage* image): image_(image) {}
    ~IsoImageRef() { if(image_) iso_image_unref(image_); }

    /* Neither copied nor moved: new_iso_image() builds it in place and it
     * stays there. */
    IsoImageRef(const IsoImageRef&) = delete;
    IsoImageRef& operator=(const IsoImageRef&) = delete;

    IsoImage* get() const { return image_; }

private:
    IsoImage* image_;
};

/* An image carrying the PVD identifiers mkdcdisc always derives from the
 * IP.BIN in use, ready for callers to populate and write. */
IsoImageRef new_iso_image(const IPBin& ip_bin, const std::string& volume_label);

/* The write options shared by every ISO9660 generation pass. `joliet` is on
 * for the CDI/MDS/NRG paths; build_gd() turns it off to match commercial
 * GD-ROM images. */
IsoWriteOpts* create_iso_write_opts(const IPBin& ip_bin, size_t ms_block, bool joliet = true);

/* Fully drains a burn_source into a byte vector and frees it. Reports in
 * bytes against the image size libisofs computed before emitting any of it.
 * Throws if libisofs cannot read the image through to its end, rather than
 * returning the truncated part of it.
 *
 * The vector is sized to hold the whole image before a byte of it arrives, so
 * that the largest allocation mkdcdisc makes is made once. `reserve_at_least`
 * is for callers that will grow it afterwards -- padding a filesystem out to
 * its track length, say: asking for the eventual size here means that growth
 * costs nothing, where letting it reallocate would briefly hold two copies of
 * the better part of a gigabyte. */
std::vector<uint8_t> read_burn_source(struct burn_source* burn_src,
                                      const progress_t* progress = nullptr,
                                      uint64_t reserve_at_least = 0);

/* How large the image a burn_source is about to emit will be, without emitting
 * it, then frees the source. libisofs settles the whole layout inside
 * iso_image_create_burn_source() -- it has nowhere else to do it, since the
 * ECMA-119 records carry absolute addresses -- so the figure is already final
 * here, and draining the source to count its bytes would be a slower way of
 * asking the same question that also has to hold the answer in memory. */
uint64_t measure_burn_source(struct burn_source* burn_src);

/* Adds a file straight from memory, with no staging on the host filesystem.
 * False if the node could not be created -- most plausibly because the disc
 * contents already provide that name. */
bool add_iso_file_from_memory(IsoDir* parent, const char* name, const std::string& content);

/* Adds the volume's copyright/abstract/bibliographic text files, and reports
 * which ones ended up there so the PVD fields can be patched to match.
 * `generate_defaults` synthesizes the ones not supplied by option. */
VolumeTextFiles add_volume_text_files(IsoDir* root, const IPBin& ip_bin, bool generate_defaults);

/* Writes the three file identifiers straight into a finished PVD, because
 * libisofs runs them through a d-character filter that rewrites the '.' of
 * COPYRIGH.TXT to '_'. ECMA-119 8.4.31 allows SEPARATOR1 there and every
 * reference dump carries the dot, and there is no relaxation switch. */
void patch_pvd_file_ids(std::vector<uint8_t>& iso, const VolumeTextFiles& present);

/* Overwrites the PVD's volume space size (ECMA-119 8.4.8). libisofs sets it
 * to the filesystem's own size; a GD-ROM's low-density volume instead
 * declares the whole area, through to the end of the audio track. */
void patch_pvd_volume_space(std::vector<uint8_t>& iso, uint32_t sectors);

/* Writes `bytes` zero bytes to `path`, a chunk at a time: the filler files
 * are sized to whatever is left of the disc, so this is worth reporting on --
 * it is most of a gigabyte on a padded image. */
void write_zero_file(const std::filesystem::path& path, std::uintmax_t bytes,
                     const progress_t* progress = nullptr);
