/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Ctrl+C, recorded as a request rather than acted on where it lands.
 *
 * The handler used to call exit() directly, which cleaned up nothing it
 * meant to: on Windows the C runtime runs a signal handler on a thread of
 * its own, so the rest of the program keeps going while the exit handlers
 * run -- libisofs still reading the file it was up to, std::filesystem::copy
 * still copying -- and the staging directory cannot be deleted while its
 * files are open. exit() from a handler is also not something a signal
 * handler may portably do at all.
 *
 * So the handler now does the one thing it is allowed to do: set a flag. The
 * long steps poll it, stop where they stand, and unwind normally; by the time
 * the exit handlers run, nothing is open and the directory goes away.
 *
 * The steps that poll are the ones that hold a file open long enough to
 * matter: staging the disc contents, writing the filler files, draining
 * libisofs, and writing the image out (which hears about it through its
 * progress callback -- see progress.h). A Ctrl+C anywhere else is noticed at
 * the end of whatever step is running.
 *
 * A second Ctrl+C skips all of that and kills the process outright, so an
 * interruption is never something the program can refuse. */

/* Installs the SIGINT/SIGTERM handlers. Call once, before the first long
 * step. */
void install_interrupt_handler();

/* Whether a Ctrl+C (or SIGTERM) has been seen. Long steps poll it to stop
 * early; their callers poll it to tell an interruption from a failure. */
bool interrupted();

/* 128 + the signal number, the shell convention for death by signal. Only
 * meaningful once interrupted() is true. */
int interrupted_exit_code();
