/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Command-line surface: see cli.hpp. */

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "cli.hpp"
#include "exit_codes.hpp"
#include "logging.hpp"
#include "terminal.hpp"

#include "disc_image/disc_image.h"
#include "version.h"

std::map<std::string, std::vector<std::string>> OPTS;

enum ArgType {
    ARG_TYPE_NAMED_OPTIONAL,
    ARG_TYPE_NAMED_REQUIRED,
    ARG_TYPE_FLAG_OPTIONAL,
    ARG_TYPE_FLAG_REQUIRED,
};

struct OptionSpec {
    std::string short_flag;   /* "" for long-only options (e.g. --no-banner) */
    std::string long_flag;
    std::string description;
    ArgType type;
    std::string group;

    /* Superseded spellings still accepted, resolved to long_flag by
     * normalize_command(). They are deliberately absent from --help: they
     * exist so scripts written against an older release keep working, not as
     * a second name to choose from. Using one prints a warning (see
     * DEPRECATED_USED and warn_about_deprecated_options()). */
    std::vector<std::string> aliases = {};

    /* The disc type this option applies to, when it isn't both. Stated here
     * and nowhere else: print_help() marks the option from it and main()
     * refuses it from it, so the help text and the diagnostics cannot end up
     * disagreeing about which format an option belongs to. */
    DiscType disc_type = DiscType::Any;

    /* What to reach for instead, appended to the refusal when the other disc
     * type has an equivalent. */
    std::string disc_type_hint = "";
};

/* PNG support is optional at build time (meson.build defines
 * MKDCDISC_WITH_PNG when libpng is found), so -i's description states what
 * this binary actually accepts instead of hedging: a build without libpng
 * rejects a PNG outright, and its help should say so before the user tries. */
#ifdef MKDCDISC_WITH_PNG
static const char* MR_IMAGE_DESCRIPTION = "MR or PNG image for the license screen";
#else
static const char* MR_IMAGE_DESCRIPTION = "MR image for the license screen; this build has no PNG support";
#endif

/* Descriptions are deliberately terse: one line, two at most. --help is a
 * reminder of what each option is, not its documentation; the full behaviour,
 * interactions and caveats live in docs/cli-reference.md and the topic docs it
 * links to, which ship alongside the binary.
 *
 * They also say nothing about which disc type an option applies to: that is
 * the disc_type field's job, and print_help() writes the "-F gdi only:" and
 * "(not with -F gdi)" markers from it. */
const std::vector<OptionSpec> COMMANDS = {
    /* General */
    {"-h", "--help", "print this help screen", ARG_TYPE_FLAG_OPTIONAL, "General"},
    {"-v", "--verbosity", "log level: '0' for silent, '1' for one line per build step, '2' adds each file and track handled, '3' adds the build plan and the internals; defaults to '1'", ARG_TYPE_NAMED_OPTIONAL, "General"},
    {"-q", "--quiet", "print nothing at all; same as '-v 0'", ARG_TYPE_FLAG_OPTIONAL, "General"},
    {"", "--no-banner", "suppress the startup banner", ARG_TYPE_FLAG_OPTIONAL, "General"},
    {"", "--color", "colorize the output: 'auto' (only on a terminal), 'always' or 'never'; defaults to 'auto'", ARG_TYPE_NAMED_OPTIONAL, "General"},
    {"", "--disable-color", "shorthand for '--color never'", ARG_TYPE_FLAG_OPTIONAL, "General"},
    {"", "--temp-dir", "directory to stage the disc contents in; defaults to the system temporary directory", ARG_TYPE_NAMED_OPTIONAL, "General"},

    /* Output Control */
    {"-o", "--output", "output filename", ARG_TYPE_NAMED_REQUIRED, "Output Control"},
    {"-F", "--format", "output format: 'cdi', 'mds', 'nrg' or 'gdi'; inferred from the output file extension when omitted; defaults to 'cdi'", ARG_TYPE_NAMED_OPTIONAL, "Output Control"},
    {"", "--allow-overwrite", "write over the output files when they already exist, instead of refusing", ARG_TYPE_FLAG_OPTIONAL, "Output Control"},
    {"-I", "--dump-iso", "also write the data track to a separate .iso", ARG_TYPE_FLAG_OPTIONAL, "Output Control", {}, DiscType::MilCdOnly},
    {"", "--gdi-truncate", "don't pad the high-density track to full GD-ROM size; smaller, but non-standard: only boots from an ODE or emulator that accepts it", ARG_TYPE_FLAG_OPTIONAL, "Output Control", {}, DiscType::GdRomOnly},
    {"", "--gdi-sector-size", "data track sector size, '2352' (track??.bin) or '2048' (track??.iso); defaults to 2352", ARG_TYPE_NAMED_OPTIONAL, "Output Control", {}, DiscType::GdRomOnly},

    /* Main Executable (1ST_READ.BIN) */
    {"-e", "--main-elf", "executable file in ELF format", ARG_TYPE_NAMED_OPTIONAL, "Main Executable (1ST_READ.BIN)", {"--elf"}},
    {"-b", "--main-binary-unscrambled", "executable file in unscrambled binary format", ARG_TYPE_NAMED_OPTIONAL, "Main Executable (1ST_READ.BIN)", {"--unscrambled-binary", "--main-bin-unscrambled"}},
    {"-B", "--main-binary-scrambled", "executable file in scrambled binary format", ARG_TYPE_NAMED_OPTIONAL, "Main Executable (1ST_READ.BIN)", {"--scrambled-binary", "--main-bin-scrambled"}},
    {"", "--boot-filename", "name of the main executable on disc; defaults to '1ST_READ.BIN'", ARG_TYPE_NAMED_OPTIONAL, "Main Executable (1ST_READ.BIN)"},

    /* Disc Metadata Bootstrap (IP.BIN) */
    {"-p", "--bootstrap", "bootstrap file (IP.BIN) to use instead of the built-in one", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)", {"--ipbin"}},
    {"-a", "--author", "author/company that made the disc/software", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)"},
    {"-n", "--title", "title of the software", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)", {"--name"}},
    {"-s", "--serial", "disc serial; defaults to 'IND-' plus a boot binary hash", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)"},
    {"-P", "--product-version", "product version, in Vx.yyy format; defaults to V1.000", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)"},
    {"-r", "--release", "release date in YYYYMMDD format; defaults to today", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)"},
    {"-H", "--peripherals", "peripheral support: 5 hex digits plus VGA and Windows CE flags, e.g. 'E000F10'; defaults to the bootstrap's value", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)"},
    {"-i", "--bootstrap-mr-image", MR_IMAGE_DESCRIPTION, ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)", {"--image"}},
    {"-m", "--disable-bootstrap-mr", "embed no license screen image at all; takes precedence over -i", ARG_TYPE_FLAG_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)", {"--no-mr"}},
    {"", "--disable-bootstrap-checks", "build even when the disc fails the checks that say it can boot; the failures are reported as warnings instead", ARG_TYPE_FLAG_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)"},
    {"", "--gdi-fpu-rounding", "FPU rounding mode the bootstrap leaves set, 'nearest' (the default) or 'zero'", ARG_TYPE_NAMED_OPTIONAL, "Disc Metadata Bootstrap (IP.BIN)", {}, DiscType::GdRomOnly},

    /* Data Track Contents */
    {"-d", "--directory", "add a directory tree, root included; repeat for multiple", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"-D", "--directory-contents", "add a directory tree, root excluded; repeat for multiple", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"-f", "--file", "add a single file; repeat for multiple files", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"-S", "--sort-file", "path to sort file", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"-N", "--disable-data-track-padding", "disable data track padding (padding optimizes loading speed)", ARG_TYPE_FLAG_OPTIONAL, "Data Track Contents", {"--no-padding"}, DiscType::MilCdOnly, " Use --gdi-truncate instead."},
    {"", "--duplicates-once", "write byte-identical files once for optimizing storage", ARG_TYPE_FLAG_OPTIONAL, "Data Track Contents"},
    {"-V", "--volume-label", "ISO9660 volume label; sanitized. Defaults to -n/--title", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"", "--copyright", "file to use as COPYRIGH.TXT; with -F gdi it defaults to a line built from -n/-a/-r", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"", "--abstract", "likewise for ABSTRACT.TXT", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"", "--biblio", "likewise for BIBLIOGR.TXT", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents"},
    {"", "--gdi-disable-copyright", "leave COPYRIGH.TXT off the disc entirely; takes precedence over --copyright", ARG_TYPE_FLAG_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},
    {"", "--gdi-disable-abstract", "likewise for ABSTRACT.TXT", ARG_TYPE_FLAG_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},
    {"", "--gdi-disable-biblio", "likewise for BIBLIOGR.TXT", ARG_TYPE_FLAG_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},
    {"", "--gdi-lda-directory", "as -d, but into the low-density area", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},
    {"", "--gdi-lda-directory-contents", "as -D, but into the low-density area", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},
    {"", "--gdi-lda-file", "as -f, but into the low-density area", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},
    {"", "--gdi-lda-sort-file", "as -S, but for the low-density area", ARG_TYPE_NAMED_OPTIONAL, "Data Track Contents", {}, DiscType::GdRomOnly},

    /* Disc Layout. Every option here is MIL-CD only -- a GD-ROM has one fixed
     * structure and nothing to choose -- so the group description says it once
     * for all three, and print_help() leaves the per-option marker off. */
    {"-L", "--disc-layout", "session arrangement: 'audio-data' (the default), 'data-data', or 'merged-data', whose second session is a full merged ISO and which requires two -t ISOs", ARG_TYPE_NAMED_OPTIONAL, "Disc Layout", {}, DiscType::MilCdOnly},
    {"-M", "--print-msinfo", "print the data-track start LBA to feed mkisofs as -C 0,MSINFO, then exit", ARG_TYPE_FLAG_OPTIONAL, "Disc Layout", {}, DiscType::MilCdOnly},

    /* Disc Tracks */
    {"-c", "--cdda-track", "audio track (.wav or raw CD-DA); repeat for multiple tracks", ARG_TYPE_NAMED_OPTIONAL, "Disc Tracks", {"--cdda"}},
    {"", "--gdi-lda-cdda-track", "audio for the low-density CDDA track (track 2); defaults to silence", ARG_TYPE_NAMED_OPTIONAL, "Disc Tracks", {}, DiscType::GdRomOnly},
    {"-t", "--external-data-track", "use a pre-built ISO as the data track; disables the data track and IP.BIN options. In 'merged-data', give it twice, once per session", ARG_TYPE_NAMED_OPTIONAL, "Disc Tracks", {"--external-iso-track"}, DiscType::MilCdOnly},
};

int verbosity() {
    if(OPTS.count("quiet")) {
        return 0;
    }

    auto level = (OPTS.count("verbosity")) ? OPTS["verbosity"][0] : "1";
    try {
        return std::stoi(level);
    } catch(...) {
        /* Refused like any other bad option value, pointer included, the
         * one such refusal that cannot go through main()'s usage_error(),
         * because the level is read before main() gets as far as validating
         * anything (the deprecation warnings already honour it). */
        std::cerr << term::error << "invalid verbosity value: " << level
                  << " (expected a number between 0 and 3)" << std::endl;
        print_help_hint();
        exit(exit_code::USAGE);
    }
}

/* See cli.hpp. Read from the environment rather than from OPTS because that
 * is where the convention puts it: build systems set it once, for every tool
 * in a build, and a tool that needed its own flag would not be reached by
 * them. */
std::optional<std::time_t> source_date_epoch() {
    static const std::optional<std::time_t> resolved = []() -> std::optional<std::time_t> {
        const char* raw = std::getenv("SOURCE_DATE_EPOCH");
        if(!raw || !*raw) {
            return std::nullopt;
        }

        errno = 0;
        char* end = nullptr;
        long long value = std::strtoll(raw, &end, 10);

        if(errno != 0 || end == raw || *end != '\0' || value < 0) {
            std::cerr << term::warning << "SOURCE_DATE_EPOCH is not a whole number of seconds "
                "since the Unix epoch (\"" << raw << "\"); ignoring it, and stamping the image "
                "with the current time as usual." << std::endl;
            return std::nullopt;
        }

        return (std::time_t) value;
    }();

    return resolved;
}

/* Returns "cdi", "mds", "nrg", or "gdi". The --format option wins; otherwise
 * the format is inferred from the output file extension, defaulting to cdi. */
std::string output_format() {
    if(OPTS.count("format")) {
        std::string fmt = OPTS["format"][0];
        std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);
        return fmt;
    }

    /* -M/--print-msinfo is answered before -o/--output is required, so there
     * may be no filename to infer from; with neither, the default stands. */
    if(!OPTS.count("output")) {
        return "cdi";
    }

    std::string ext = std::filesystem::path(OPTS["output"][0]).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if(ext == ".mds" || ext == ".mdf") {
        return "mds";
    }

    if(ext == ".nrg") {
        return "nrg";
    }

    if(ext == ".gdi") {
        return "gdi";
    }

    return "cdi";
}

/* Data-track sector size for -F gdi. Raw 2352-byte Mode 1 sectors unless
 * --gdi-sector-size asks for plain 2048-byte ones; the value itself is
 * validated in main() before any building starts. */
uint32_t gdi_sector_size() {
    if(OPTS.count("gdi-sector-size") && OPTS["gdi-sector-size"][0] == "2048") {
        return CD_GDI_SECTOR_SIZE_COOKED;
    }

    return CD_GDI_SECTOR_SIZE_RAW;
}

/* The three MIL-CD layouts, each with the value -L/--disc-layout takes for it
 * and the name everything else calls it by. One table for both, so what a
 * message tells the user to type and what it calls the result stay in step,
 * and so a fourth layout is one row here rather than an edit to each of the
 * places below. */
struct DiscLayoutSpec {
    DiscLayout layout;
    const char* value;
    const char* name;
};

static const std::vector<DiscLayoutSpec> DISC_LAYOUTS = {
    {DiscLayout::AudioData,  "audio-data",  "Audio/Data"},
    {DiscLayout::DataData,   "data-data",   "Data/Data"},
    {DiscLayout::MergedData, "merged-data", "Merged Data/Data"},
};

static const DiscLayoutSpec& disc_layout_spec(DiscLayout layout) {
    for(const auto& spec: DISC_LAYOUTS) {
        if(spec.layout == layout) {
            return spec;
        }
    }

    /* Unreachable: every enumerator has a row above, and the enum is not
     * built from anything but them. */
    assert(false);
    return DISC_LAYOUTS[0];
}

/* Absent, the default stands. An unrecognized value cannot get this far --
 * validate_disc_layout() refuses it before main() reads the layout -- but it
 * still answers with the default rather than reading past the table, so a path
 * that somehow skipped the check builds the documented disc instead of an
 * undefined one. */
DiscLayout disc_layout() {
    if(!OPTS.count("disc-layout")) {
        return DiscLayout::AudioData;
    }

    for(const auto& spec: DISC_LAYOUTS) {
        if(OPTS["disc-layout"][0] == spec.value) {
            return spec.layout;
        }
    }

    return DiscLayout::AudioData;
}

std::string disc_layout_value(DiscLayout layout) {
    return disc_layout_spec(layout).value;
}

std::string disc_layout_name(DiscLayout layout) {
    return disc_layout_spec(layout).name;
}

/* See cli.hpp. The list of valid values is built from the table rather than
 * spelled out again, on the same grounds as the table itself. */
bool validate_disc_layout() {
    if(!OPTS.count("disc-layout")) {
        return true;
    }

    const std::string& value = OPTS["disc-layout"][0];
    for(const auto& spec: DISC_LAYOUTS) {
        if(value == spec.value) {
            return true;
        }
    }

    std::ostringstream expected;
    for(size_t i = 0; i < DISC_LAYOUTS.size(); ++i) {
        if(i > 0) {
            expected << (i + 1 == DISC_LAYOUTS.size() ? " or " : ", ");
        }
        expected << "'" << DISC_LAYOUTS[i].value << "'";
    }

    std::cerr << term::error << "invalid -L/--disc-layout value: " << value
              << " (expected " << expected.str() << ")" << std::endl;
    return false;
}

/* --color wins over --disable-color when both are given, on the grounds that
 * the one carrying a value is the one that was meant. */
bool apply_color_options() {
    if(OPTS.count("disable-color")) {
        term::set_color_mode(term::ColorMode::Never);
    }

    if(OPTS.count("color")) {
        const std::string& when = OPTS["color"][0];

        term::ColorMode mode;
        if(!term::parse_color_mode(when, &mode)) {
            std::cerr << term::error << "invalid --color value: " << when
                      << " (expected 'auto', 'always' or 'never')" << std::endl;
            return false;
        }

        term::set_color_mode(mode);
    }

    return true;
}


/* Every way of naming what goes on the disc, since choosing between them is
 * the first decision a run has to make and the usage line was only showing
 * one of them. -b and -B are grouped: they take the same kind of file and
 * differ in whether it is already scrambled, which is a distinction for the
 * option list rather than for a line meant to be scanned. */
const char* HELP_TEXT_USAGE =
    "mkdcdisc [OPTION]... [-e ELF | -b|-B BIN | -t ISO] -o OUTPUT";

/* One paragraph per entry, printed with a blank line between them. The first is
 * the one-line answer to what the tool is; the second fills in the detail --
 * which formats it writes, and what an image file is good for once it exists,
 * which nothing in the option list below ever gets to.
 *
 * "self-booting" qualifies the MIL-CD half only: that is the hack it names, and
 * a GD-ROM image boots because it is a GD-ROM, not because of it. */
const std::vector<std::string> HELP_TEXT_DESCRIPTION = {
    "A modern command-line interface (CLI) tool for generating disc images for the Sega Dreamcast.",

    "This tool creates a GD-ROM (.gdi) or a self-booting MIL-CD image in Padus DiscJuggler (.cdi), "
    "Alcohol 120% (.mds/.mdf), or Nero Burning ROM (.nrg) format. These images can be used directly "
    "on an emulator or via a virtual drive, or on a physical console via a burned CD-R or an "
    "optical drive emulator (ODE).",
};

const std::vector<std::string> HELP_TEXT_EXAMPLES = {
    "mkdcdisc -e game.elf -o game.cdi",
    "mkdcdisc -e game.elf -d data/ -n \"My Game\" -o game.cdi",
    "mkdcdisc -t data.iso -c track01.wav -o game.mds",
    "mkdcdisc -e game.elf -d data/ -n \"My Game\" -o game.gdi",
};

/* Short blurbs printed under a group heading in the full help, above that
 * group's option list. Each answers what the list below it cannot -- what the
 * group is for, which of its options a run actually needs, or what happens
 * when none of them is given -- rather than restating the heading. */
const std::map<std::string, std::string> GROUP_DESCRIPTIONS = {
    {"General", "These options decide what the tool prints about its own run, and where it does "
                "its work. None of them affect the generated disc image itself."},

    {"Output Control", "These options control the output the tool generates. By default, a MIL-CD "
                       "Audio/Data disc image in Padus DiscJuggler (CDI) format is produced."},

    {"Main Executable (1ST_READ.BIN)", "The program the console runs at boot, given in exactly one "
                                       "of these 3 forms, or in none of them when "
                                       "-t/--external-data-track supplies an ISO that already "
                                       "carries it."},

    {"Disc Metadata Bootstrap (IP.BIN)", "The Initial Program (IP) bootstrap every Dreamcast disc "
                                         "needs: the code that loads the executable, the fields the "
                                         "console reads back, and the customized license screen, if "
                                         "any. This tool carries its own bootstrap, so these options "
                                         "exist to fill its fields in; each one left out keeps what "
                                         "that default bootstrap already has. Its license screen "
                                         "reads \"... is what official games display, but this is "
                                         "100% unofficial and unlicensed.\"; you may replace that MR "
                                         "image with your own, or leave it out entirely."},

    {"Data Track Contents", "What the ISO9660 filesystem holds besides the main executable, which is "
                            "added for you. With -F gdi these fill the high-density area, the "
                            "low-density one having its own --gdi-lda-* options. They are all "
                            "disabled when -t/--external-data-track is used, as that ISO is already "
                            "built."},

    {"Disc Layout", "In MIL-CD mode, an Audio/Data disc is generated by default, the only layout "
                    "that supports CD-DA audio tracks (see -c/--cdda-track). -F gdi has no layout "
                    "choice: a GD-ROM always has the same structure."},

    {"Disc Tracks", "The tracks around the data one: CD-DA audio, either in the MIL-CD audio session "
                    "or inside a GD-ROM's high-density zone. -t/--external-data-track is the "
                    "exception: it replaces the data track itself with an ISO built elsewhere."},
};



/* Prints a short startup banner to stderr so it never pollutes
 * machine-readable stdout output (e.g. -M/--print-msinfo). */
/* See cli.hpp. Not a second option parser: all it has to catch is one of the
 * ways of asking for silence, plus the one mode that answers a question instead
 * of building. Anything it doesn't recognize is left to parse_options().
 *
 * It errs towards printing. A banner that shows up when it was not wanted is
 * a cosmetic annoyance; one suppressed by a misread argument looks like a
 * program that failed to start. */
bool banner_wanted(int argc, char* argv[]) {
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if(arg == "--no-banner" || arg == "-q" || arg == "--quiet") {
            return false;
        }

        /* -M/--print-msinfo prints one number and exits. Redirecting it was
         * never affected -- the number goes to stdout and the banner to stderr
         * -- but on a terminal, where the number is there to be read off and
         * pasted into an mkisofs command, three lines introducing the program
         * are just in the way. -h/--help keeps its banner: that output is a
         * screen the program is talking through, not a value. */
        if(arg == "-M" || arg == "--print-msinfo") {
            return false;
        }

        /* -v 0 is the same level as -q, and the value is a separate argument,
         * the only form the real parser accepts either. Any level that
         * means silence counts, not just the "0" spelling; a value that isn't
         * a number at all is left for verbosity() to reject, which it now
         * does under the banner like every other diagnostic. */
        if((arg == "-v" || arg == "--verbosity") && i + 1 < argc) {
            try {
                if(std::stoi(argv[i + 1]) <= 0) {
                    return false;
                }
            } catch(...) {
            }
        }
    }

    return true;
}

void print_banner() {
    std::cerr << term::styled(term::Style::Heading, "mkdcdisc v" MKDCDISC_VERSION)
              << " - Sega Dreamcast Disc Image Generator\n"
              << MKDCDISC_COPYRIGHT << "\n\n";
}

/* Print text word-wrapped to fit within width columns, with every line but
 * the first indented by indent spaces (the caller is expected to already be
 * positioned at that column for the first line). */
static void print_wrapped(const std::string& text, size_t indent, size_t width) {
    std::istringstream words(text);
    std::string word;
    size_t col = indent;
    bool line_start = true;

    while(words >> word) {
        size_t needed = word.size() + (line_start ? 0 : 1);

        if(!line_start && col + needed > width) {
            std::cout << "\n" << std::string(indent, ' ');
            col = indent;
            line_start = true;
            needed = word.size();
        }

        if(!line_start) {
            std::cout << " ";
        }

        std::cout << word;
        col += needed;
        line_start = false;
    }
}

/* The description paragraphs, blank line between them and one after, so what
 * follows -- the usage line, in both callers -- starts where it would have
 * with the single paragraph this used to be. */
static void print_description() {
    for(size_t i = 0; i < HELP_TEXT_DESCRIPTION.size(); ++i) {
        if(i > 0) {
            std::cout << "\n\n";
        }

        print_wrapped(HELP_TEXT_DESCRIPTION[i], 0, term::WRAP_WIDTH);
    }

    std::cout << "\n\n";
}

/* Whether a whole group is MIL-CD only. Such a group says so once in its
 * description, and its options are then left unmarked: repeating "(not with -F
 * gdi)" on every line of "Disc Layout" would be three copies of a heading the
 * reader has just read. Derived rather than declared, so a group that later
 * gains an option applying to both starts marking the others again. */
static bool group_is_milcd_only(const std::string& group) {
    bool any = false;

    for(const auto& cmd: COMMANDS) {
        if(cmd.group != group) {
            continue;
        }
        if(cmd.disc_type != DiscType::MilCdOnly) {
            return false;
        }
        any = true;
    }

    return any;
}

/* An option's description as --help shows it: what the table says, plus the
 * markers derived from the fields around it. "-F gdi only:" leads, because it
 * says which half of the program the line even belongs to; "(not with -F gdi)"
 * and "(required)" trail, being footnotes to a description that stands without
 * them. */
static std::string described(const OptionSpec& cmd) {
    std::string description = cmd.description;

    if(cmd.disc_type == DiscType::GdRomOnly) {
        description = "-F gdi only: " + description;
    }

    if(cmd.type == ARG_TYPE_NAMED_REQUIRED) {
        description += " (required)";
    }

    if(cmd.disc_type == DiscType::MilCdOnly && !group_is_milcd_only(cmd.group)) {
        description += " (not with -F gdi)";
    }

    return description;
}

/* See cli.hpp. Built once, on first use: main() walks it twice per run, and
 * the table it comes from never changes. */
const std::vector<DiscTypeRestriction>& disc_type_restricted_options() {
    static const std::vector<DiscTypeRestriction> restricted = [] {
        std::vector<DiscTypeRestriction> out;

        for(const auto& cmd: COMMANDS) {
            if(cmd.disc_type == DiscType::Any) {
                continue;
            }

            out.push_back({
                cmd.long_flag.substr(cmd.long_flag.find_first_not_of("-")),
                cmd.short_flag.empty() ? cmd.long_flag : cmd.short_flag + "/" + cmd.long_flag,
                cmd.disc_type_hint,
                cmd.disc_type,
            });
        }

        return out;
    }();

    return restricted;
}

/* Written once, so the two places that point at --help point at it in the same
 * words. They differ only in which stream they say it on, and in what they say
 * before it. */
static const char* HELP_POINTER = "Try 'mkdcdisc --help' for the list of valid options.";

/* Prints just the description and usage line, plus a pointer to --help, for
 * the case where mkdcdisc is invoked with no arguments at all. */
void print_usage_hint() {
    print_description();
    std::cout << term::styled(term::Style::Heading, "Usage:") << " "
              << HELP_TEXT_USAGE << "\n\n";
    std::cout << HELP_POINTER << std::endl;
}

void print_help_hint() {
    /* Set off by a blank line: it is an aside about where to look next, not
     * part of the complaint above it, and a diagnostic long enough to have
     * wrapped would otherwise run straight into it. */
    std::cerr << "\n" << HELP_POINTER << std::endl;
}

void print_help() {
    print_description();
    std::cout << term::styled(term::Style::Heading, "Usage:") << " "
              << HELP_TEXT_USAGE << "\n\n";

    size_t max_width = 0;
    for(auto& cmd: COMMANDS) {
        max_width = std::max(cmd.long_flag.size(), max_width);
    }

    /* Column at which descriptions start: "  " + flag (2) + ", " + the
     * setw(max_width + 4) padded long name below. */
    size_t indent = max_width + 10;

    std::string last_group;
    for(auto& cmd: COMMANDS) {
        if(cmd.group != last_group) {
            if(!last_group.empty()) {
                std::cout << std::endl;
            }
            std::cout << term::styled(term::Style::Heading, cmd.group + ":") << std::endl;
            last_group = cmd.group;

            auto group_desc = GROUP_DESCRIPTIONS.find(cmd.group);
            if(group_desc != GROUP_DESCRIPTIONS.end()) {
                print_wrapped(group_desc->second, 0, term::WRAP_WIDTH);
                std::cout << "\n\n";
            }
        }

        std::string flag_col = cmd.short_flag.empty() ? std::string(4, ' ') : cmd.short_flag + ", ";

        std::cout << "  " << flag_col << std::left << std::setw(max_width + 4) << std::setfill(' ') << cmd.long_flag;

        print_wrapped(described(cmd), indent, term::WRAP_WIDTH);
        std::cout << std::endl;
    }

    std::cout << "\n" << term::styled(term::Style::Heading, "Examples:") << "\n";
    for(auto& example: HELP_TEXT_EXAMPLES) {
        std::cout << "  " << example << "\n";
    }
    std::cout << std::endl;
}

/* Deprecated spellings seen on the command line, as (used, canonical) pairs,
 * in the order they were given. Filled by normalize_command() and drained by
 * warn_about_deprecated_options(); the warning cannot be printed as the
 * argument is parsed, because the verbosity it must respect is itself an
 * option that may not have been read yet. */
static std::vector<std::pair<std::string, std::string>> DEPRECATED_USED;

static std::string normalize_command(const std::string& arg) {
    for(auto& t: COMMANDS) {
        if(t.short_flag == arg || t.long_flag == arg) {
            return t.long_flag;
        }

        for(auto& alias: t.aliases) {
            if(alias == arg) {
                DEPRECATED_USED.push_back({arg, t.long_flag});
                return t.long_flag;
            }
        }
    }

    return "";
}

/* Called from main() once OPTS is populated, so verbosity()/--quiet apply.
 * Goes to stderr, like the banner, to keep stdout clean for the
 * machine-readable output of -M/--print-msinfo. */
void warn_about_deprecated_options() {
    if(!logging::enabled(logging::Level::Normal)) {
        return;
    }

    for(auto& [used, canonical]: DEPRECATED_USED) {
        std::cerr << term::warning << "option " << used << " is deprecated, use " << canonical
                  << " instead." << std::endl;
    }
}

/* FIXME: Take required arguments without -- prefixes */
std::map<std::string, std::vector<std::string>> parse_options(int argc, char* argv[], bool* ok) {
    std::map<std::string, std::vector<std::string>> opts;

    assert(ok);
    *ok = true;

    std::string current_arg;
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(current_arg.empty()) {
            /* Awaiting command */
            auto l = arg.length();
            if(l < 2 || arg[0] != '-') {
                std::cerr << term::error << "invalid argument: " << arg << std::endl;
                *ok = false;
                current_arg.clear();
                continue;
            }

            auto normalized = normalize_command(arg);
            if(normalized.empty()) {
                std::cerr << term::error << "invalid argument: " << arg << std::endl;
                *ok = false;
                current_arg.clear();
                continue;
            }

            current_arg = normalized.substr(normalized.find_first_not_of("-"), std::string::npos);

            /* Check is valid */
            bool found = false;
            bool is_flag = false;
            for(auto& cmd: COMMANDS) {
                if(cmd.long_flag == normalized) {
                    found = true;
                    is_flag = cmd.type == ARG_TYPE_FLAG_OPTIONAL || cmd.type == ARG_TYPE_FLAG_REQUIRED;
                    break;
                }
            }

            /* Unreachable as things stand; normalize_command() only ever
             * returns a long_flag out of COMMANDS, which is the table being
             * searched here, but kept as the backstop it was, now refusing
             * the way the two checks above do rather than by exiting from
             * inside the parser with a code of its own. */
            if(!found) {
                std::cerr << term::error << "invalid argument: " << arg << std::endl;
                *ok = false;
                current_arg.clear();
                continue;
            }

            /* Don't expect a value */
            if(is_flag) {
                auto& values = opts[current_arg];
                values.push_back("true");
                current_arg.clear();
            }

        } else {
            /* Awaiting value */
            auto& values = opts[current_arg];
            values.push_back(arg);
            current_arg.clear();
        }
    }

    return opts;
}
