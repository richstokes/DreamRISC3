/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Entry point: parse the options, validate the combinations each mode allows,
 * then hand off to the builder for the requested format. Everything else
 * lives in the modules included below.
 *
 * What it exits with, and what each code promises, is in exit_codes.hpp. */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "audio.hpp"
#include "build_cd.hpp"
#include "build_gd.hpp"
#include "cli.hpp"
#include "disc_layout.hpp"
#include "exit_codes.hpp"
#include "gd_track_plan.hpp"
#include "input_files.hpp"
#include "interrupt.hpp"
#include "logging.hpp"
#include "output_files.hpp"
#include "plan.hpp"
#include "temp_dir.hpp"
#include "terminal.hpp"

#include "disc_image/disc_image.h"

#include "temp_dir.hpp"


/* Refusing the run over the way it was invoked: the caller has already named
 * what is wrong, so all that is left is to point at where the valid spellings
 * are and exit with the usage code.
 *
 * A function rather than two lines repeated at each of the twenty places
 * that refuse an invocation, which is how all but three of them came to print
 * no pointer at all -- `return usage_error();` is hard to write half of. File
 * errors deliberately do not go through it: a missing ISO is not something
 * --help has anything to say about. */
static int usage_error() {
    print_help_hint();
    return exit_code::USAGE;
}

int main(int argc, char* argv[]) {
    /* Before anything is printed, including by the parser below: what comes
     * out of here is colored or not from the very first line. */
    term::configure(argc, argv);

    /* Likewise before anything is printed: a diagnostic raised in the middle
     * of a build step gets a line of its own from here on. */
    logging::install_diagnostic_guard();

    /* Ahead of the parser, which has diagnostics of its own to print: the
     * banner is the program introducing itself, so it belongs above whatever
     * it then has to say, not below it. That is why what suppresses it
     * (--no-banner, -q, -v 0) is read from argv rather than from OPTS, which
     * does not exist yet -- see banner_wanted(). */
    if(banner_wanted(argc, argv)) {
        print_banner();
    }

    bool args_ok = false;
    OPTS = parse_options(argc, argv, &args_ok);

    if(!apply_color_options()) {
        return usage_error();
    }

    warn_about_deprecated_options();

    if(argc == 1) {
        print_usage_hint();
        return exit_code::USAGE;
    }

    if(OPTS.count("help")) {
        print_help();
        return exit_code::SUCCESS;
    }

    /* The parser has already named what it did not understand, so all that is
     * left to say is where the valid spellings are. Printing the whole option
     * list here instead would scroll that message off the screen with the
     * answer to a question nobody asked. */
    if(!args_ok) {
        return usage_error();
    }

    /* Up here rather than down with the other option-value checks because
     * -M/--print-msinfo reads the layout and then exits, several checks before
     * reaching them. */
    if(!validate_disc_layout()) {
        return usage_error();
    }

    const DiscLayout layout = disc_layout();

    /* Neither Data/Data layout has an audio session for CDDA tracks to go in.
     * Checked once, here, for every path below it: -M/--print-msinfo, a
     * Standard Mode build and an External ISO Mode one. These used to be four
     * near-identical copies, and the builders that caught the last of them only
     * did so once the executable had been loaded and scrambled -- so the run
     * announced two steps and did their work before refusing, exited as a build
     * failure rather than as a usage error, and followed the message with an
     * "unable to build the disc image" that named nothing.
     *
     * -F gdi is left out deliberately: the block further down refuses
     * -L/--disc-layout there outright, and being told the option does not apply
     * to a GD-ROM at all is more use than being told which two of its values
     * conflict with -c. */
    if(output_format() != "gdi" && layout != DiscLayout::AudioData && OPTS.count("cdda-track")) {
        std::cerr << term::error << "--cdda-track cannot be combined with -L/--disc-layout "
                  << disc_layout_value(layout) << ". A " << disc_layout_name(layout)
                  << " image has no audio session for CDDA tracks." << std::endl;
        return usage_error();
    }

    /* How many audio tracks the disc can carry. Two different rules meet in one
     * check: Sega's, which allows gd_track::MAX_GDDA_TRACKS of them in a
     * GD-ROM's high-density area, and the Red Book ceiling every MIL-CD format
     * inherits, which allows CD_MAX_TRACKS tracks in all and spends one of them
     * on the data track. A GD-ROM always puts its audio in the high-density
     * area; a MIL-CD only has an audio session in the Audio/Data layout, the
     * other two having refused --cdda-track just above.
     *
     * Refused here rather than in either builder, which would open every audio
     * file first, and rather than by the writers, which return false on the
     * same ceiling once the whole disc has been built and can then say nothing
     * but that writing failed. The count answers on its own -- which is also
     * why this sits above -M/--print-msinfo rather than with the other
     * format-specific checks: -M answers and exits without ever building, and
     * computing an MSINFO for a disc that cannot be built helps nobody.
     *
     * -F is not validated yet at this point, so an unrecognized one lands on
     * the MIL-CD limit. That is the laxer of the two, so a count this lets
     * through is one the "unknown output format" error further down still gets
     * to report first; a count it stops is wrong whichever format was meant. */
    if(OPTS.count("cdda-track") && (output_format() == "gdi" || layout == DiscLayout::AudioData)) {
        const bool gd_rom = output_format() == "gdi";
        const size_t limit = gd_rom ? gd_track::MAX_GDDA_TRACKS : CD_MAX_TRACKS - 1;
        const size_t given = OPTS["cdda-track"].size();

        if(given > limit) {
            std::cerr << term::error << given << (gd_rom ? " GD-DA" : " CDDA")
                      << " tracks given, but at most " << limit << " fit: "
                      << (gd_rom
                            ? "a GD-ROM's high-density area spans tracks 3 to 99, and the audio "
                              "sits between an opening data track and the closing one"
                            : "a disc holds " + std::to_string(CD_MAX_TRACKS)
                              + " tracks and the data track takes one of them")
                      << "." << std::endl;
            return usage_error();
        }
    }

    /* MSINFO-only mode: compute the data-track start LBA for the given -c tracks
     * (or the Audio/Data default if none) and print just that number, so it can be
     * fed to mkisofs as -C 0,MSINFO before building an ISO for -t. A Data/Data
     * layout has no audio session, so its MSINFO is always 0. */
    if(OPTS.count("print-msinfo")) {
        /* Refused here rather than with the other MIL-CD-only options further
         * down, which -M never reaches: it is answered and exits first. The
         * number it prints exists to be fed to mkisofs for a -t build, and -t
         * cannot be used with -F gdi at all, so a GD-ROM has nothing to ask. */
        if(output_format() == "gdi") {
            std::cerr << term::error << "option -M/--print-msinfo cannot be used with -F gdi. "
                                        "MSINFO belongs to the multi-session MIL-CD layouts." << std::endl;
            return usage_error();
        }

        if(layout == DiscLayout::MergedData) {
            if(!OPTS.count("external-data-track") || OPTS["external-data-track"].size() != 1) {
                std::cerr << term::error << "-M with -L/--disc-layout merged-data requires exactly one "
                                            "-t/--external-data-track (the already-built session 1 ISO)." << std::endl;
                return usage_error();
            }

            std::error_code ec;
            auto iso1_size = std::filesystem::file_size(OPTS["external-data-track"][0], ec);
            if(ec) {
                std::cerr << term::error << "unable to determine size of ISO file: "
                          << OPTS["external-data-track"][0] << std::endl;
                return exit_code::FILE_ERROR;
            }

            std::cout << std::dec << data_session_length_in_sectors(iso1_size, /* is_first_session */ true)
                      << std::endl;
            return exit_code::SUCCESS;
        }

        if(layout == DiscLayout::DataData) {
            std::cout << 0 << std::endl;
            return exit_code::SUCCESS;
        }

        cd_image_t* img = cd_new_image();
        cd_session_t* session0 = cd_new_session(img);
        if(!add_audio_session(session0, /* log */ false)) {
            cd_free_image(&img);
            return exit_code::FILE_ERROR;
        }
        std::cout << std::dec << cd_session_length_in_sectors(session0) << std::endl;
        cd_free_image(&img);
        return exit_code::SUCCESS;
    }

    /* Said outright rather than left for the reader to spot in the option
     * list: this used to print the whole help with no message at all, which
     * named neither what was missing nor that anything was. */
    if(!OPTS.count("output")) {
        std::cerr << term::error << "no output file given. Use -o/--output." << std::endl;
        return usage_error();
    }

    if(output_format() != "cdi" && output_format() != "mds" && output_format() != "nrg" && output_format() != "gdi") {
        std::cerr << term::error << "unknown output format: " << output_format() << " (expected 'cdi', 'mds', 'nrg', or 'gdi')" << std::endl;
        return usage_error();
    }

    /* Fail here rather than mid-build: an unreadable file would otherwise
     * leave the volume quietly missing what was asked for. */
    for(const char* option: {"copyright", "abstract", "biblio"}) {
        if(OPTS.count(option) && !read_file_contents(OPTS[option][0])) {
            std::cerr << term::error << "cannot read --" << option << " file: "
                      << OPTS[option][0] << std::endl;
            return exit_code::FILE_ERROR;
        }
    }

    for(const char* option: {"gdi-lda-directory", "gdi-lda-directory-contents", "gdi-lda-file",
                              "gdi-lda-cdda-track", "gdi-lda-sort-file"}) {
        if(!OPTS.count(option)) {
            continue;
        }

        for(const auto& path: OPTS[option]) {
            if(!std::filesystem::exists(path)) {
                std::cerr << term::error << "no such path for --" << option << ": " << path << std::endl;
                return exit_code::FILE_ERROR;
            }
        }
    }

    /* -F gdi only supports Standard Mode building its own ISO9660
     * filesystem/IP.BIN, in the default Audio/Data-shaped layout. A boot
     * binary given already scrambled (-B) is unscrambled on the way in
     * instead of being rejected -- see gather_files(). */
    if(output_format() == "gdi") {
        /* The MIL-CD-only options, read off the option table rather than
         * listed again here: -F gdi builds its own filesystem and IP.BIN in
         * one fixed structure, so an external ISO, a layout choice and a
         * padding switch have nothing to act on. -N in particular would
         * otherwise be accepted and then quietly ignored, the GD-ROM path
         * doing its own padding -- the worst of both worlds. */
        bool has_conflict = false;
        for(const auto& opt: disc_type_restricted_options()) {
            if(opt.disc_type == DiscType::MilCdOnly && OPTS.count(opt.key)) {
                std::cerr << term::error << "option " << opt.label << " cannot be used with -F gdi."
                          << opt.hint << std::endl;
                has_conflict = true;
            }
        }
        if(has_conflict) {
            return usage_error();
        }

        if(OPTS.count("gdi-sector-size")) {
            const std::string& size = OPTS["gdi-sector-size"][0];
            if(size != "2048" && size != "2352") {
                std::cerr << term::error << "invalid --gdi-sector-size value: " << size
                          << " (expected '2352' for raw Mode 1 sectors or '2048' for plain user data)"
                          << std::endl;
                return usage_error();
            }
        }

        /* Validated here so gd_hda_template() can read the option without a
         * second opinion about what an unrecognized value means. */
        if(OPTS.count("gdi-fpu-rounding")) {
            const std::string& mode = OPTS["gdi-fpu-rounding"][0];
            if(mode != "nearest" && mode != "zero") {
                std::cerr << term::error << "invalid --gdi-fpu-rounding value: " << mode
                          << " (expected 'nearest' or 'zero')" << std::endl;
                return usage_error();
            }

            /* A -p/--bootstrap IP.BIN is used verbatim, bootstrap included, so
             * it brings its own rounding mode and the option cannot apply. */
            if(OPTS.count("bootstrap")) {
                std::cerr << term::warning << "--gdi-fpu-rounding is ignored because -p/--bootstrap "
                    "supplies its own bootstrap." << std::endl;
            }
        }

        /* Disabling a text file wins over supplying one, the way -m does over
         * -i. Said here rather than left silent: the two options were given
         * together, and only one of them can be what was meant. */
        static const std::vector<std::pair<std::string, std::string>> text_file_options = {
            {"copyright", "gdi-disable-copyright"},
            {"abstract",  "gdi-disable-abstract"},
            {"biblio",    "gdi-disable-biblio"},
        };

        for(const auto& [supply, disable]: text_file_options) {
            if(OPTS.count(supply) && OPTS.count(disable)) {
                std::cerr << term::warning << "--" << supply << " is ignored because --" << disable
                          << " was given." << std::endl;
            }
        }
    } else {
        /* Options that only apply to -F gdi, again read off the option table.
         * By convention their names all carry the --gdi- prefix, so `mkdcdisc
         * --help | grep gdi` lists the whole mode in one go. An option
         * restricted to gdi and named without the prefix (as --truncate once
         * was) is a bug -- the e2e suite checks the help text for exactly
         * that. */
        for(const auto& opt: disc_type_restricted_options()) {
            if(opt.disc_type == DiscType::GdRomOnly && OPTS.count(opt.key)) {
                std::cerr << term::error << opt.label << " only applies to -F gdi." << std::endl;
                return usage_error();
            }
        }

    }

    /* merged-data only exists as an External ISO Mode variant: both sessions
     * are pre-built ISOs supplied via -t, there is no Standard Mode equivalent
     * (see the External-ISO mode block below). */
    if(layout == DiscLayout::MergedData && !OPTS.count("external-data-track")) {
        std::cerr << term::error << "-L/--disc-layout merged-data requires -t/--external-data-track, given "
                                    "twice (session 1 ISO, then session 2 ISO). Standard Mode does not support "
                     "this layout." << std::endl;
        return usage_error();
    }

    /* Nothing already on disc is written over unless the run says it may.
     *
     * Here rather than higher up or lower down: which files a run writes follows
     * from -o, -F and -I, and every refusal those three can earn has been made
     * by the time this line is reached -- while below it every path ends in a
     * builder, so this is the one place all three of them pass through before
     * anything is staged, announced or written. Until this check existed the
     * writer simply took whatever -o named and truncated it: a rebuild under the
     * same name, a second format sharing a basename, or a .gdi dropped beside
     * another one's track files destroyed the earlier image in silence, and
     * reported having written this one.
     *
     * A file error rather than a usage one: the options were right, and --help
     * has nothing to say about what is in the output directory. */
    if(!check_output_files(OPTS["output"][0])) {
        return exit_code::FILE_ERROR;
    }

    /* External-ISO mode: delegate filesystem/IP.BIN creation to external tools
     * (mkisofs) and only wrap the resulting ISO into a CDI or MDS/MDF image,
     * like cdi4dc does for CDI. */
    if(OPTS.count("external-data-track")) {
        static const std::vector<std::pair<std::string, std::string>> disabled_in_external_mode = {
            {"author",                 "-a/--author"},
            {"main-binary-unscrambled",   "-b/--main-binary-unscrambled"},
            {"main-binary-scrambled",     "-B/--main-binary-scrambled"},
            {"directory",              "-d/--directory"},
            {"directory-contents",     "-D/--directory-contents"},
            {"main-elf",               "-e/--main-elf"},
            {"file",                   "-f/--file"},
            {"bootstrap-mr-image",     "-i/--bootstrap-mr-image"},
            {"disable-bootstrap-mr",   "-m/--disable-bootstrap-mr"},
            {"dump-iso",               "-I/--dump-iso"},
            {"title",                  "-n/--title"},
            {"disable-data-track-padding", "-N/--disable-data-track-padding"},
            {"bootstrap",              "-p/--bootstrap"},
            {"release",                "-r/--release"},
            {"serial",                 "-s/--serial"},
            {"sort-file",              "-S/--sort-file"},
            {"volume-label",           "-V/--volume-label"},
        };

        bool has_conflict = false;
        for(const auto& [key, label]: disabled_in_external_mode) {
            if(OPTS.count(key)) {
                std::cerr << term::error << "option " << label << " cannot be used with -t/--external-data-track "
                             "(the ISO and its IP.BIN are built externally by mkisofs)." << std::endl;
                has_conflict = true;
            }
        }
        if(has_conflict) {
            return usage_error();
        }

        if(layout == DiscLayout::MergedData) {
            if(OPTS["external-data-track"].size() != 2) {
                std::cerr << term::error << "-L/--disc-layout merged-data requires exactly two "
                                            "-t/--external-data-track ISOs (session 1, then session 2)." << std::endl;
                return usage_error();
            }

            std::filesystem::path iso1_path = OPTS["external-data-track"][0];
            std::filesystem::path iso2_path = OPTS["external-data-track"][1];

            if(!std::filesystem::exists(iso1_path)) {
                std::cerr << term::error << "ISO file does not exist: " << iso1_path << std::endl;
                return exit_code::FILE_ERROR;
            }
            if(!std::filesystem::exists(iso2_path)) {
                std::cerr << term::error << "ISO file does not exist: " << iso2_path << std::endl;
                return exit_code::FILE_ERROR;
            }

            announce_build_plan();

            if(!build_cd_from_merged_data_isos(iso1_path, iso2_path)) {
                std::cerr << term::error << "unable to build the disc image." << std::endl;
                return exit_code::BUILD_FAILED;
            }

            return exit_code::SUCCESS;
        }

        if(OPTS["external-data-track"].size() != 1) {
            std::cerr << term::error << "-t/--external-data-track may only be given once "
                                        "(use -L/--disc-layout merged-data to wrap two ISOs)." << std::endl;
            return usage_error();
        }

        std::filesystem::path iso_path = OPTS["external-data-track"][0];
        if(!std::filesystem::exists(iso_path)) {
            std::cerr << term::error << "ISO file does not exist: " << iso_path << std::endl;
            return exit_code::FILE_ERROR;
        }

        announce_build_plan();

        if(!build_cd_from_external_iso(iso_path)) {
            std::cerr << term::error << "unable to build the disc image." << std::endl;
            return exit_code::BUILD_FAILED;
        }

        return exit_code::SUCCESS;
    }

    /* Reached only once the -t/--external-data-track branch above has been
     * ruled out, so a pre-built ISO is genuinely one of the answers here. */
    if(!OPTS.count("main-elf") && !OPTS.count("main-binary-unscrambled") && !OPTS.count("main-binary-scrambled")) {
        std::cerr << term::error << "no main executable given. Use -e/--main-elf, "
                                    "-b/--main-binary-unscrambled or -B/--main-binary-scrambled -- or "
                                    "-t/--external-data-track to wrap a pre-built ISO." << std::endl;
        return usage_error();
    }

    if(OPTS.count("main-elf") + OPTS.count("main-binary-unscrambled") + OPTS.count("main-binary-scrambled") > 1) {
        std::cerr << term::error << "-e/--main-elf, -b/--main-binary-unscrambled, and -B/--main-binary-scrambled are "
                                    "mutually exclusive; specify only one 1ST_READ.BIN source." << std::endl;
        return usage_error();
    }

    announce_build_plan();

    auto temp_dir_maybe = generate_temp_directory();
    if(!temp_dir_maybe) {
        return exit_code::FILE_ERROR;
    }

    auto temp_dir = temp_dir_maybe.value();
    install_temp_dir_cleanup(temp_dir);

    /* From here on there is real work to interrupt, and a directory that has
     * to be deleted afterwards -- which only works if the work stops first.
     * See interrupt.hpp. */
    install_interrupt_handler();

    /* Catching here (rather than letting an exception propagate out of
     * main()) is what makes the atexit() cleanup above actually run: an
     * uncaught exception reaches std::terminate(), which calls abort() and
     * skips every atexit handler. */
    try {
        bool is_gdi = output_format() == "gdi";

        /* GD-ROM boot requires an unscrambled 1ST_READ.BIN, unlike MIL-CD
         * (CDI/MDS/NRG). */
        if(!gather_files(temp_dir, /* scramble_output */ !is_gdi)) {
            return interrupted() ? interrupted_exit_code() : 4;
        }

        if(!(is_gdi ? build_gd(temp_dir) : build_cd(temp_dir))) {
            /* Told to stop rather than unable to carry on: returning here is
             * what lets the staging directory be cleaned up, which is the
             * whole reason the build stopped where it did. */
            if(interrupted()) {
                std::cerr << "Interrupted." << std::endl;
                return interrupted_exit_code();
            }

            std::cerr << term::error << "unable to build the disc image." << std::endl;
            return exit_code::BUILD_FAILED;
        }
    } catch(const std::exception& e) {
        std::cerr << term::error << e.what() << std::endl;
        return exit_code::BUILD_FAILED;
    } catch(...) {
        std::cerr << term::error << "unknown exception" << std::endl;
        return exit_code::BUILD_FAILED;
    }

    return exit_code::SUCCESS;
}
