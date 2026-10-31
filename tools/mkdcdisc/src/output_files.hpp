/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* What a run is going to write, answered from the options alone and before any
 * of it exists: how to name those files, and whether writing them would
 * destroy something that is already there.
 *
 * One file rather than a question each place answers for itself, because -o
 * names one path and the run writes anywhere between one file and a hundred of
 * them: an .mds carries its bulk in a sibling .mdf, a .gdi is a descriptor
 * plus a track file per track, and -I/--dump-iso adds an .iso to either. The
 * closing "Wrote ..." line, the -v 3 build plan and the overwrite check all
 * need that same answer, and a second opinion about it would be a file
 * silently written over or a file promised and never produced. */

#include <filesystem>
#include <string>

/* The files this run will produce, as one phrase: one file for CDI and NRG,
 * the .mds/.mdf pair for Alcohol, and the .gdi descriptor plus the directory
 * its track files go in. Used by the build plan before they exist and by the
 * closing "Wrote ..." once they do, so what was promised and what is reported
 * are the same words. */
std::string output_files_description(const std::string& output_path);

/* Refuses to write over what a previous run left behind, having named every
 * file in the way and the option that permits it.
 *
 * False means the build must not go ahead: something is there and
 * --allow-overwrite was not given. With the option, the same files are named
 * as a note and the run carries on -- which is what mkdcdisc did
 * unconditionally, and in silence, up to this check existing.
 *
 * Call once, after everything that could refuse the invocation over the
 * options themselves and before anything is staged, announced or written. */
bool check_output_files(const std::string& output_path);
