/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Reading an ISO9660 volume back out of the flat bytes that make it up: the
 * Primary Volume Descriptor, the root directory, and the offset every address
 * inside the volume was written against.
 *
 * This exists because mkdcdisc now has to answer questions about a filesystem
 * it did not build -- the one -t/--external-data-track hands it -- and about
 * the one it did, as a check that it came out as intended. Both are the same
 * question asked of the same bytes, so both go through here.
 *
 * Deliberately dependency-free: no options, no logging, no libisofs. It is
 * handed bytes and returns facts, which is what lets it be unit-tested against
 * hand-built volumes, and what keeps the judgement about those facts (which
 * ones are errors, which are warnings) in boot_checks.cpp where it belongs.
 *
 * Only the root directory is read, never the tree below it. The one caller
 * that walks a volume wants the boot file, which the console looks up at the
 * root or not at all -- and stopping there means never having to translate a
 * recorded address into a buffer offset, which is the part that depends on
 * knowing the volume's base LBA. See locate_root_directory().
 *
 * Separate from tests/common/iso_read.h, which answers the same kind of
 * question for the test suite and is kept independent on purpose: those checks
 * exist to describe libisofs's output rather than to agree with mkdcdisc's
 * reading of it, so the two must not share a reader. */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iso9660 {

constexpr size_t SECTOR = 2048;

/* Sectors 0-15, which ECMA-119 reserves and says nothing about. It is where a
 * Dreamcast disc carries its boot header. */
constexpr size_t SYSTEM_AREA_SECTORS = 16;
constexpr size_t SYSTEM_AREA_SIZE = SYSTEM_AREA_SECTORS * SECTOR;

/* The Primary Volume Descriptor, which sits at sector 16 of the volume's own
 * bytes whatever address the volume was built for. */
struct Volume {
    bool valid = false;

    std::string volume_id;
    uint32_t volume_space = 0;
    uint32_t root_lba = 0;
    uint32_t root_length = 0;
};

Volume read_volume(const std::vector<uint8_t>& iso);

/* Where the root directory says it is, and where it actually is.
 *
 * The two differ by exactly the MSINFO the volume was built for. An ISO built
 * with `mkisofs -C 0,N` (or, here, libisofs with ms_block N) has N added to
 * every address it records, because those addresses are read on a disc where
 * the volume starts at N -- but the file itself still begins at its own byte
 * zero. So the root directory record in the PVD names sector N+k while the
 * extent it names sits at sector k of the buffer, and the difference between
 * the two recovers N without being told it.
 *
 * `sector` is found by scanning for the extent rather than assumed, so the
 * answer is self-validating: the candidate must open with a "." record that
 * is a directory and that points back at the very address the PVD gave, which
 * nothing but the real root directory does. Not found means the volume is not
 * one this can make sense of, and callers say so rather than guessing. */
struct RootDirectory {
    bool found = false;

    uint32_t declared_lba = 0;  /* what the PVD records */
    uint32_t sector = 0;        /* where it is in the buffer */
    uint32_t length = 0;
};

RootDirectory locate_root_directory(const std::vector<uint8_t>& iso);

/* The MSINFO the volume was built for, or nothing when the root directory
 * could not be located. Just the subtraction described above, named after the
 * question it answers so call sites read as asking it. */
std::optional<uint32_t> detect_msinfo(const std::vector<uint8_t>& iso);

/* One entry of the root directory. `name` has the ";1" version suffix
 * stripped, and is otherwise exactly the bytes recorded -- uppercased by
 * whichever tool wrote the volume, not by this. Callers that compare against a
 * name a user chose have to decide for themselves what case means; see
 * boot_checks.cpp. */
struct Entry {
    std::string name;
    uint32_t lba = 0;
    uint32_t size = 0;
    bool is_dir = false;
};

/* The root directory's entries, "." and ".." excluded, in the order recorded.
 * Empty when the root directory could not be located or holds nothing. */
std::vector<Entry> read_root_directory(const std::vector<uint8_t>& iso);

}
