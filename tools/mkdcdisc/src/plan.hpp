/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Everything the run has settled on, printed once at -v 3 before any of it is
 * acted on: which disc, in which format and layout, from which files, with
 * which metadata.
 *
 * The rest of the log says what mkdcdisc is doing as it does it, which answers
 * "how far did it get" but not "what was it asked for", and the answer to
 * that is spread across forty options, their defaults, and the several places
 * one option quietly overrides another. A bug report that opens with this
 * block does not have to reconstruct the command line from its output.
 *
 * Nothing here decides anything: every value is read from the same accessor
 * the builder will read it from. A plan that had opinions of its own would
 * eventually describe a build that isn't the one that runs. */

/* Prints the plan (at -v 3) and the line marking the end of the pre-run checks
 * (at -v 2); the same moment, and the last one at which nothing has been
 * built yet. Called from main() once every option has been validated. */
void announce_build_plan();
