/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Command-line surface: the option table, the parser, the help/banner
 * rendering, and the accessors every other module uses to read what was
 * asked for. */

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

/* Parsed options, keyed by long flag without the leading dashes. Populated
 * once by main() from parse_options() and read from everywhere afterwards.
 *
 * A global rather than a value threaded through every builder: it is read
 * from nearly every function in the program, and passing it explicitly is a
 * separate change from splitting the file it used to live in. */
extern std::map<std::string, std::vector<std::string>> OPTS;

std::map<std::string, std::vector<std::string>> parse_options(int argc, char* argv[], bool* ok);

/* Which of the two disc types an option applies to. Most apply to both, and
 * say nothing about it; the rest are declared in the option table, which is
 * then the only place that knows -- --help marks them from it, and main()
 * refuses them from it, instead of each keeping a list of its own that the
 * other could drift away from. */
enum class DiscType {
    Any,
    GdRomOnly,  /* -F gdi; by convention the name carries the --gdi- prefix */
    MilCdOnly,  /* -F cdi/mds/nrg, i.e. anything but -F gdi */
};

/* An option that only applies to one disc type, as main() needs to see it. The
 * option table itself stays private to cli.cpp: what validation wants is the
 * OPTS key to look up, the name to print, and what to suggest instead. */
struct DiscTypeRestriction {
    std::string key;    /* the OPTS key: the long flag without its dashes */
    std::string label;  /* how a diagnostic should name it, e.g. "-t/--external-data-track" */
    std::string hint;   /* the GD-ROM equivalent to point at, "" when there is none */
    DiscType disc_type;
};

const std::vector<DiscTypeRestriction>& disc_type_restricted_options();

/* Which arrangement of sessions -L/--disc-layout asks for. MIL-CD only: a
 * GD-ROM's two zones are what the format is rather than a choice made about
 * it, which is why the option is declared MilCdOnly in the table.
 *
 * One value rather than a flag per layout: the three are mutually exclusive,
 * and separate flags could also say "both" -- a state that is not a disc, and
 * that every place reading them would have to refuse by hand. */
enum class DiscLayout {
    AudioData,   /* the default: an audio session, then the data session */
    DataData,    /* the full ISO, then a duplicated boot-head session */
    MergedData,  /* two full pre-built ISOs, both supplied via -t */
};

/* Option accessors, each applying the option's default when it is absent.
 *
 * The name of the main executable is deliberately not among them: it is not
 * an option's answer alone, since a -p/--bootstrap IP.BIN declares one too.
 * See boot_filename() in ip_boot.hpp. */
int verbosity();
std::string output_format();
uint32_t gdi_sector_size();
DiscLayout disc_layout();

/* The two ways a layout is written about: the value -L/--disc-layout takes for
 * it ("data-data"), and the name a diagnostic or the build plan gives it
 * ("Data/Data"). Both come from one table, so what a message tells the user to
 * type and what it calls the result cannot drift apart. */
std::string disc_layout_value(DiscLayout layout);
std::string disc_layout_name(DiscLayout layout);

/* Refuses an unrecognized -L/--disc-layout value, having said which three
 * there are. Call after OPTS has been assigned and before anything reads
 * disc_layout(), -M/--print-msinfo included -- it answers and exits before the
 * other option-value checks in main() are reached. */
bool validate_disc_layout();

/* SOURCE_DATE_EPOCH, the cross-ecosystem convention for reproducible builds:
 * a whole number of seconds since the Unix epoch, standing in for every "now"
 * that would otherwise reach the image -- the ISO9660 volume timestamps, the
 * per-file timestamps, and the IP.BIN release date when -r/--release does not
 * give one.
 *
 * Unset, mkdcdisc stamps the current time, which is what it has always done.
 * Set to something that is not a number of seconds, it says so and carries on
 * as if unset: a build should not fail over an environment variable it did not
 * ask for.
 *
 * Resolved once, so that one run cannot straddle two answers, and warned about
 * once for the same reason. */
std::optional<std::time_t> source_date_epoch();

/* Warns on stderr about any superseded option spelling used on the command
 * line. Call after OPTS has been assigned, so the warning honours -v/-q. */
void warn_about_deprecated_options();

/* Hands --color/--disable-color to the terminal module, replacing the
 * decision it made from argv alone in term::configure(). Returns false, having
 * said why, when --color was given something that isn't one of its three
 * values. Call after OPTS has been assigned and before anything else is
 * printed. */
bool apply_color_options();

/* Whether the startup banner should be printed, worked out from argv alone.
 *
 * The banner comes out before parse_options(), so that an argument the parser
 * rejects is reported under it rather than above it; which means it cannot
 * wait for OPTS to tell it whether --no-banner, -q, -v 0 or -M was given. Same
 * lenient look at argv that term::configure() takes at --color, for the same
 * reason; see terminal.hpp. */
bool banner_wanted(int argc, char* argv[]);

void print_banner();

/* The short description plus the usage line, for `mkdcdisc` with no arguments
 * at all; someone who does not yet know what this is. Goes to stdout: it is
 * what that invocation was answered with, not a diagnostic. */
void print_usage_hint();

/* The one line pointing at --help, for an invocation that went wrong in a way
 * already reported just above it. On stderr, next to the complaint it belongs
 * to, and deliberately not the whole option list: a typo does not need forty
 * lines of options scrolling the message that named it off the screen. */
void print_help_hint();

void print_help();
