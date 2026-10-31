/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Raw sector assembly: see raw_sector.h. */

#include <string.h>

#include "raw_sector.h"
#include "third_party/libedc/libedc.h"

/* The subheader every data sector on a MIL-CD carries. Recorded twice, as XA
 * requires: the two copies let a drive recover the field from either one.
 *
 * 0x09 is the submode byte -- 0x08 "data" plus 0x01 "end of record" -- which is
 * what every disc mkdcdisc has written since the beginning. */
static const uint8_t FORM1_SUBHEADER[8] = {
    0x00, 0x00, 0x09, 0x00,
    0x00, 0x00, 0x09, 0x00
};

/* Zeroes the sector and drops `len` bytes of payload at `offset`, leaving the
 * rest of the user-data area zeroed. Everything outside it is zeroed too, which
 * matters for the 8 spare bytes a Mode 1 sector carries after its EDC: the
 * encoder does not write those, and they are covered by the parity. */
static void place_payload(uint8_t* sector, size_t offset,
                          const void* data, size_t len) {
    memset(sector, 0, RAW_SECTOR_SIZE);

    if(len > RAW_SECTOR_USER_DATA_SIZE) {
        len = RAW_SECTOR_USER_DATA_SIZE;
    }
    if(len && data) {
        /* len is clamped to RAW_SECTOR_USER_DATA_SIZE just above.
         * nosemgrep: insecure-use-memcpy-fn */
        memcpy(sector + offset, data, len);
    }
}

void raw_sector_encode_form1(uint8_t* sector, const void* data, size_t len,
                             uint32_t address) {
    place_payload(sector, RAW_SECTOR_FORM1_DATA_OFFSET, data, len);
    /* 8 bytes at a fixed offset inside a RAW_SECTOR_SIZE sector.
     * nosemgrep: insecure-use-memcpy-fn */
    memcpy(sector + RAW_SECTOR_FORM1_SUBHEADER_OFFSET,
           FORM1_SUBHEADER, sizeof(FORM1_SUBHEADER));

    do_encode_L2(sector, MODE_2_FORM_1, (unsigned) address);
}

void raw_sector_encode_mode1(uint8_t* sector, const void* data, size_t len,
                             uint32_t address) {
    place_payload(sector, RAW_SECTOR_MODE1_DATA_OFFSET, data, len);

    do_encode_L2(sector, MODE_1, (unsigned) address);
}
