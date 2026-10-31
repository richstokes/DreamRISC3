/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* The files a run writes, and whether it may: see output_files.hpp. */

#include <algorithm>
#include <cctype>
#include <iostream>
#include <system_error>
#include <vector>

#include "cli.hpp"
#include "logging.hpp"
#include "output_files.hpp"
#include "terminal.hpp"

/* The directory a -F gdi run drops its track files in: alongside the .gdi
 * descriptor, which means the current one when the output path names no
 * directory at all. */
static std::filesystem::path gdi_track_directory(const std::string& output_path) {
    auto track_dir = std::filesystem::path(output_path).parent_path();

    return track_dir.empty() ? std::filesystem::path(".") : track_dir;
}

std::string output_files_description(const std::string& output_path) {
    if(output_format() == "mds") {
        auto mds_path = std::filesystem::path(output_path).replace_extension("mds");
        auto mdf_path = std::filesystem::path(output_path).replace_extension("mdf");

        return mds_path.string() + " + " + mdf_path.string();
    }

    if(output_format() == "gdi") {
        return output_path + " + track files in " + gdi_track_directory(output_path).string();
    }

    return output_path;
}

/* Whether `filename` is one of the track files of a .gdi set, named the way
 * cd_write_to_gdi() names them: "track" and a two-digit track number, then the
 * extension its sector format takes -- .bin for raw Mode 1 data, .iso for
 * plain 2048-byte data, .raw for CD-DA.
 *
 * Matched by pattern because how many track files a build produces is not
 * known until the disc has been laid out: it depends on the GD-DA tracks, and
 * a --gdi-sector-size that flips .bin to .iso means a rebuild can leave the
 * previous set's data tracks behind rather than replacing them. Every name this
 * answers to is a name some .gdi build writes, which is what makes it the set
 * worth refusing to walk into -- a stale track01.bin next to a new track01.iso
 * is a directory holding two discs and describing one.
 *
 * Case-insensitively, because a name differing only in case is the same file on
 * Windows and on macOS, and refusing it where it isn't costs a build that says
 * exactly which file it stopped for. */
static bool is_gdi_track_filename(const std::string& filename) {
    static const std::string PREFIX = "track";

    /* "track", two digits, a dot and a three-letter extension, so the length
     * is settled before anything is indexed. */
    if(filename.size() != PREFIX.size() + 6) {
        return false;
    }

    std::string name = filename;
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return (char) std::tolower(c); });

    if(name.compare(0, PREFIX.size(), PREFIX) != 0) {
        return false;
    }

    const size_t digits_at = PREFIX.size();

    if(!std::isdigit((unsigned char) name[digits_at]) ||
       !std::isdigit((unsigned char) name[digits_at + 1]) ||
       name[digits_at + 2] != '.') {
        return false;
    }

    const std::string extension = name.substr(digits_at + 3);
    return extension == "bin" || extension == "iso" || extension == "raw";
}

/* The files already on disc that this run would write into, in the order a
 * diagnostic should name them: what -o itself asks for first, then the rest.
 * Empty when the target is clear. */
static std::vector<std::filesystem::path> existing_output_files(const std::string& output_path) {
    std::vector<std::filesystem::path> found;

    auto add_if_present = [&](const std::filesystem::path& path) {
        std::error_code ec;
        if(!std::filesystem::exists(path, ec)) {
            return;
        }

        /* -o out.iso -I names the image and the dumped data track identically,
         * the format being inferred as cdi either way. One file, said once. */
        if(std::find(found.begin(), found.end(), path) == found.end()) {
            found.push_back(path);
        }
    };

    if(output_format() == "mds") {
        add_if_present(std::filesystem::path(output_path).replace_extension("mds"));
        add_if_present(std::filesystem::path(output_path).replace_extension("mdf"));
    } else {
        add_if_present(output_path);
    }

    if(output_format() == "gdi") {
        /* Sorted, so a set of leftover track files reads in track order rather
         * than in whatever order the directory happens to be walked in. */
        std::vector<std::filesystem::path> tracks;

        std::error_code ec;
        for(const auto& entry:
                std::filesystem::directory_iterator(gdi_track_directory(output_path), ec)) {
            if(is_gdi_track_filename(entry.path().filename().string())) {
                tracks.push_back(entry.path());
            }
        }

        std::sort(tracks.begin(), tracks.end());
        found.insert(found.end(), tracks.begin(), tracks.end());
    }

    /* Last, because it is the extra output rather than the one asked for, and
     * -I/--dump-iso is MIL-CD only, so it never joins the track files above. */
    if(OPTS.count("dump-iso")) {
        add_if_present(std::filesystem::path(output_path).replace_extension("iso"));
    }

    return found;
}

bool check_output_files(const std::string& output_path) {
    const std::vector<std::filesystem::path> existing = existing_output_files(output_path);

    if(existing.empty()) {
        return true;
    }

    std::string names;
    for(const auto& path: existing) {
        if(!names.empty()) {
            names += ", ";
        }
        names += path.string();
    }

    /* Named rather than counted even when there are many: every one of them is
     * about to be destroyed, and which files those are is the whole question. */
    if(OPTS.count("allow-overwrite")) {
        logging::note() << "Allow overwriting: writing over " << names;
        return true;
    }

    const bool one = existing.size() == 1;

    std::cerr << term::error << "the output file" << (one ? " already exists: " : "s already exist: ")
              << names << ". Nothing has been written; use --allow-overwrite to write over "
              << (one ? "it" : "them") << ", or -o/--output to build somewhere else." << std::endl;

    return false;
}
