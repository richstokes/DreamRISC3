/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Reading what the user hands in: the boot executable, and the files and
 * directories that make up the data track. */

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/* Flattens an ELF into the raw binary the console loads. */
bool load_elf_file(const std::filesystem::path& elf_name, std::vector<char>& bin_data);
bool load_bin_file(const std::filesystem::path& bin_path, std::vector<char>& bin_data);

/* Stages everything the data track will hold into `out`: the boot binary
 * under the name boot_filename() settles on, plus whatever -d/-D/-f name.
 *
 * `scramble_output` selects whether that binary is scrambled on the way in.
 * MIL-CD boot requires it; GD-ROM boot requires it not be. That is decided by
 * the output format alone, regardless of which of -e/-b/-B supplied the
 * binary. */
bool gather_files(const std::filesystem::path& out, bool scramble_output = true);

/* Reads a whole file into memory. Returns nullopt if it can't be opened. */
std::optional<std::string> read_file_contents(const std::filesystem::path& path);
