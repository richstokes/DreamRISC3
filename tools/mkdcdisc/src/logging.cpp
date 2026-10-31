/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Verbosity levels and the lines that honour them: see logging.hpp. */

#include "logging.hpp"

#include <cctype>
#include <sstream>
#include <streambuf>

#include "cli.hpp"
#include "progress.hpp"
#include "terminal.hpp"

namespace logging {

Level level() {
    int asked = verbosity();

    if(asked <= 0) {
        return Level::Quiet;
    }
    if(asked >= 3) {
        return Level::Debug;
    }

    return asked == 1 ? Level::Normal : Level::Verbose;
}

bool enabled(Level at) {
    return static_cast<int>(level()) >= static_cast<int>(at);
}

Line::Line(bool enabled, std::ostream& out):
    enabled_(enabled),
    out_(out) {
}

Line::~Line() {
    if(!enabled_) {
        return;
    }

    /* Detail printed from inside a phase belongs to that phase twice over: it
     * has to close the phase's half-drawn line before writing anything (the
     * label is on that line, waiting for its "Done."), and it is indented so
     * the phase it came from stays readable. */
    const char* indent = "";
    if(ProgressLine* step = active_progress_line()) {
        step->break_line();
        indent = DETAIL_INDENT;
    }

    out_ << indent << buffer_.str() << std::endl;
}

Line verbose() {
    return Line(enabled(Level::Verbose), std::cout);
}

Line debug() {
    return Line(enabled(Level::Debug), std::cout);
}

Line note() {
    return Line(enabled(Level::Normal), std::cout);
}

namespace {

/* Ends the open step's line, if there is one. Idempotent: break_line() is a
 * no-op once the line is already closed, so a multi-part diagnostic streamed
 * in several operations still only breaks once. */
void break_step_line() {
    if(ProgressLine* step = active_progress_line()) {
        step->break_line();
    }
}

/* How wide a piece of text prints, in columns. An escape sequence is colour,
 * not columns, so it counts for nothing -- otherwise a colored diagnostic
 * would wrap a dozen characters earlier than the same text uncolored. */
size_t visible_width(const std::string& text) {
    size_t width = 0;

    for(size_t i = 0; i < text.size(); ++i) {
        if(text[i] != '\033') {
            ++width;
            continue;
        }

        /* Skip to the letter that ends the sequence. A malformed one runs to
         * the end of the string, which is as good a guess as any. */
        while(i < text.size() && !std::isalpha((unsigned char) text[i])) {
            ++i;
        }
    }

    return width;
}

/* std::cerr's buffer, replaced so that everything written to it comes out
 * presentable without every call site having to arrange that for itself.
 *
 * Two duties, both of which the writer has no way of discharging. It closes
 * whatever build step is on screen before writing, so a diagnostic raised
 * mid-step starts on a line of its own; both halves land in the right order
 * because break_line() ends with std::endl, which flushes stdout before the
 * diagnostic reaches stderr. And it word-wraps to the width --help uses, so a
 * long message is shaped like the option list it points at rather than
 * running off the side of the terminal.
 *
 * Wrapping needs the whole line before it can place the first break, so text
 * is held until its newline arrives. Every diagnostic in the program ends in
 * std::endl, and sync() deliberately does not flush what is still pending:
 * std::cerr is unit-buffered, so it syncs after each << of a message still
 * being assembled, and honouring those would emit the message a fragment at a
 * time -- unwrapped, which is the whole thing this avoids. */
class DiagnosticBuf: public std::streambuf {
public:
    explicit DiagnosticBuf(std::streambuf* wrapped): wrapped_(wrapped) {}

protected:
    int_type overflow(int_type c) override {
        if(traits_type::eq_int_type(c, traits_type::eof())) {
            return traits_type::not_eof(c);
        }

        put(traits_type::to_char_type(c));
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for(std::streamsize i = 0; i < n; ++i) {
            put(s[i]);
        }

        return n;
    }

    int sync() override {
        return wrapped_->pubsync();
    }

private:
    void put(char c) {
        if(c != '\n') {
            if(pending_.empty()) {
                /* About to start a line, so this is the moment the step above
                 * has to be closed -- before any of it is emitted. */
                break_step_line();
            }

            pending_ += c;
            return;
        }

        emit_wrapped();
        wrapped_->sputc('\n');
    }

    /* Greedy wrap: words are placed until one would cross the margin, and it
     * starts the next line instead. A word longer than the margin on its own
     * -- a path, usually -- overruns rather than being cut in half, which is
     * what --help does with one too. */
    void emit_wrapped() {
        std::istringstream words(pending_);
        std::string word;
        size_t column = 0;

        while(words >> word) {
            size_t width = visible_width(word);

            if(column > 0 && column + 1 + width > term::WRAP_WIDTH) {
                wrapped_->sputc('\n');
                column = 0;
            } else if(column > 0) {
                wrapped_->sputc(' ');
                column += 1;
            }

            wrapped_->sputn(word.data(), (std::streamsize) word.size());
            column += width;
        }

        pending_.clear();
    }

    std::streambuf* wrapped_;
    std::string pending_;
};

DiagnosticBuf* DIAGNOSTIC_BUF = nullptr;

}

void install_diagnostic_guard() {
    if(DIAGNOSTIC_BUF) {
        return;
    }

    /* Deliberately never deleted: std::cerr keeps pointing at it, including
     * from the atexit() handler that cleans up the staging directory, and a
     * buffer destroyed before its stream has finished with it is worse than a
     * single allocation that lives as long as the process. */
    DIAGNOSTIC_BUF = new DiagnosticBuf(std::cerr.rdbuf());
    std::cerr.rdbuf(DIAGNOSTIC_BUF);
}

}
