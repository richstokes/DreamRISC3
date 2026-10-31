/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* What mkdcdisc exits with, and what each number promises.
 *
 * A script wrapping mkdcdisc can only tell one failure from another by the
 * number, so these are a contract rather than an implementation detail: the
 * same kind of failure keeps the same code across releases, and the table in
 * docs/cli-reference.md says so in the user's words. Named here so that a new
 * failure path has to choose one deliberately, the two file errors that
 * exited USAGE for years did so because 2 was simply the number nearby code
 * happened to return.
 *
 * SUCCESS is deliberately not called EXIT_SUCCESS: that name is <cstdlib>'s. */

namespace exit_code {

/* The image was written, or what was asked for (-M, --help) was printed. */
inline constexpr int SUCCESS = 0;

/* The invocation was refused: an unknown option, a bad value, or a
 * combination that cannot mean anything. Nothing was attempted, and the
 * message is followed by a pointer to --help. See usage_error() in main.cpp,
 * which every such refusal but one goes through. */
inline constexpr int USAGE = 2;

/* A file the run needs is missing or unreadable, or one it would write is
 * already there and nothing said it could be replaced (--allow-overwrite). The
 * options were right; the disc could not be built from them as things stand on
 * disc. The line between this and USAGE is whether --help could have helped, it
 * has nothing to say about a missing ISO. */
inline constexpr int FILE_ERROR = 4;

/* The build itself failed, having got far enough to try. */
inline constexpr int BUILD_FAILED = 5;

/* Interrupted: 128 plus the signal number, so Ctrl+C is 130. Not a constant
 * because it depends on which signal arrived, see interrupted_exit_code()
 * in interrupt.hpp. */

}
