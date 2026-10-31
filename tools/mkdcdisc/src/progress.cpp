/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Terminal progress lines: see progress.hpp. */

#include <iomanip>
#include <iostream>
#include <sstream>

#include "interrupt.hpp"
#include "logging.hpp"
#include "progress.hpp"
#include "terminal.hpp"

/* Where the status field starts, so the percentages (and the "Done."s) of
 * consecutive steps line up under each other whatever their labels say. Wide
 * enough for the longest one mkdcdisc prints, plus a gap. */
static const size_t STATUS_COLUMN = 34;

/* The innermost step with a line open. See active_progress_line(). */
static ProgressLine* ACTIVE_LINE = nullptr;

namespace {

/* extern "C" so the address handed to a progress_t is a C function
 * pointer proper; anonymous namespace so it stays private to this file. */
extern "C" int progress_line_trampoline(void* user_data, uint64_t done, uint64_t total) {
    static_cast<ProgressLine*>(user_data)->report(done, total);

    /* Answered whatever the line is doing on screen: -q silences the
     * percentage, it does not silence Ctrl+C. */
    return interrupted() ? 1 : 0;
}

}

ProgressLine* active_progress_line() {
    return ACTIVE_LINE;
}

ProgressLine::ProgressLine(std::string label):
    progress_{progress_line_trampoline, this},
    label_(std::move(label)),
    enabled_(logging::enabled(logging::Level::Normal)),
    interactive_(term::is_terminal(std::cout)),
    last_percent_(-1),
    finished_(false),
    line_open_(false),
    previous_(ACTIVE_LINE) {

    if(label_.size() < STATUS_COLUMN) {
        label_.resize(STATUS_COLUMN, ' ');
    } else {
        label_ += ' ';
    }

    if(enabled_) {
        std::cout << label_ << std::flush;
        line_open_ = true;
    }

    ACTIVE_LINE = this;
}

ProgressLine::~ProgressLine() {
    /* Only what is still half-drawn needs closing: a line that detail has
     * already broken would otherwise get a blank one to itself. */
    if(enabled_ && !finished_ && line_open_) {
        std::cout << std::endl;
    }

    ACTIVE_LINE = previous_;
}

void ProgressLine::break_line() {
    if(!enabled_ || !line_open_) {
        return;
    }

    std::cout << std::endl;
    line_open_ = false;

    /* The percentage went with the line it was drawn on, so the next report()
     * has to draw one again rather than skip it as unchanged. */
    last_percent_ = -1;
}

void ProgressLine::draw_status(const std::string& status) {
    /* Redrawing the label too, rather than only the status field, keeps the
     * line correct even if something else wrote to the terminal meanwhile.
     * With no line open there is nothing to rewind over, so the label is
     * simply written again -- a step that had detail printed under it picks
     * its percentage back up on a fresh line. */
    if(line_open_) {
        std::cout << '\r';
    }

    std::cout << label_ << status << std::flush;
    line_open_ = true;
}

void ProgressLine::report(uint64_t done, uint64_t total) {
    if(!enabled_ || !interactive_ || finished_ || total == 0) {
        return;
    }

    /* Every step mkdcdisc reports knows its size exactly, but this is not
     * the place to find out otherwise: an overshooting step reads as finished
     * rather than as "137%". */
    int percent = done >= total ? 100 : (int) ((done * 100) / total);

    if(percent == last_percent_) {
        return;
    }
    last_percent_ = percent;

    std::ostringstream status;
    status << std::setw(3) << percent << '%';
    draw_status(status.str());
}

void ProgressLine::finish() {
    if(!enabled_ || finished_) {
        return;
    }
    finished_ = true;

    /* The phase stops being the one detail belongs under the moment it is
     * done, rather than when its scope happens to end. */
    ACTIVE_LINE = previous_;

    if(!line_open_) {
        /* Detail was printed underneath, and the label went up with it. The
         * phase closes as the last item of its own list rather than by
         * announcing itself a second time. */
        std::cout << logging::DETAIL_INDENT << term::styled(term::Style::Success, "Done.") << std::endl;
        return;
    }

    /* "Done." is longer than the "100%" it lands on, so nothing of the last
     * percentage survives underneath it. */
    if(interactive_ && last_percent_ >= 0) {
        std::cout << '\r' << label_;
    }

    std::cout << term::styled(term::Style::Success, "Done.") << std::endl;
    line_open_ = false;
}
