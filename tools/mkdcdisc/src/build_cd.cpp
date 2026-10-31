/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* CD-shaped disc building: see build_cd.hpp. */

#include <climits>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

#include "boot_checks.hpp"
#include "build_cd.hpp"
#include "cli.hpp"
#include "disc_layout.hpp"
#include "duplicates.hpp"
#include "input_files.hpp"
#include "interrupt.hpp"
#include "ip_boot.hpp"
#include "iso_builder.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "progress.hpp"
#include "sort_file.hpp"
#include "terminal.hpp"

/* Standard mode */
bool build_cd(const std::filesystem::path& input_dir) {
    std::string output_cdi = OPTS["output"][0];

    /* Disc layout selection. The default is the classic Audio/Data layout
     * (a dummy/CDDA audio session followed by the data session).
     * -L/--disc-layout data-data instead produces a Data/Data image, which has
     * no audio session, so CDDA tracks are not possible there. merged-data
     * never reaches here: it is External ISO Mode only. */
    const bool data_data = disc_layout() == DiscLayout::DataData;

    IPBin ip_bin;
    prepare_ip_bin(&ip_bin, input_dir, IP_BIN_MILCD, IP_BIN_MILCD_len);

    /* Generate CDI image */
    cd_image_t* img = cd_new_image();
    cd_image_set_volume_name(img, PATH_TO_CSTR(std::filesystem::path(output_cdi).filename().stem()));

    /* In the default Audio/Data layout the first session holds the CDDA
     * audio track(s) (or a dummy audio track), and the data track lands in a
     * second session at the standard high-density offset. The ISO is built
     * with ms_block == that offset.
     *
     * A Data/Data image has no audio session: the ISO is built as MSINFO 0
     * (ms_block == 0) and a boot track is duplicated into a second data
     * session further down. So we skip the audio session entirely here. */
    size_t start_lba = 0;

    if(!data_data) {
    /* Add the first session, this is where CDDA tracks go */
    cd_session_t* session0 = cd_new_session(img);

    if(!add_audio_session(session0)) {
        return false;
    }

    start_lba = cd_session_length_in_sectors(session0);
    } /* end if(!data_data) */

    std::size_t padding_file_size = estimate_padding_sectors(img, input_dir) * 2048;

    /* Padding fills the track out to a 74-minute disc so the real content
     * sits on the outer edge, where a CD reads fastest. Content that already
     * reaches that size leaves nothing to add, which is not a failure but is
     * worth saying: the load-time optimization silently does not happen, and
     * a disc this size is where write_disc_image()'s capacity warning starts
     * becoming relevant. */
    if(!OPTS.count("disable-data-track-padding") && padding_file_size == 0) {
        logging::verbose() << "Data track padding skipped: the disc contents already "
                              "fill a 74-minute CD";
    }

    if(!OPTS.count("disable-data-track-padding") && padding_file_size > 0) {
        ProgressLine padding("Padding data track...");
        write_zero_file(std::filesystem::path(input_dir) / "0.0", padding_file_size,
            padding.progress());

        if(interrupted()) {
            return false;
        }

        padding.finish();
    }

    /* Build ISO */
    iso_init();

    IsoWriteOpts *opts = NULL;

    /* Held for the whole function, not released after the first burn source:
     * -I/--dump-iso builds a second one from this same image further down. */
    IsoImageRef image = new_iso_image(ip_bin, volume_label());
    IsoImage* iso = image.get();

    IsoDir* root = iso_image_get_root(iso);

    iso_tree_add_dir_rec(iso, root, PATH_TO_CSTR(input_dir));

    /* Only what --copyright/--abstract/--biblio explicitly ask for: unlike a
     * GD-ROM's low-density area, a MIL-CD has no convention of always
     * carrying these, and generating them by default would add three files to
     * every image mkdcdisc has ever produced. */
    VolumeTextFiles text_files = add_volume_text_files(root, ip_bin,
        /* generate_defaults */ false);

    if(text_files.copyright) iso_image_set_copyright_file_id(iso, PVD_COPYRIGHT_FILE);
    if(text_files.abstract) iso_image_set_abstract_file_id(iso, PVD_ABSTRACT_FILE);
    if(text_files.biblio) iso_image_set_biblio_file_id(iso, PVD_BIBLIO_FILE);

    /* Weight the padding file to the front, and hide it from the RR/ECMA-119
     * and Joliet trees (equivalent to mkisofs' -hide/-hide-joliet): it's
     * padding, not content, but its bytes must still be written, hence
     * LIBISO_HIDE_BUT_WRITE (without it, libisofs drops the content of a node
     * that's invisible in every tree).
     *
     * libisofs writes high weights to LOW block addresses, so INT_MAX puts the
     * padding at the bottom of the track and pushes the real content outwards,
     * where a CD reads fastest; the load-time optimization
     * -N/--disable-data-track-padding turns off.
     *
     * 1ST_READ.BIN is deliberately left at the default weight 0 here, unlike
     * build_hda_iso() which pins the boot binary to the very end: that
     * placement is a GD-ROM trait, not something a MIL-CD wants. */
    /* Files --duplicates-once must leave alone. Collected as node pointers
     * rather than names because not all of the names are knowable here: a
     * -p/--bootstrap can call the boot binary something other than
     * 1ST_READ.BIN. */
    std::set<IsoNode*> undeduplicable;

    if(!OPTS.count("disable-data-track-padding") && padding_file_size > 0) {
        IsoNode* padding_node = NULL;
        if(iso_dir_get_node(root, "0.0", &padding_node) == 1) {
            iso_node_set_sort_weight(padding_node, INT_MAX);
            iso_node_set_hidden(padding_node,
                LIBISO_HIDE_ON_RR | LIBISO_HIDE_ON_JOLIET | LIBISO_HIDE_BUT_WRITE);

            /* Its whole purpose is to occupy space, so it is the one file on
             * the disc that must never be shared away, and being written
             * from zeroes it is byte-identical to any other filler of the
             * same length, which makes it a certain match rather than an
             * unlikely one. */
            undeduplicable.insert(padding_node);
        }
    }

    /* Weights are handed to --duplicates-once rather than used to exclude:
     * a shared extent has one position, so the set takes the highest weight
     * among it, and any file placed below that is named in a warning. */
    const std::map<IsoNode*, int> weights = apply_sort_file("sort-file", root);

    /* The boot binary is never shared either. On a MIL-CD its position is not
     * pinned the way a GD-ROM's is, so nothing would break -- but "the file
     * the console boots from is written on its own" is a rule worth having
     * without exceptions, and one file's worth of saving is not worth the
     * caveat. */
    IsoNode* boot_node = NULL;
    const std::string boot_file = boot_filename();
    if(!boot_file.empty() && iso_dir_get_node(root, boot_file.c_str(), &boot_node) == 1) {
        undeduplicable.insert(boot_node);
    }

    apply_duplicates_once("data track", iso, root, undeduplicable, weights);

    opts = create_iso_write_opts(ip_bin, start_lba);

    struct burn_source* burn_src;
    iso_image_create_burn_source(iso, opts, &burn_src);

    ProgressLine generating("Generating data track...");

    std::vector<uint8_t> iso_data = read_burn_source(burn_src, generating.progress());
    iso_write_opts_free(opts);

    if(interrupted()) {
        return false;
    }

    patch_pvd_file_ids(iso_data, text_files);

    generating.finish();

    logging::debug() << "Data track is " << iso_data.size() << " bytes";

    /* The same checks the external ISO gets, over the filesystem mkdcdisc just
     * built itself. They should never fire here, which is the point: mkdcdisc
     * controls every byte of this, so one of them firing is a regression in
     * this program rather than a mistake by the user -- and hearing about it is
     * worth more than the assumption that it cannot happen.
     *
     * Exact, unlike the external case: start_lba is the figure libisofs was
     * handed as its ms_block a few lines up, not a figure some other tool
     * arrived at by its own accounting, so there is no modelling difference to
     * allow for.
     *
     * Ahead of the -I/--dump-iso block deliberately, so a refused build does
     * not leave a dumped .iso behind as though it had succeeded. */
    if(!boot_checks::check_disc({
        {"the data track", boot_checks::Role::Bootable, &iso_data,
         boot_checks::MsinfoExpectation{(uint32_t) start_lba, /* exact */ true}},
    })) {
        return false;
    }

    if(OPTS.count("dump-iso")) {
        std::string iso_path = PATH_TO_CSTR(std::filesystem::path(output_cdi).replace_extension("iso"));

        logging::note() << "Dumping the data track to " << iso_path;

        if(start_lba == 0) {
            std::ofstream iso_out(iso_path, std::ios::binary);
            iso_out.write((char*) &iso_data[0], iso_data.size());
        } else {
            /* iso_data has start_lba baked into every directory-record LBA,
             * so on its own (outside the multi-session disc it's meant to
             * sit at that offset within) it's not a valid standalone
             * ISO9660 image. Build it again with ms_block=0 just for the dump. */
            IsoWriteOpts* dump_opts = create_iso_write_opts(ip_bin, 0);

            struct burn_source* dump_burn_src;
            iso_image_create_burn_source(iso, dump_opts, &dump_burn_src);

            /* A second full pass over the filesystem, as slow as the first. */
            ProgressLine dumping("Generating standalone .iso...");
            std::vector<uint8_t> dump_data = read_burn_source(dump_burn_src, dumping.progress());

            if(interrupted()) {
                return false;
            }

            dumping.finish();

            iso_write_opts_free(dump_opts);

            patch_pvd_file_ids(dump_data, text_files);

            std::ofstream iso_out(iso_path, std::ios::binary);
            iso_out.write((char*) &dump_data[0], dump_data.size());
        }
    }

    add_data_track_sessions(img, iso_data, data_data);

    return write_disc_image(img, output_cdi);
}

/* External-ISO mode: wrap a pre-built ISO (made by mkisofs, with IP.BIN already
 * embedded via its -G switch) into a CDI or MDS/MDF, exactly like img4dc does for
 * CDI or MDS+MDF. mkdcdisc does not touch the filesystem or IP.BIN here: it only
 * builds the Audio/Data (or, with -L/--disc-layout data-data, Data/Data) disc
 * structure around the given ISO, then writes it out in the format selected by
 * -F/--format (or inferred from the output extension). */
bool build_cd_from_external_iso(const std::filesystem::path& iso_path) {
    std::string output_path = OPTS["output"][0];

    const bool data_data = disc_layout() == DiscLayout::DataData;

    std::vector<uint8_t> iso_data;
    if(!read_iso_file(iso_path, iso_data)) {
        return false;
    }

    logging::note() << "Using external ISO as data track: " << iso_path
                    << " (" << iso_data.size() << " bytes)";

    /* Generate the disc image */
    cd_image_t* img = cd_new_image();
    cd_image_set_volume_name(img, PATH_TO_CSTR(std::filesystem::path(output_path).filename().stem()));

    /* Audio/Data layout (the default): first session holds the CDDA tracks
     * given via -c, or a blank 4-second filler; the external ISO must have
     * been built with a matching mkisofs -C 0,<msinfo> so its internal
     * addresses line up. Data/Data layout has no audio session: the
     * external ISO must have been built at MSINFO 0 instead. */
    boot_checks::MsinfoExpectation expected_msinfo;

    if(!data_data) {
        cd_session_t* session0 = cd_new_session(img);
        if(!add_audio_session(session0)) {
            cd_free_image(&img);
            return false;
        }

        /* Where the audio session ends, which is a figure worked out from
         * lead-in, lead-out and pregap lengths rather than measured -- hence
         * not exact, see MsinfoExpectation. */
        expected_msinfo = {(uint32_t) cd_session_length_in_sectors(session0), /* exact */ false};
        logging::note() << "Data track MSINFO: " << expected_msinfo.sectors;
    } else {
        logging::note() << "Data/Data layout: the external ISO must have been built at MSINFO 0.";
    }

    /* The ISO came from mkisofs rather than from here, so this is the first
     * point at which anything has looked at what is actually in it. Placed
     * after the audio session because that is what settles the sector the ISO
     * must have been built for; nothing of the disc has been written yet.
     *
     * Both layouts reaching this function boot from the volume itself --
     * Data/Data's second session is a duplicate of its first 18 sectors, not a
     * volume of its own. */
    if(!boot_checks::check_disc({
        {"the external ISO", boot_checks::Role::Bootable, &iso_data, expected_msinfo},
    })) {
        cd_free_image(&img);
        return false;
    }

    add_data_track_sessions(img, iso_data, data_data);

    return write_disc_image(img, output_path);
}

/* -L/--disc-layout merged-data: wrap two pre-built ISOs (both made externally by
 * mkisofs) as two full data sessions. Session 2 must already be a merge of
 * session 1's files with its own new ones (mkisofs -M) and carry its own
 * IP.BIN (-G); mkdcdisc does not build or touch either filesystem here. */
bool build_cd_from_merged_data_isos(const std::filesystem::path& iso1_path,
                                      const std::filesystem::path& iso2_path) {
    std::string output_path = OPTS["output"][0];

    std::vector<uint8_t> iso1_data, iso2_data;
    if(!read_iso_file(iso1_path, iso1_data) || !read_iso_file(iso2_path, iso2_data)) {
        return false;
    }

    logging::note() << "Using external ISO as session 1 data track: " << iso1_path
                    << " (" << iso1_data.size() << " bytes)";
    logging::note() << "Using external ISO as session 2 data track: " << iso2_path
                    << " (" << iso2_data.size() << " bytes)";

    /* Session 1 is read but never booted, so it is held to a different
     * standard: it needs no boot header of its own, and one it happens to
     * carry is reported as unused rather than as wrong.
     *
     * The sectors they must have been built for differ too. Session 1 starts
     * the disc, so exactly 0 -- there is nothing to model. Session 2 starts
     * wherever session 1 ended, which is the same figure -M/--print-msinfo
     * prints for this pair, computed from session 1's size alone. */
    boot_checks::MsinfoExpectation session1_msinfo{0, /* exact */ true};
    boot_checks::MsinfoExpectation session2_msinfo{
        (uint32_t) data_session_length_in_sectors(iso1_data.size(), /* is_first_session */ true),
        /* exact */ false,
    };

    if(!boot_checks::check_disc({
        {"the session 1 ISO", boot_checks::Role::NotBooted, &iso1_data, session1_msinfo},
        {"the session 2 ISO", boot_checks::Role::Bootable, &iso2_data, session2_msinfo},
    })) {
        return false;
    }

    cd_image_t* img = cd_new_image();
    cd_image_set_volume_name(img, PATH_TO_CSTR(std::filesystem::path(output_path).filename().stem()));

    add_merged_data_sessions(img, iso1_data, iso2_data);

    return write_disc_image(img, output_path);
}
