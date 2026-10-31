/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* The -v 3 build plan: see plan.hpp. */

#include <iomanip>
#include <string>
#include <vector>

#include "cli.hpp"
#include "ip_boot.hpp"
#include "iso_builder.hpp"
#include "logging.hpp"
#include "output_files.hpp"
#include "plan.hpp"

#include "disc_image/disc_image.h"

namespace {

/* Column the values line up at, wide enough for the longest label below. A
 * fixed width rather than one measured from the labels actually printed: the
 * set of them depends on the mode, and a block that lines up differently
 * depending on the options is harder to compare between two bug reports. */
constexpr int LABEL_WIDTH = 24;

void field(const char* label, const std::string& value) {
    logging::debug() << std::left << std::setw(LABEL_WIDTH) << label << ": " << value;
}

/* How many times an option was given, which for the repeatable ones is how
 * many files or directories it contributes. */
size_t times_given(const char* option) {
    return OPTS.count(option) ? OPTS[option].size() : 0;
}

std::string first_value(const char* option) {
    return OPTS[option][0];
}

/* "1 file" / "3 files": a count with its noun in the right number. */
std::string counted(size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

std::string join(const std::vector<std::string>& parts, const char* separator) {
    std::string joined;

    for(const auto& part: parts) {
        if(!joined.empty()) {
            joined += separator;
        }
        joined += part;
    }

    return joined;
}

/* What a -d/-D/-f family puts on a volume, counted rather than listed: the
 * files themselves are named one by one at -v 2 as they are staged, and a
 * plan that reprinted a thousand of them would bury everything around it.
 *
 * -d and -D are counted together. They differ in whether the directory itself
 * appears on the volume, which matters to the contents and not to how much of
 * the command line went into them. */
std::string contents_summary(const char* directory, const char* directory_contents, const char* file) {
    std::vector<std::string> parts;

    size_t directories = times_given(directory) + times_given(directory_contents);
    if(directories) {
        parts.push_back(counted(directories, "directory", "directories"));
    }

    if(times_given(file)) {
        parts.push_back(counted(times_given(file), "file", "files"));
    }

    return parts.empty() ? "nothing" : join(parts, ", ");
}

std::string image_format_name() {
    const std::string format = output_format();

    if(format == "mds") {
        return "Alcohol 120% (MDS+MDF)";
    }
    if(format == "nrg") {
        return "Nero Burning ROM (NRG)";
    }
    if(format == "gdi") {
        return "GD-ROM disc image (GDI)";
    }

    return "Padus DiscJuggler (CDI)";
}

std::string image_layout_name() {
    if(output_format() == "gdi") {
        /* Not one of the three MIL-CD layouts at all: a GD-ROM's two zones are
         * what the format is, rather than an arrangement chosen for it. */
        return "Low-density/high-density (GD-ROM)";
    }
    return disc_layout_name(disc_layout());
}

/* Which of -e/-b/-B supplied the executable, and what mkdcdisc will do to it
 * on the way in. */
std::string main_executable() {
    if(OPTS.count("main-elf")) {
        return first_value("main-elf") + " (ELF)";
    }
    if(OPTS.count("main-binary-unscrambled")) {
        return first_value("main-binary-unscrambled") + " (unscrambled binary)";
    }

    return first_value("main-binary-scrambled") + " (already scrambled binary)";
}

std::string executable_scrambling() {
    /* Decided by the output format alone, whichever of -e/-b/-B was given:
     * MIL-CD boot requires a scrambled binary, GD-ROM boot requires it not
     * be. An already scrambled -B is therefore unscrambled again for a
     * GD-ROM. */
    if(output_format() == "gdi") {
        return OPTS.count("main-binary-scrambled")
            ? "unscrambled again for GD-ROM boot"
            : "none, GD-ROM boot wants it unscrambled";
    }

    return OPTS.count("main-binary-scrambled")
        ? "already scrambled, used as given"
        : "scrambled for MIL-CD boot";
}

std::string bootstrap_source(const IPBinMetadata& meta) {
    if(!OPTS.count("bootstrap")) {
        return "the built-in template";
    }

    if(!meta.from_custom_bootstrap) {
        return first_value("bootstrap") + " (unreadable; falling back to the built-in template)";
    }

    return first_value("bootstrap") + " (used as given; the other IP.BIN options are ignored)";
}

/* -F gdi only: which of the two high-density templates is in use. A
 * -p/--bootstrap file is used verbatim, bootstrap included, so it brings its
 * own rounding mode and the option plays no part -- the same reasoning that
 * makes main.cpp warn about the combination. */
std::string fpu_rounding(const IPBinMetadata& meta) {
    if(meta.from_custom_bootstrap) {
        return "whatever the -p/--bootstrap file's bootstrap sets";
    }

    return gd_fpu_rounding() == "zero" ? "round to zero" : "round to nearest (the default)";
}

std::string license_screen(const IPBinMetadata& meta) {
    if(meta.from_custom_bootstrap) {
        return "whatever the -p/--bootstrap file carries";
    }
    if(OPTS.count("disable-bootstrap-mr")) {
        return "none";
    }
    if(OPTS.count("bootstrap-mr-image")) {
        return first_value("bootstrap-mr-image");
    }

    return "the built-in default MR image";
}

/* The three text files the volume can reference from its PVD. A GD-ROM
 * generates the ones not supplied, since every reference dump carries all
 * three, bar those a --gdi-disable-* flag keeps off the disc; the other
 * formats only get what was asked for. */
std::string volume_text_files() {
    const std::tuple<const char*, const char*, const char*> files[] = {
        {"copyright", "gdi-disable-copyright", PVD_COPYRIGHT_FILE},
        {"abstract",  "gdi-disable-abstract",  PVD_ABSTRACT_FILE},
        {"biblio",    "gdi-disable-biblio",    PVD_BIBLIO_FILE},
    };

    /* Outside -F gdi nothing is generated and the --gdi-disable-* flags are
     * refused, so the volume carries exactly what was named and listing that
     * says everything there is to say. */
    if(output_format() != "gdi") {
        std::vector<std::string> present;
        for(const auto& [option, disable_option, filename]: files) {
            (void) disable_option;
            if(OPTS.count(option)) {
                present.push_back(filename);
            }
        }

        return present.empty() ? "none" : join(present, ", ");
    }

    /* All three are reported, each with where its contents come from: with
     * three independent ways for a file to be there or not, a summary would
     * take longer to decode than the three states themselves. */
    std::vector<std::string> described;
    for(const auto& [option, disable_option, filename]: files) {
        std::string state = "generated";
        if(OPTS.count(disable_option)) {
            state = "disabled";
        } else if(OPTS.count(option)) {
            state = "from " + first_value(option);
        }

        described.push_back(std::string(filename) + " " + state);
    }

    return join(described, ", ");
}

/* The first session of a MIL-CD, which is audio unless the layout has no
 * audio session at all. */
std::string audio_session() {
    if(disc_layout() != DiscLayout::AudioData) {
        return "none, this layout has no audio session";
    }
    if(!OPTS.count("cdda-track")) {
        return "one blank 4-second track";
    }

    return counted(times_given("cdda-track"), "CDDA track", "CDDA tracks")
        + " (" + join(OPTS["cdda-track"], ", ") + ")";
}

std::string data_track_padding() {
    return OPTS.count("disable-data-track-padding")
        ? "disabled, the track stops after its contents"
        : "enabled, the track is padded out to the full CD size";
}

std::string high_density_track() {
    return OPTS.count("gdi-truncate")
        ? "truncated to its contents (non-standard: needs an ODE or emulator that accepts it)"
        : "padded out to the full GD-ROM size";
}

std::string data_track_sector_size() {
    return gdi_sector_size() == CD_GDI_SECTOR_SIZE_COOKED
        ? "2048 bytes, plain user data (track??.iso)"
        : "2352 bytes, raw Mode 1 (track??.bin)";
}

/* The metadata block, and the contents it describes: everything that only
 * exists when mkdcdisc builds the filesystem and the IP.BIN itself. A
 * -t/--external-data-track run has none of it; mkisofs did all of that, and
 * every option below is refused there. */
void print_standard_mode_plan() {
    const bool gdrom = output_format() == "gdi";

    IPBinMetadata meta = planned_ip_bin_metadata(
        gdrom ? gd_hda_template() : IP_BIN_MILCD,
        gdrom ? gd_hda_template_len() : IP_BIN_MILCD_len);

    field("Volume label", volume_label());
    field("Boot filename", meta.boot_filename);
    field("Main executable", main_executable());
    field("Executable scrambling", executable_scrambling());
    field("Bootstrap (IP.BIN)", bootstrap_source(meta));
    if(gdrom) {
        field("FPU rounding", fpu_rounding(meta));
    }
    field("License screen", license_screen(meta));

    field("Software title", meta.title);
    field("Author", meta.author);
    field("Serial", meta.serial.empty() ? "generated from the boot binary" : meta.serial);
    field("Product version", meta.product_version);
    field("Release date", meta.release_date);
    field("Peripherals", meta.peripherals);

    field("Disc contents", contents_summary("directory", "directory-contents", "file"));
    field("Sort file", OPTS.count("sort-file") ? first_value("sort-file") : "none");
    field("Volume text files", volume_text_files());

    if(gdrom) {
        field("Low-density contents",
            contents_summary("gdi-lda-directory", "gdi-lda-directory-contents", "gdi-lda-file"));
        field("Low-density sort file",
            OPTS.count("gdi-lda-sort-file") ? first_value("gdi-lda-sort-file") : "none");
        field("Low-density audio",
            OPTS.count("gdi-lda-cdda-track") ? first_value("gdi-lda-cdda-track") : "silence");
        field("GD-DA tracks", OPTS.count("cdda-track")
            ? counted(times_given("cdda-track"), "track", "tracks")
                + " (" + join(OPTS["cdda-track"], ", ") + ")"
            : "none");
        field("High-density track", high_density_track());
    } else {
        field("Audio session", audio_session());
        field("Data track padding", data_track_padding());
    }
}

void print_build_plan() {
    logging::debug() << "Build plan:";

    field("Disc type", output_format() == "gdi" ? "GD-ROM" : "MIL-CD");
    field("Image format", image_format_name());
    field("Image layout", image_layout_name());
    field("Output", output_files_description(first_value("output")));

    if(OPTS.count("dump-iso")) {
        field("Extra output", "the data track, also written as a standalone .iso");
    }

    if(output_format() == "gdi") {
        field("Data track sectors", data_track_sector_size());
    }

    if(OPTS.count("external-data-track")) {
        /* Two ISOs under -L/--disc-layout merged-data, one otherwise; both were
         * checked before this ran, so what is named here exists. */
        const auto& isos = OPTS["external-data-track"];

        if(isos.size() == 2) {
            field("Session 1 data track", isos[0] + " (pre-built ISO)");
            field("Session 2 data track", isos[1] + " (pre-built ISO)");
        } else {
            field("Data track", isos[0] + " (pre-built ISO, with its own IP.BIN)");
        }

        field("Audio session", audio_session());
    } else {
        field("Data track", "built by mkdcdisc from the files below");
        print_standard_mode_plan();
    }

    /* A blank line: the plan is a block to be read (or pasted into a bug
     * report) as one, not the first few lines of the build log. */
    logging::debug() << "";
}

}

void announce_build_plan() {
    /* Asking for the plan is not free, it reads the -p/--bootstrap file, and
     * resolving the boot filename can warn about what it finds, so nothing
     * below -v 3 pays for a block it would not print. */
    if(logging::enabled(logging::Level::Debug)) {
        print_build_plan();
    }

    logging::verbose() << "Pre-run checks finished, beginning generation";
}
