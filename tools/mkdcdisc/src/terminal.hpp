/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Colored output, and the single place that decides whether there is any.
 *
 * The colors are ANSI escape sequences, which every terminal mkdcdisc runs in
 * understands -- the Windows console included, since Windows 10, once
 * ENABLE_VIRTUAL_TERMINAL_PROCESSING has been turned on for it (which
 * configure() does). A console that refuses the flag, or a Windows old enough
 * not to know it, simply stays uncolored.
 *
 * An escape sequence written anywhere that isn't a terminal is not a color but
 * litter, so nothing is emitted unless the stream at hand is known to be one:
 * `mkdcdisc ... > build.log`, a pipe into grep, and the e2e suite capturing
 * output all keep exactly the plain text they got before there were colors.
 * stdout and stderr are decided separately, since redirecting one says nothing
 * about the other.
 *
 * That decision can be overridden, in this order:
 *
 *   --color auto|always|never   what the command line asked for; `always`
 *                               emits sequences whatever they are written to
 *   --disable-color             shorthand for --color never
 *   NO_COLOR                    set and non-empty: off (see no-color.org)
 *   CLICOLOR_FORCE              set to anything but "0": on, redirected or not
 *   TERM=dumb                   off
 *
 * Call sites test none of that: they stream a Style, or one of the ready-made
 * prefixes below, and get an escape sequence or nothing at all. */

#include <iosfwd>
#include <string>
#include <string_view>

namespace term {

/* The column mkdcdisc's prose wraps at -- the --help text and the diagnostics
 * both, so a message and the option list it points at are shaped the same.
 * Fixed rather than taken from the terminal: --help has always been written
 * to this width, and a diagnostic that reflows differently depending on the
 * window it was printed in is harder to compare against a bug report. */
inline constexpr size_t WRAP_WIDTH = 100;

enum class ColorMode {
    Auto,    /* colored when the stream is a terminal that can take it */
    Always,
    Never,
};

/* What a piece of output means, rather than which color it comes out as: the
 * palette is this module's business, and it deliberately stays within the
 * eight basic ANSI colors, which are the user's terminal theme rather than
 * fixed values -- red on a light background is not the same red as red on a
 * dark one, and only the terminal knows which it is on. */
enum class Style {
    Reset,
    Error,      /* bold red */
    Warning,    /* bold yellow */
    Note,       /* bold cyan */
    Success,    /* bold green */
    Heading,    /* bold */
    Dim,
};

/* Works out whether stdout and stderr are colored, and puts a Windows console
 * into ANSI mode if that is what is on the other end. Call once from main(),
 * before anything is printed.
 *
 * argv is scanned for --color/--disable-color as well, so that the few
 * messages printed while the options are still being parsed already honour
 * them; the value is validated later, by apply_color_options() (see
 * cli.hpp). */
void configure(int argc, char* argv[]);

/* Re-decides with the mode the option parser settled on. */
void set_color_mode(ColorMode mode);

/* "auto"/"always"/"never" -> ColorMode. Returns false, leaving *mode alone,
 * for anything else, so the caller can reject the value in its own words. */
bool parse_color_mode(const std::string& when, ColorMode* mode);

/* Whether `os` currently gets escape sequences. Only std::cout, std::cerr and
 * std::clog are ever colored; anything else (a string stream, a file) is not
 * a terminal by construction. */
bool colored(const std::ostream& os);

/* Whether `os` is connected to a terminal, regardless of whether it is
 * colored: what tells an interactive progress line apart from a log file.
 * See ProgressLine in progress.hpp. */
bool is_terminal(const std::ostream& os);

/* Switches `os` to `style` from there on, and leaves it that way -- prefer
 * the Text form below, which cannot forget to switch back. */
std::ostream& operator<<(std::ostream& os, Style style);

/* A run of text in one style, reset immediately afterwards. */
struct Text {
    Style style;
    std::string_view text;
};

std::ostream& operator<<(std::ostream& os, const Text& text);

/* `std::cout << term::styled(term::Style::Heading, "Usage:")`. The text is
 * only borrowed, so this is for use within the streaming expression itself,
 * which is where a style belongs anyway. */
constexpr Text styled(Style style, std::string_view text) {
    return Text{style, text};
}

/* The prefixes the diagnostics start with, so that `std::cerr << term::error
 * << "..."` reads as the line it prints. */
inline constexpr Text error{Style::Error, "Error: "};
inline constexpr Text warning{Style::Warning, "Warning: "};
inline constexpr Text note{Style::Note, "Note: "};

}
