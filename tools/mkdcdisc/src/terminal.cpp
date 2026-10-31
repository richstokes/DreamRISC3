/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Colored output: see terminal.hpp. */

/* MSYS2 and Cygwin build against newlib, whose headers hide the POSIX
 * extensions (fileno() and isatty() among them) as soon as -std=c++17 defines
 * __STRICT_ANSI__. _DEFAULT_SOURCE asks for the same visibility glibc gives by
 * default on Linux, so this file sees one set of declarations everywhere. It
 * has to come before the first header. */
#if defined(__MSYS__) || defined(__CYGWIN__)
#define _DEFAULT_SOURCE 1
#endif

/* Likewise before the first header, and before <windows.h> in particular:
 * GetFileInformationByHandleEx() (used to recognize an MSYS terminal) is Vista
 * and up, and MinGW-w64 hides it when the target version is left at its own
 * older default. */
#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "terminal.hpp"

#ifdef _WIN32
#include <io.h>
#include <windows.h>

/* Windows 10 (1511) and up interpret ANSI escape sequences in a console, but
 * only once asked to. The flag's value is part of the API rather than of the
 * header, so an older MinGW-w64 that doesn't declare it is no reason to give
 * up on a console that would accept it. */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

namespace {

struct StreamState {
    bool is_terminal = false;
    bool colored = false;
};

/* Nothing is colored until configure() has run: a stray escape sequence is a
 * worse failure than a missing color. */
StreamState g_stdout;
StreamState g_stderr;
term::ColorMode g_mode = term::ColorMode::Auto;

const StreamState* state_for(const std::ostream& os) {
    if(&os == &std::cout) {
        return &g_stdout;
    }

    /* std::clog is stderr too, unbuffered-ness aside. */
    if(&os == &std::cerr || &os == &std::clog) {
        return &g_stderr;
    }

    return nullptr;
}

const char* escape(term::Style style) {
    switch(style) {
        case term::Style::Reset:   return "\033[0m";
        case term::Style::Error:   return "\033[1;31m";
        case term::Style::Warning: return "\033[1;33m";
        case term::Style::Note:    return "\033[1;36m";
        case term::Style::Success: return "\033[1;32m";
        case term::Style::Heading: return "\033[1m";
        case term::Style::Dim:     return "\033[2m";
    }

    return "";
}

/* Set to anything at all, empty string excepted -- what NO_COLOR asks for. */
bool env_is_set(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

/* Set to anything but "0" -- what CLICOLOR_FORCE asks for. */
bool env_is_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

#ifdef _WIN32

/* A real console (rather than a file, a pipe, or NUL), which GetConsoleMode()
 * answers exactly. _isatty() does not: it is true of any character device,
 * NUL included, so `mkdcdisc ... > NUL` would look interactive to it. */
bool is_windows_console(int fd) {
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if(handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD mode = 0;
    return GetConsoleMode(handle, &mode) != 0;
}

/* Turns on the console's interpretation of escape sequences, and says whether
 * it is now on. False on Windows 8 and older, which don't know the flag.
 *
 * It is deliberately not put back on the way out: the flag only makes the
 * console act on sequences rather than print them, every console program
 * emitting any turns it on the same way, and there is no exit path -- Ctrl+C
 * included -- on which a restore could be relied upon anyway. */
bool enable_virtual_terminal(int fd) {
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if(handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD mode = 0;
    if(!GetConsoleMode(handle, &mode)) {
        return false;
    }

    if(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
        return true;
    }

    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

/* The MSYS2/Cygwin terminals -- mintty, and the DreamSDK shell with it -- are
 * not consoles: a native Windows program writing to one sees a named pipe,
 * _isatty() says no and GetConsoleMode() fails, yet what is on the other end
 * is a terminal that speaks ANSI perfectly well. Without this, a MinGW-w64
 * build would be colorless in precisely the shell it is most often run from.
 *
 * Cygwin names that pipe after what it is, which is what makes recognizing it
 * a fact about the handle rather than a guess:
 *
 *   \msys-1888ae32e00d56aa-pty0-to-master
 *   \cygwin-b029bcfe0b7a5f4b-pty2-from-master
 *
 * $TERM or $MSYSTEM would be the guess: both are just as set when the output
 * of that same shell is redirected into a file. */
bool is_msys_terminal(int fd) {
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if(handle == INVALID_HANDLE_VALUE || GetFileType(handle) != FILE_TYPE_PIPE) {
        return false;
    }

    /* FILE_NAME_INFO ends in a variable-length name, so it is allocated with
     * room for one rather than by itself. */
    char buffer[sizeof(FILE_NAME_INFO) + MAX_PATH * sizeof(WCHAR)] = {0};
    FILE_NAME_INFO* info = (FILE_NAME_INFO*) buffer;

    if(!GetFileInformationByHandleEx(handle, FileNameInfo, info, sizeof(buffer))) {
        return false;
    }

    std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));

    bool from_cygwin = name.find(L"msys-") != std::wstring::npos ||
                       name.find(L"cygwin-") != std::wstring::npos;
    bool is_pty = name.find(L"-pty") != std::wstring::npos;
    bool is_master_end = name.find(L"-to-master") != std::wstring::npos ||
                         name.find(L"-from-master") != std::wstring::npos;

    return from_cygwin && is_pty && is_master_end;
}

#endif

bool fd_is_terminal(int fd) {
#ifdef _WIN32
    return is_windows_console(fd) || is_msys_terminal(fd);
#else
    return isatty(fd) != 0;
#endif
}

/* Whether escape sequences written to `fd` will be acted on rather than
 * printed. Only ever asked about a stream already known to be a terminal. */
bool fd_takes_ansi(int fd) {
#ifdef _WIN32
    if(is_windows_console(fd)) {
        return enable_virtual_terminal(fd);
    }

    /* An MSYS terminal, then, which needs no permission to be itself. */
    return true;
#else
    (void) fd;
    return true;
#endif
}

bool decide(int fd, bool is_terminal) {
    switch(g_mode) {
        case term::ColorMode::Never:
            return false;

        case term::ColorMode::Always:
#ifdef _WIN32
            /* Sequences were asked for whatever is downstream, but a console
             * still has to be told to interpret them rather than print them.
             * Its answer doesn't change the outcome: --color always means the
             * sequences go out either way. */
            enable_virtual_terminal(fd);
#endif
            return true;

        case term::ColorMode::Auto:
            break;
    }

    if(env_is_set("NO_COLOR")) {
        return false;
    }

    if(env_is_enabled("CLICOLOR_FORCE")) {
#ifdef _WIN32
        enable_virtual_terminal(fd);
#endif
        return true;
    }

    const char* term_name = std::getenv("TERM");
    if(term_name != nullptr && std::strcmp(term_name, "dumb") == 0) {
        return false;
    }

    return is_terminal && fd_takes_ansi(fd);
}

void resolve() {
    int out_fd = fileno(stdout);
    int err_fd = fileno(stderr);

    g_stdout.is_terminal = fd_is_terminal(out_fd);
    g_stderr.is_terminal = fd_is_terminal(err_fd);

    g_stdout.colored = decide(out_fd, g_stdout.is_terminal);
    g_stderr.colored = decide(err_fd, g_stderr.is_terminal);
}

}

namespace term {

bool parse_color_mode(const std::string& when, ColorMode* mode) {
    if(when == "auto") {
        *mode = ColorMode::Auto;
    } else if(when == "always") {
        *mode = ColorMode::Always;
    } else if(when == "never") {
        *mode = ColorMode::Never;
    } else {
        return false;
    }

    return true;
}

void configure(int argc, char* argv[]) {
    /* A lenient look at argv, not a second option parser: all it has to catch
     * is a --color/--disable-color on its way past, so that the handful of
     * messages printed before parse_options() has run honour it. Anything it
     * doesn't recognize -- including a bad --color value, or the word landing
     * here as some other option's value -- is left alone for the real parser
     * and apply_color_options() to deal with. */
    bool disabled = false;
    bool given = false;
    ColorMode from_option = ColorMode::Auto;

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if(arg == "--disable-color") {
            disabled = true;
        } else if(arg == "--color" && i + 1 < argc && !given) {
            /* The first --color, since that is the one apply_color_options()
             * will read out of OPTS. */
            given = parse_color_mode(argv[i + 1], &from_option);
        }
    }

    /* Settled in the order apply_color_options() settles it, so that a message
     * printed before the real parser has run is never colored differently from
     * the same message printed after it. */
    if(disabled) {
        g_mode = ColorMode::Never;
    }

    if(given) {
        g_mode = from_option;
    }

    resolve();
}

void set_color_mode(ColorMode mode) {
    g_mode = mode;
    resolve();
}

bool colored(const std::ostream& os) {
    const StreamState* state = state_for(os);
    return state != nullptr && state->colored;
}

bool is_terminal(const std::ostream& os) {
    const StreamState* state = state_for(os);
    return state != nullptr && state->is_terminal;
}

std::ostream& operator<<(std::ostream& os, Style style) {
    if(colored(os)) {
        os << escape(style);
    }

    return os;
}

std::ostream& operator<<(std::ostream& os, const Text& text) {
    if(colored(os)) {
        return os << escape(text.style) << text.text << escape(Style::Reset);
    }

    return os << text.text;
}

}
