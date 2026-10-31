/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* ISO9660 volume reading: see iso_reader.hpp. */

#include <algorithm>
#include <cstring>

#include "iso_reader.hpp"

namespace iso9660 {

/* How far into the volume the root directory extent is looked for.
 *
 * Every writer lays a volume out the same way: the system area, then the
 * volume descriptor set, then both path tables, and only then the directory
 * extents -- the root's first among them. So the answer is always a handful of
 * sectors in; the images mkdcdisc itself builds put it at sector 18.
 *
 * The bound is what keeps a file that is not an ISO9660 volume at all from
 * being scanned end to end before being given up on, and 4096 sectors (8 MB)
 * leaves room for path tables far larger than any Dreamcast disc's. */
static const size_t MAX_ROOT_DIRECTORY_SCAN_SECTORS = 4096;

/* Fields of a directory record, ECMA-119 9.1, as offsets from its start. The
 * both-endian pairs are read through their little-endian half, which is the
 * one every other reader in this project uses too. */
static const size_t RECORD_EXTENT = 2;
static const size_t RECORD_DATA_LENGTH = 10;
static const size_t RECORD_FILE_FLAGS = 25;
static const size_t RECORD_NAME_LENGTH = 32;
static const size_t RECORD_NAME = 33;

/* The shortest a directory record can be: everything up to the name, plus one
 * byte of name. */
static const size_t RECORD_MIN_LENGTH = RECORD_NAME + 1;

static const uint8_t FILE_FLAG_DIRECTORY = 0x02;

static uint32_t le32(const uint8_t* at) {
    return (uint32_t) at[0] | ((uint32_t) at[1] << 8)
        | ((uint32_t) at[2] << 16) | ((uint32_t) at[3] << 24);
}

/* An ECMA-119 identifier with its space padding removed. */
static std::string trimmed_field(const uint8_t* at, size_t len) {
    std::string s((const char*) at, len);

    while(!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
        s.pop_back();
    }

    return s;
}

Volume read_volume(const std::vector<uint8_t>& iso) {
    Volume volume;

    if(iso.size() < (SYSTEM_AREA_SECTORS + 1) * SECTOR) {
        return volume;
    }

    const uint8_t* pvd = iso.data() + SYSTEM_AREA_SECTORS * SECTOR;

    /* Type 1 is the Primary Volume Descriptor, and "CD001" is what makes the
     * sector a volume descriptor at all rather than 2048 bytes that happen to
     * start with a 1. */
    if(pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        return volume;
    }

    volume.valid = true;
    volume.volume_id = trimmed_field(pvd + 40, 32);
    volume.volume_space = le32(pvd + 80);

    /* The root directory record is not in the directory hierarchy at all: it
     * is embedded in the PVD itself, at offset 156. */
    volume.root_lba = le32(pvd + 156 + RECORD_EXTENT);
    volume.root_length = le32(pvd + 156 + RECORD_DATA_LENGTH);

    return volume;
}

/* Whether the sector at `at` opens with the root directory's own "." record,
 * naming the extent the PVD said the root directory lives at.
 *
 * All four conditions are needed for the answer to be trustworthy: a
 * single-byte name of 0x00 is what ECMA-119 spells "." with, the directory
 * flag rules out a file that happens to start that way, and the two addresses
 * pointing back at what the PVD recorded is what makes it *this* volume's root
 * rather than any directory extent. Nothing else in a volume satisfies all
 * four. */
static bool is_root_directory_extent(const std::vector<uint8_t>& iso, size_t at,
                                     uint32_t root_lba, uint32_t root_length) {
    if(at + RECORD_MIN_LENGTH > iso.size()) {
        return false;
    }

    uint8_t record_length = iso[at];
    if(record_length < RECORD_MIN_LENGTH || at + record_length > iso.size()) {
        return false;
    }

    if(iso[at + RECORD_NAME_LENGTH] != 1 || iso[at + RECORD_NAME] != 0x00) {
        return false;
    }

    if((iso[at + RECORD_FILE_FLAGS] & FILE_FLAG_DIRECTORY) == 0) {
        return false;
    }

    return le32(iso.data() + at + RECORD_EXTENT) == root_lba
        && le32(iso.data() + at + RECORD_DATA_LENGTH) == root_length;
}

RootDirectory locate_root_directory(const std::vector<uint8_t>& iso) {
    RootDirectory root;

    Volume volume = read_volume(iso);
    if(!volume.valid || volume.root_length == 0) {
        return root;
    }

    /* From just past the PVD rather than from sector 0: the system area holds
     * the boot header, which is arbitrary bytes as far as ECMA-119 is
     * concerned, and there is no reason to let them answer a question about
     * the filesystem. */
    size_t sectors = iso.size() / SECTOR;
    size_t last = std::min(sectors, SYSTEM_AREA_SECTORS + 1 + MAX_ROOT_DIRECTORY_SCAN_SECTORS);

    for(size_t sector = SYSTEM_AREA_SECTORS + 1; sector < last; ++sector) {
        /* A volume can only have been built for an address at or after the one
         * it physically sits at, so a candidate past the recorded address
         * would mean a negative MSINFO. */
        if(sector > volume.root_lba) {
            break;
        }

        if(!is_root_directory_extent(iso, sector * SECTOR, volume.root_lba, volume.root_length)) {
            continue;
        }

        root.found = true;
        root.declared_lba = volume.root_lba;
        root.sector = (uint32_t) sector;
        root.length = volume.root_length;

        return root;
    }

    return root;
}

std::optional<uint32_t> detect_msinfo(const std::vector<uint8_t>& iso) {
    RootDirectory root = locate_root_directory(iso);

    if(!root.found) {
        return std::nullopt;
    }

    return root.declared_lba - root.sector;
}

std::vector<Entry> read_root_directory(const std::vector<uint8_t>& iso) {
    std::vector<Entry> entries;

    RootDirectory root = locate_root_directory(iso);
    if(!root.found) {
        return entries;
    }

    size_t extent = (size_t) root.sector * SECTOR;

    for(uint32_t offset = 0; offset < root.length; ) {
        size_t at = extent + offset;

        if(at + RECORD_MIN_LENGTH > iso.size()) {
            return entries;
        }

        uint8_t record_length = iso[at];
        if(record_length == 0) {
            /* A directory record never straddles a sector boundary, so a
             * zero length is ECMA-119's way of saying "the rest of this
             * sector is padding". */
            offset = ((offset / SECTOR) + 1) * SECTOR;
            continue;
        }

        if(record_length < RECORD_MIN_LENGTH || at + record_length > iso.size()) {
            return entries;
        }

        uint8_t name_length = iso[at + RECORD_NAME_LENGTH];
        if(RECORD_NAME + name_length > record_length) {
            return entries;
        }

        /* "." and ".." are spelled as one byte, 0x00 and 0x01, which are not
         * names anybody asked after. */
        bool is_self_or_parent = name_length == 1
            && (iso[at + RECORD_NAME] == 0x00 || iso[at + RECORD_NAME] == 0x01);

        if(!is_self_or_parent) {
            Entry entry;
            entry.lba = le32(iso.data() + at + RECORD_EXTENT);
            entry.size = le32(iso.data() + at + RECORD_DATA_LENGTH);
            entry.is_dir = (iso[at + RECORD_FILE_FLAGS] & FILE_FLAG_DIRECTORY) != 0;

            entry.name.assign((const char*) iso.data() + at + RECORD_NAME, name_length);

            /* ECMA-119 records a file name as "NAME.EXT;1". The version is
             * never part of what anyone calls the file. */
            size_t version = entry.name.find(';');
            if(version != std::string::npos) {
                entry.name = entry.name.substr(0, version);
            }

            entries.push_back(entry);
        }

        offset += record_length;
    }

    return entries;
}

}
