/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* The scratch directory mkdcdisc stages a disc's contents in, and the
 * machinery that makes sure it goes away again. */

#include <filesystem>
#include <optional>

std::optional<std::filesystem::path> generate_temp_directory();
void destroy_temp_directory(const std::filesystem::path& path);

/* Arranges for `path` to be removed on the way out, whether the program
 * returns normally or exit()s from anywhere. Call once, right after creating
 * the directory.
 *
 * An interrupted run reaches it the same way, by unwinding rather than by
 * dying where it stood -- see interrupt.hpp, which is what makes a Ctrl+C
 * leave a directory that can still be deleted. */
void install_temp_dir_cleanup(const std::filesystem::path& path);
