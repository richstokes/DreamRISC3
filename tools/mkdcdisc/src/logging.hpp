/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* What each verbosity level is for, and the only way to write a line that
 * honours one.
 *
 * -v/--verbosity takes 0 to 3, and until this module existed each level meant
 * whatever the call site testing `verbosity() > N` happened to think it meant.
 * The contract now is:
 *
 *   0  -q/--quiet   Nothing at all on stdout. Not "less"; nothing. The only
 *                   things that still print are what was explicitly asked for
 *                   (-M/--print-msinfo) and errors, which go to stderr.
 *
 *   1  (default)    One line per phase of the build, and nothing else: what
 *                   mkdcdisc is doing, never how. These are ProgressLines
 *                   (see progress.hpp), so the phases read as a checklist and
 *                   the long ones count themselves up.
 *
 *   2  -v 2         One line per thing handled within a phase: each input
 *                   file loaded, each track added, each option that changed
 *                   what the phase did.
 *
 *   3  -v 3         Internals: sizes, addresses, staging paths, sort weights.
 *                   The level to ask for in a bug report.
 *
 * Levels are cumulative -- 3 prints what 1 and 2 print as well.
 *
 * Call sites stream into logging::verbose() or logging::debug() rather than
 * testing the level themselves:
 *
 *     logging::verbose() << "Loading " << elf_name;
 *     logging::debug() << "Bin size: " << bin_data.size() << " bytes";
 *
 * with no trailing std::endl -- the line ends itself. Below its level a Line
 * formats nothing at all, so an expensive argument costs nothing when it
 * would not have been printed.
 *
 * Each line is built in its own buffer and then written, which is not just
 * tidiness: std::hex and friends are sticky on std::cout, and one call site
 * leaving it in hex used to silently reformat every number printed after it
 * (see elf_parser.cpp, which did exactly that). A fresh buffer per line
 * cannot leak its formatting into the next one. */

#include <iostream>
#include <sstream>

namespace logging {

/* What detail printed from inside a build step is indented by, so it reads as
 * belonging to the step above it rather than as a step of its own. Shared with
 * progress.hpp, which lands a broken step's "Done." on the same column. */
inline constexpr const char* DETAIL_INDENT = "  ";

enum class Level {
    Quiet   = 0,
    Normal  = 1,
    Verbose = 2,
    Debug   = 3,
};

/* What -v/--verbosity settled on, clamped to the range above. */
Level level();

/* Whether a line written `at` this level would be printed. Prefer streaming
 * into the helpers below; this is for the rare case of skipping work rather
 * than output, such as the two-pass sort-weight dump. */
bool enabled(Level at);

/* One log line, built in full and written when it goes out of scope. Not
 * constructed directly -- see verbose()/debug() below. */
class Line {
public:
    Line(bool enabled, std::ostream& out);
    ~Line();

    Line(const Line&) = delete;
    Line& operator=(const Line&) = delete;

    template<typename T>
    Line& operator<<(const T& value) {
        if(enabled_) {
            buffer_ << value;
        }
        return *this;
    }

    /* std::hex, std::setw and the rest still work, and stay confined to the
     * line that asked for them. std::endl is accepted too, though a line
     * already ends itself. */
    Line& operator<<(std::ostream& (*manip)(std::ostream&)) {
        if(enabled_) {
            buffer_ << manip;
        }
        return *this;
    }

    Line& operator<<(std::ios_base& (*manip)(std::ios_base&)) {
        if(enabled_) {
            buffer_ << manip;
        }
        return *this;
    }

private:
    bool enabled_;
    std::ostream& out_;
    std::ostringstream buffer_;
};

/* A line at Level::Verbose: one thing handled within the current phase. */
Line verbose();

/* A line at Level::Debug: an internal detail of how the phase did it. */
Line debug();

/* A line at Level::Normal that is not a phase of its own -- a note about the
 * build rather than a step in it, such as which layout an external ISO must
 * have been built for. Phases themselves are ProgressLines, not these. */
Line note();

/* Makes stderr close a build step's line before writing to it, so a
 * diagnostic raised in the middle of a step starts on a line of its own:
 *
 *     Loading main executable...
 *     ELF file does not exist: "game.elf"
 *
 * rather than being appended to the label that is still waiting for its
 * "Done.". This is done to stderr as a whole rather than through a logging
 * helper per message, because a diagnostic is raised from wherever the
 * failure is -- deep inside libisofs wrappers, ELF parsing, filesystem
 * copies -- and every one of those places would otherwise have to know which
 * step it happens to be running under. They don't, and shouldn't.
 *
 * Call once from main(), after term::configure() and before anything is
 * printed. Idempotent. */
void install_diagnostic_guard();

}
