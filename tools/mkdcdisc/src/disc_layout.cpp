/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Disc assembly and output: see disc_layout.hpp. */

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

#include "audio.hpp"
#include "cli.hpp"
#include "disc_layout.hpp"
#include "logging.hpp"
#include "interrupt.hpp"
#include "output_files.hpp"
#include "paths.hpp"
#include "progress.hpp"
#include "terminal.hpp"

/* Build the first (audio) session: either the CDDA tracks given via -c (WAV or
 * raw), or a 4-second blank filler when none are supplied. When log is false, no
 * progress output is emitted (used for the MSINFO-only computation). */
bool add_audio_session(cd_session_t* session0, bool log) {
    if(OPTS.count("cdda-track")) {
        size_t track_idx = 0;
        for(auto& filename: OPTS["cdda-track"]) {
            ++track_idx;
            auto data_maybe = load_audio_track(filename);
            if(!data_maybe) {
                return false;
            }
            auto data = data_maybe.value();

            size_t const audio_sector_size = 2352;
            size_t const audio_bytes_per_second = 176400;
            size_t const minimum_track_sectors = 300;
            size_t const minimum_track_size = minimum_track_sectors * audio_sector_size;
            size_t const minimum_track_duration = minimum_track_size / audio_bytes_per_second;

            /* Enforce audio track size */
            if (data.size() < minimum_track_size) {
                std::cerr << term::error << "audio file " << filename << " is too short - it must be at least " << minimum_track_size << " bytes (" << minimum_track_duration << " seconds)" << std::endl;
                return false;
            }

            cd_new_track(session0, TRACK_TYPE_AUDIO, &data[0], data.size());

            if(log) {
                logging::note() << "Added track " << track_idx << " (" << data.size()
                                << " bytes) from " << filename;
            }
        }
    } else {
        cd_new_track_blank(session0, TRACK_TYPE_AUDIO, 2352 * 302); /* 4 seconds of audio */
    }

    return true;
}

size_t estimate_padding_sectors(const cd_image_t* image_so_far, const std::filesystem::path& data_dir) {
    auto dir_size = [](const std::filesystem::path& dir) -> std::size_t {
        std::size_t size{ 0 };
        for(const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if(entry.is_regular_file() && !entry.is_symlink()) {
                size += entry.file_size();
            }
        }
        return size;
    };

    const std::size_t iso_padding = 10 * 1024 * 1024;  /* 10M for ISO structures */
    std::size_t used = cd_image_length_in_sectors(image_so_far);
    used += dir_size(data_dir) / 2048;
    used += iso_padding / 2048;

    return used >= CD_74MIN_SECTORS ? 0 : CD_74MIN_SECTORS - used;
}

/* Say so when the finished disc is longer than the media it is meant for.
 *
 * A warning and not a refusal, unlike the GD-ROM equivalent in build_gd.cpp:
 * the high-density zone of a GD-ROM is one fixed physical size, so overrunning
 * it is simply wrong, whereas CD-R comes in several and 90- and 99-minute
 * blanks do exist. What can be claimed is that the image has outgrown the
 * media nearly everyone has, and by how much; whether the user's blank and
 * burner will take it is theirs to know.
 *
 * This is the tail of the case behind the dca-miami report: a disc root large
 * enough to fill the track leaves estimate_padding_sectors() with nothing to
 * add, which used to crash and then, once that was fixed, produced an
 * unburnable image in complete silence. */
static void warn_if_over_capacity(const cd_image_t* img) {
    const size_t sectors = cd_image_length_in_sectors(img);

    if(sectors <= CD_80MIN_SECTORS || !logging::enabled(logging::Level::Normal)) {
        return;
    }

    std::cerr << term::warning << "the disc is " << sectors << " sectors, "
              << (sectors - CD_80MIN_SECTORS) << " past the " << CD_80MIN_SECTORS
              << " an 80-minute CD-R holds. It will not burn to standard media."
              << std::endl;
}


/* Add the data track session(s) holding iso_data to img: either a single
 * Audio/Data data session, or (data_data == true) a Data/Data pair; the
 * full ISO followed by a duplicated "boot head" session. The layout only
 * depends on the finished ISO bytes, not on how they were produced, so this
 * is shared by build_cdi() and build_cdi_from_external_iso(). */
void add_data_track_sessions(cd_image_t* img, const std::vector<uint8_t>& iso_data, bool data_data) {
    if(data_data) {
        /* Data/Data layout:
         *
         *   Session 1 : the full ISO, as a data track, built as MSINFO 0
         *               (logical start LBA 0).
         *   Session 2 : a "boot head" data track duplicating the first 18
         *               sectors of the ISO, the IP.BIN system area (16
         *               sectors) plus the ISO9660 volume descriptors, padded
         *               to the 300-sector track minimum. The generic image
         *               writers position this session at
         *               (session 1 length + 11400) on their own, which matches
         *               the reference tools' MSINFO_OFFSET_BASE. */
        cd_session_t* session1 = cd_new_session(img);
        /* Borrowed: iso_data belongs to the caller, who holds it until after
         * write_disc_image(). The boot head below cannot do the same. */
        cd_track_t* data = cd_new_track_borrowed(session1, TRACK_TYPE_DATA,
            &iso_data[0], iso_data.size());
        cd_track_set_mode(data, TRACK_MODE_XA_MODE2_FORM1);
        cd_track_set_postgap_sectors(data, 2);

        const size_t boot_head_sectors  = 18;   /* IP.BIN + volume descriptors */
        const size_t sector_user_bytes  = 2048; /* Mode2/Form1 user data */
        const size_t min_track_sectors  = 300;  /* CD minimum track length */

        std::vector<uint8_t> boot_head(min_track_sectors * sector_user_bytes, 0);
        size_t copy_bytes = std::min(boot_head_sectors * sector_user_bytes, iso_data.size());
        std::copy(iso_data.begin(), iso_data.begin() + copy_bytes, boot_head.begin());

        cd_session_t* session2 = cd_new_session(img);
        /* Copied, deliberately: boot_head is assembled here and dies at the
         * closing brace, long before the image is written. */
        cd_track_t* boot = cd_new_track(session2, TRACK_TYPE_DATA, &boot_head[0], boot_head.size());
        cd_track_set_mode(boot, TRACK_MODE_XA_MODE2_FORM1);
        cd_track_set_postgap_sectors(boot, 2);
    } else {
        /* Audio/Data layout (the default):
         *
         * The audio session was already added before this call (CDDA tracks,
         * or a dummy 4-second track), and the ISO simply lands in a second
         * session here. It must have been built with ms_block matching that
         * session's length, so its LBAs already line up; no boot-head
         * duplication is needed, unlike Data/Data. */
        cd_session_t* session1 = cd_new_session(img);
        /* Borrowed, as in the Data/Data branch above. */
        cd_track_t* data = cd_new_track_borrowed(session1, TRACK_TYPE_DATA,
            &iso_data[0], iso_data.size());
        cd_track_set_mode(data, TRACK_MODE_XA_MODE2_FORM1);
        cd_track_set_postgap_sectors(data, 2); /* Add 2 sectors of postgap to the data track */
    }
}

/* Add the two full data sessions of a merged-data image. Unlike data-data's
 * boot-head synthesis, both ISOs are used exactly as
 * given via -t/--external-data-track: session 2 is expected to already be a
 * merge (via mkisofs -M) of session 1's files with its own new ones, and to
 * carry its own IP.BIN (-G). mkdcdisc does not touch either filesystem. */
void add_merged_data_sessions(cd_image_t* img,
                                    const std::vector<uint8_t>& iso1_data,
                                    const std::vector<uint8_t>& iso2_data) {
    /* Both borrowed: these are the two ISOs read from disk by the caller,
     * which holds them until after write_disc_image(). Two full data sessions
     * copied would be two extra ISOs in memory at once. */
    cd_session_t* session1 = cd_new_session(img);
    cd_track_t* track1 = cd_new_track_borrowed(session1, TRACK_TYPE_DATA,
        &iso1_data[0], iso1_data.size());
    cd_track_set_mode(track1, TRACK_MODE_XA_MODE2_FORM1);
    cd_track_set_postgap_sectors(track1, 2);

    cd_session_t* session2 = cd_new_session(img);
    cd_track_t* track2 = cd_new_track_borrowed(session2, TRACK_TYPE_DATA,
        &iso2_data[0], iso2_data.size());
    cd_track_set_mode(track2, TRACK_MODE_XA_MODE2_FORM1);
    cd_track_set_postgap_sectors(track2, 2);
}

/* Mirrors cd_session_length_in_sectors()'s accounting (disc_image.c) for a
 * session holding a single XA Mode 2 Form 1 data track with a 2-sector
 * postgap and the default 150-sector pregap, without allocating the
 * (potentially huge) track data itself. Used to print the session 2 MSINFO
 * for merged-data from the already-built session 1 ISO's size alone. */
size_t data_session_length_in_sectors(uintmax_t data_size_bytes, bool is_first_session) {
    const size_t SECTOR_USER_BYTES = 2048;
    const size_t LEAD_IN_SECTORS = 4500;
    const size_t LEAD_OUT_SECTORS = is_first_session ? 6750 : 2250;
    const size_t PREGAP_SECTORS = 150;
    const size_t POSTGAP_SECTORS = 2;

    size_t track_sectors = (size_t) ((data_size_bytes + SECTOR_USER_BYTES - 1) / SECTOR_USER_BYTES);
    return LEAD_IN_SECTORS + LEAD_OUT_SECTORS + PREGAP_SECTORS + track_sectors + POSTGAP_SECTORS;
}

/* Write img out as a DiscJuggler .cdi, Alcohol 120% .mds/.mdf pair, or Nero
 * .nrg per -F/--format (or the output extension), then free img. Shared by
 * every build_cdi* function. */
bool write_disc_image(cd_image_t* img, const std::string& output_path) {
    bool written = false;

    /* What the closing message names. Worked out up front rather than by
     * whichever branch below runs, so that a write that never finished can be
     * named too; and so that the build plan can name the same files before
     * any of them exist. */
    const std::string wrote_what = output_files_description(output_path);

    /* Before the slow part, so an unburnable image is announced while it can
     * still be interrupted rather than after the wait. Skipped for -F gdi,
     * which is not CD-shaped and does its own capacity checking against the
     * GD-ROM zones in build_gd.cpp. */
    if(output_format() != "gdi") {
        warn_if_over_capacity(img);
    }

    /* Sector framing (EDC/ECC on every one of them) over hundreds of
     * megabytes: the slowest single step of a build, and the one worth
     * watching. */
    ProgressLine writing("Writing disc image...");
    cd_image_set_progress(img, writing.progress());

    /* Every fopen below reports as a time-of-check/time-of-use race, because
     * check_output_files() stat'd these same paths earlier. That check refuses
     * to clobber a file the user forgot about -- it is a courtesy, not a
     * security gate, and nothing downstream trusts its answer. mkdcdisc is a
     * plain non-setuid CLI writing a path its own invoker named, so there is no
     * privilege boundary for a swapped path to cross: whoever can win the race
     * can already write the file outright. Closing it would mean O_EXCL and
     * refusing to overwrite, which is the opposite of what -o asks for. */
    if(output_format() == "mds") {
        auto mds_path = std::filesystem::path(output_path).replace_extension("mds");
        auto mdf_path = std::filesystem::path(output_path).replace_extension("mdf");

        FILE* mds = fopen(PATH_TO_CSTR(mds_path), "wb"); // nosemgrep: toctou-race-condition
        FILE* mdf = fopen(PATH_TO_CSTR(mdf_path), "wb"); // nosemgrep: toctou-race-condition

        if(mds && mdf) {
            written = cd_write_to_mds(img, mds, mdf);
        }

        if(mds) fclose(mds);
        if(mdf) fclose(mdf);
    } else if(output_format() == "nrg") {
        const char* path = output_path.c_str();
        FILE* output = fopen(path, "wb"); // nosemgrep: toctou-race-condition

        if(output) {
            written = cd_write_to_nrg(img, output);
            fclose(output);
        }
    } else if(output_format() == "gdi") {
        auto gdi_path = std::filesystem::path(output_path);

        std::error_code ec;
        auto track_dir = gdi_path.parent_path();
        if(!track_dir.empty()) {
            std::filesystem::create_directories(track_dir, ec);
        }

        FILE* gdi_file = fopen(PATH_TO_CSTR(gdi_path), "wb"); // nosemgrep: toctou-race-condition

        if(gdi_file) {
            written = cd_write_to_gdi(img, gdi_file, PATH_TO_CSTR(track_dir), gdi_sector_size());
            fclose(gdi_file);
        }
    } else {
        const char* path = output_path.c_str();
        FILE* output = fopen(path, "wb"); // nosemgrep: toctou-race-condition

        if(output) {
            written = cd_write_to_cdi(img, output, path);
            fclose(output);
        }
    }

    /* A failed write leaves the progress line unfinished on purpose: it never
     * reached "Done.", and the error the caller prints starts on its own
     * line. */
    if(written) {
        writing.finish();

        logging::note() << "Wrote " << wrote_what;
    } else if(interrupted()) {
        /* Named rather than deleted: what is on disc is the user's, even
         * half-written, and it is not this function's call to remove a file
         * that may have existed before this run. */
        std::cerr << "Left incomplete: " << wrote_what << std::endl;
    }

    cd_free_image(&img);

    return written;
}

/* Read path fully into out_data, validating it's non-empty and warning if its
 * size isn't a whole number of 2048-byte sectors. Shared by the External ISO
 * Mode and merged-data code paths. */
bool read_iso_file(const std::filesystem::path& path, std::vector<uint8_t>& out_data) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if(!in) {
        std::cerr << term::error << "unable to open ISO file: " << path << std::endl;
        return false;
    }

    std::streamsize size = in.tellg();
    if(size <= 0) {
        std::cerr << term::error << "ISO file is empty: " << path << std::endl;
        return false;
    }

    in.seekg(0, std::ios::beg);
    out_data.resize(size);
    if(!in.read(reinterpret_cast<char*>(&out_data[0]), size)) {
        std::cerr << term::error << "unable to read ISO file: " << path << std::endl;
        return false;
    }

    if(size % 2048 != 0 && logging::enabled(logging::Level::Normal)) {
        std::cerr << term::warning << "ISO size (" << size << " bytes) is not a multiple of "
                                      "2048; the file may not be a raw 2048-byte/sector ISO image." << std::endl;
    }

    return true;
}
