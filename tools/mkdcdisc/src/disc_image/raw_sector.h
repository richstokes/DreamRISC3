/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Assembling one raw 2352-byte sector: where the payload sits inside it, what
 * framing goes around it, and the call into the EDC/ECC encoder that computes
 * the error detection and correction fields.
 *
 * All four image writers need this and each used to carry its own copy, which
 * meant the buffer layout -- an offset that differs between the two sector
 * modes, an XA subheader that only one of them has -- was spelled out four
 * times. It is spelled out here instead.
 *
 * This is also the only place in mkdcdisc that reaches into
 * src/third_party/libedc: the writers ask for a sector and get one, without
 * naming a vendored symbol or knowing which sector type the encoder calls it.
 *
 * The encoder needs the whole 2352 bytes to work in, including the 12-byte sync
 * field it writes itself, so callers that do not store all of it (the CDI
 * writer keeps only 2336 bytes, dropping sync and header) still have to hand
 * over a full buffer and write out the part they want afterwards. */

#include <stdint.h>
#include <stddef.h>

/* A full raw sector, sync field included. */
#define RAW_SECTOR_SIZE 2352

/* User data one sector carries, in either mode. */
#define RAW_SECTOR_USER_DATA_SIZE 2048

/* Where the payload starts, per mode. Mode 1 puts user data straight after the
 * 4-byte header; XA Mode 2 Form 1 fits its 8-byte subheader in between. */
#define RAW_SECTOR_MODE1_DATA_OFFSET 16
#define RAW_SECTOR_FORM1_SUBHEADER_OFFSET 16
#define RAW_SECTOR_FORM1_DATA_OFFSET 24

/* What a CDI data track stores: everything from the subheader on, the 12-byte
 * sync and the 4-byte header dropped (see cd_write_to_cdi()). */
#define RAW_SECTOR_FORM1_STORED_SIZE (RAW_SECTOR_SIZE - RAW_SECTOR_FORM1_SUBHEADER_OFFSET)

/* Builds a complete XA Mode 2 Form 1 sector in `sector`: sync, header, the
 * data subheader, `len` bytes of payload zero-padded to a full 2048, then the
 * EDC and P/Q parity computed over it.
 *
 * `len` may be short -- a track whose length is not a whole number of sectors
 * ends in one such sector -- or zero. Padding happens inside the user-data area,
 * before the parity is computed, so a short sector is still a valid one.
 *
 * `address` is the sector's own address as it should appear in the header.
 * What that counts from is the caller's business and differs between writers;
 * see cd_write_to_cdi(), which deliberately feeds a track-relative number.
 *
 * `sector` must be RAW_SECTOR_SIZE bytes. Its previous contents are discarded. */
void raw_sector_encode_form1(uint8_t* sector, const void* data, size_t len,
                             uint32_t address);

/* The same, for a Mode 1 sector: no subheader, payload at offset 16. */
void raw_sector_encode_mode1(uint8_t* sector, const void* data, size_t len,
                             uint32_t address);
