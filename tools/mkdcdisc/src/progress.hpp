/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* The terminal end of a progress_t: one line of output that a long step
 * keeps rewriting in place. */

#include <cstdint>
#include <string>

#include "progress.h"

/* A single log line owning one phase of the build, from the moment it is
 * announced to the moment it is done:
 *
 *   Generating data track...         42%     while it runs
 *   Generating data track...        Done.    once finished
 *
 * Every phase mkdcdisc goes through gets one of these, which is what the
 * default verbosity level is made of -- a checklist of the steps, in order,
 * and nothing else (see logging.hpp for what each level shows). A phase with
 * nothing to count is announced and finished the same way; it simply never
 * shows a percentage.
 *
 * The percentage is rewritten over itself with a carriage return, so the
 * whole step stays on the line it announced itself on. That only works on a
 * terminal (terminal.hpp is what decides whether stdout is one), so when
 * stdout is redirected (a log file, a pipe, the e2e suite) the percentages
 * are dropped and just the announcement and "Done." are printed -- one clean
 * line per step, exactly what mkdcdisc printed before there was any progress
 * reporting at all.
 *
 * At -v 2 and above the phase also has detail printed underneath it, which
 * arrives while the label is still waiting for its "Done." on a half-drawn
 * line. break_line() is how that detail gets a line of its own; the phase
 * then closes with an indented "Done." rather than by repeating its label:
 *
 *   Gathering input files...
 *     Loading "game.elf"
 *     Done.
 *
 * At -v 0 / -q nothing is printed and no percentage is ever computed. */
class ProgressLine {
public:
    /* Prints `label` (e.g. "Generating data track...") right away, so the
     * step is announced before it starts rather than after it ends. */
    explicit ProgressLine(std::string label);

    /* Closes the line if finish() was not called -- an error path bailing out
     * mid-step leaves the message it prints on its own line rather than
     * appended to a half-drawn percentage. */
    ~ProgressLine();

    ProgressLine(const ProgressLine&) = delete;
    ProgressLine& operator=(const ProgressLine&) = delete;

    /* The C-callable view of this line, to hand to anything taking a
     * progress_t* -- cd_image_set_progress(), read_burn_source(). Valid
     * for as long as the ProgressLine is.
     *
     * It is also how the step hears that it has been interrupted: the
     * callback answers "stop" once a Ctrl+C has been seen (see
     * interrupt.hpp). That is deliberately not conditional on anything this
     * line prints -- -q silences the percentage, not the Ctrl+C. */
    const progress_t* progress() const { return &progress_; }

    /* Advances the line. `total` may be 0 when the size isn't known, in which
     * case nothing is drawn. Cheap to call per sector or per read: it only
     * touches the terminal when the whole percent actually changes. */
    void report(uint64_t done, uint64_t total);

    /* Replaces the percentage with "Done." and ends the line. Idempotent. */
    void finish();

    /* Ends the terminal line this step is drawn on, so that something else
     * can print underneath it rather than into it. Idempotent, and a no-op
     * once the line is already closed. Called by the logging module before it
     * writes detail from inside a phase -- there is no reason for a call site
     * to reach for it directly. */
    void break_line();

private:
    void draw_status(const std::string& status);

    progress_t progress_;
    std::string label_;
    bool enabled_;      /* -v 0 / -q silences the whole line */
    bool interactive_;  /* stdout is a terminal, so \r rewrites work */
    int last_percent_;  /* -1 until the first percentage is drawn */
    bool finished_;
    bool line_open_;    /* something is drawn on the current line, unterminated */

    /* The step this one was opened inside of, restored when it goes away.
     * Steps nest by scope rather than by intent -- build_cd()'s standalone
     * .iso dump is opened while the data track's own line is still in scope
     * -- so the innermost live one is tracked as a chain rather than assumed
     * to be the only one. */
    ProgressLine* previous_;
};

/* The innermost step currently being reported on, or nullptr when no phase is
 * open or the open one has already finished. What the logging module asks in
 * order to place its detail lines under the right phase. */
ProgressLine* active_progress_line();
