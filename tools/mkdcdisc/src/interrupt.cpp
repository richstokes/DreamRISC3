/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Interruption handling: see interrupt.hpp. */

#include <csignal>

#include "interrupt.hpp"

/* volatile sig_atomic_t is the one kind of object a handler may touch, and
 * assigning to it the one thing it may do. Everything else this file used to
 * do from inside the handler was undefined behaviour that happened to work. */
static volatile std::sig_atomic_t g_interrupting_signal = 0;

namespace {

extern "C" void record_interrupt(int sig) {
    g_interrupting_signal = sig;

    /* A second one is not a request but an order: put the default disposition
     * back, so it kills the process however stuck the step being cancelled
     * turns out to be. The Windows C runtime resets the disposition on its
     * own before calling a handler; POSIX does not, so say it either way. */
    std::signal(sig, SIG_DFL);
}

}

void install_interrupt_handler() {
    std::signal(SIGINT, record_interrupt);
    std::signal(SIGTERM, record_interrupt);

#ifdef SIGBREAK
    /* Windows' Ctrl+Break. It reaches a console program by a different route
     * than Ctrl+C (a console control event addressed to the process group
     * rather than to the console) and it is just as much a request to stop,
     * so it gets the same answer. */
    std::signal(SIGBREAK, record_interrupt);
#endif
}

bool interrupted() {
    return g_interrupting_signal != 0;
}

int interrupted_exit_code() {
    return 128 + (int) g_interrupting_signal;
}
