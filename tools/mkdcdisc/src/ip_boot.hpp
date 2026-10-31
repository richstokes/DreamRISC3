/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* The Dreamcast boot header: the 32K structure a disc carries at the start of
 * its first data track, holding the metadata the console reads plus the
 * second-stage bootstrap.
 *
 * A GD-ROM has two of them, sharing only their 256-byte header: the
 * high-density IP.BIN, which carries the TOC and the Sega logos, and the
 * low-density IP0000.BIN, which carries neither and is one 2048-byte
 * sector repeated sixteen times. Only the high-density one is a bundled
 * template, the low-density image is built from its header, see
 * replicate_lda_header() below. A MIL-CD has just the one. */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct IPBin {
    struct {
        char hardware_id[16];
        char maker_id[16];
        char device_info[16];
        char area_symbols[8];
        char peripherals[8];
        char product_number[10];
        char product_version[6];
        char release_date[16];
        char boot_filename[16];
        char company_name[16];
        char game_name[128];
    } meta;

    char toc[512] = {0};
    char license_screen[13312];
    char area_protection[256];
    char bootstrap1[10240];
    char bootstrap2[8192];
};

/* The bundled templates, defined once in ip_boot.cpp. Each is emitted by xxd
 * as an array plus a matching _len. */
extern unsigned char IP_BIN_MILCD[];        /* MIL-CD, for CDI/MDS/NRG */
extern unsigned int IP_BIN_MILCD_len;
extern unsigned char IP_BIN_GDROM_HDA_NEAREST[];  /* GD-ROM high-density area, */
extern unsigned int IP_BIN_GDROM_HDA_NEAREST_len; /* one per FPU rounding mode */
extern unsigned char IP_BIN_GDROM_HDA_ZERO[];
extern unsigned int IP_BIN_GDROM_HDA_ZERO_len;

/* The GD-ROM high-density template --gdi-fpu-rounding selects: the round-to-
 * nearest one by default, the round-to-zero one when asked for.
 *
 * The two differ only in the second-stage bootstrap, which sets the SH-4
 * FPSCR rounding mode before handing over to the main executable. Programs
 * built against KallistiOS set it themselves and are unaffected; the choice
 * matters to code that inherits whatever the bootstrap left, which is what
 * round-to-nearest -- the mode every previous release shipped, and the
 * default here -- has always given them.
 *
 * The option is validated in main.cpp, so an unrecognized value never reaches
 * this far; anything other than "zero" reads as the default. */
const unsigned char* gd_hda_template();
size_t gd_hda_template_len();

/* Which mode the above will pick, for the -v 3 build plan to report. */
std::string gd_fpu_rounding();

/* Extracts one of the fixed-size, space- or null-padded string fields of
 * IPBin::meta into a plain std::string, stripping the padding. */
std::string trim_ipbin_field(const char* data, size_t max_len);

/* The two fields of the header that are nobody's to choose: every Dreamcast
 * disc ever pressed carries exactly these bytes, and they are what identifies
 * the 32K block as a boot header at all rather than as 32K of anything else.
 * Neither is NUL-terminated in the header, so both are compared over exactly
 * their field's width. */
extern const char* const IPBIN_HARDWARE_ID;  /* "SEGA SEGAKATANA " */
extern const char* const IPBIN_MAKER_ID;     /* "SEGA ENTERPRISES" */

/* Whether a Boot Filename field names something a console can actually look
 * up: a non-empty bare file name, not a path and not a directory reference.
 * Asked both of the -p/--bootstrap header mkdcdisc is about to write (see
 * boot_filename()) and of the one an external ISO arrived carrying, so that
 * the two readings of the same field cannot drift apart. */
bool is_usable_boot_filename(const std::string& name);

/* The SEGA licence screen code, IP.BIN offsets 0300-36FF: the code that draws
 * the "PRODUCED BY OR UNDER LICENSE FROM SEGA ENTERPRISES, LTD." screen before
 * handing over to the bootstrap.
 *
 * The one stretch of the header that cannot be modified at all. The boot ROM
 * compares every byte of it against a copy it holds itself and refuses to boot
 * the disc on any difference (Comstedt, "IP.BIN"), so a disc carrying anything
 * else here is not a disc with an unusual licence screen -- it is a disc that
 * does not boot, on every console.
 *
 * Byte-identical in all three bundled templates, the MIL-CD one and both
 * GD-ROM ones, which is what lets there be a single answer here rather than
 * one per format: a volume can be checked against it without first working out
 * which template the disc it came from was built from. */
extern const size_t IPBIN_LICENSE_CODE_OFFSET;
extern const size_t IPBIN_LICENSE_CODE_LENGTH;

const unsigned char* ipbin_license_code();

/* What the header will end up declaring, worked out before any of it is
 * written: the -p/--bootstrap file's own fields when there is one, otherwise
 * the metadata options over whatever the template ships with -- which is where
 * the two fields that have no generated default (product version, peripherals)
 * come from when nothing overrides them.
 *
 * `serial` is empty when -s/--serial was not given, because its default is a
 * hash of the boot binary and that binary need not exist yet. Every other
 * field is final.
 *
 * Resolved here rather than inside generate_ip_bin() so that the -v 3 build
 * plan can report the same answers the header is about to be filled with,
 * instead of a second opinion about them. */
struct IPBinMetadata {
    std::string title;
    std::string author;
    std::string serial;
    std::string product_version;
    std::string release_date;
    std::string peripherals;
    std::string boot_filename;

    /* The fields above came out of a -p/--bootstrap file, which is used
     * verbatim -- so the metadata options played no part in them. False when
     * no such file was given, and when the one given could not be read. */
    bool from_custom_bootstrap = false;
};

IPBinMetadata planned_ip_bin_metadata(const unsigned char* template_data, size_t template_len);

/* The title the header will declare: the -p/--bootstrap file's own, otherwise
 * -n/--title, otherwise the built-in default. Split out of the metadata above
 * because it is the one field that does not depend on which template is in
 * use, and the ISO9660 volume label falls back to it -- see volume_label() in
 * iso_builder.hpp. */
std::string ipbin_title();

/* The name the main executable takes on the disc, and in the Boot Filename
 * field of the IP.BIN that ships with it. The two are the same question: it
 * is the name the console looks up at boot time, so a disc whose header and
 * whose filesystem disagree about it simply does not boot.
 *
 * A -p/--bootstrap IP.BIN is used verbatim, which makes it the answer:
 * whatever it declares is what the console will ask for. Otherwise
 * --boot-filename, otherwise 1ST_READ.BIN.
 *
 * This is the name the file is *staged* under, in the case it was given: it is
 * what gather_files() writes, and therefore what Joliet and Rock Ridge carry --
 * the names KallistiOS resolves at runtime. The header field is a separate
 * question, because ECMA-119's d-characters have no lower case and libisofs
 * upper-cases every name on its way into the primary tree; that field is
 * upper-cased to match, in set_ipbin_boot_filename() and nowhere else. So a
 * disc can hold game.bin under Joliet and Rock Ridge, GAME.BIN in the primary
 * tree, and declare GAME.BIN in its header -- three spellings of one file, each
 * the way its own reader writes it.
 *
 * Which is why callers wanting to find the file, on the host or in a libisofs
 * tree, ask here rather than reading the header field back: see the sort-weight
 * pinning in build_gd.cpp.
 *
 * Resolved once, on the first call, since it may have to read the
 * -p/--bootstrap file and warn about what it finds. */
std::string boot_filename();

bool load_ip_bin(const std::filesystem::path& ipbin_path, IPBin* out);

/* Fills `template_data` in from the metadata options, and places the MR boot
 * logo at the fixed offset the layout reserves for it. */
bool generate_ip_bin(const std::filesystem::path& output_dir,
                     const std::filesystem::path& bin_path, IPBin* out,
                     const unsigned char* template_data, size_t template_len);

/* Either the user's -p/--bootstrap file used verbatim, or `template_data` filled
 * in from the metadata options. Returns true iff the custom file was used, so
 * callers can apply their own format-specific post-processing on top. */
bool prepare_ip_bin(IPBin* out, const std::filesystem::path& input_dir,
                    const unsigned char* template_data, size_t template_len);

/* Copies the 256-byte header into all sixteen sectors of a low-density
 * boot image, which is that one sector repeated. Writing the metadata once
 * would leave fifteen sectors advertising nothing at all -- a mismatch no
 * pressed disc has. */
void replicate_lda_header(IPBin* ip_bin);
