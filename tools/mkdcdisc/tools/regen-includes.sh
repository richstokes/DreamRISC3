#!/usr/bin/env bash
#
# mkdcdisc - Sega Dreamcast Disc Image Generator
#
# SPDX-License-Identifier: MIT
# Copyright (C) 2022-2026 Simulant and contributors
# (https://www.simulant.dev/)
#
# Regenerates the committed C-array includes in src/assets/ from the binary
# assets in assets/. These includes are what main.cpp embeds; they are checked
# in so that a normal build needs no extra tools. This script is only for
# maintainers who changed one of the source binaries -- run it via the Meson
# target (`ninja regen-includes` / `meson compile regen-includes`) or directly.
#
# It requires `xxd` (from Vim). The normal build does NOT: that is the whole
# point of committing the generated includes.
#
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v xxd >/dev/null 2>&1; then
    echo "error: xxd not found: it is needed only to regenerate these includes," >&2
    echo "       not to build mkdcdisc. Install Vim (which provides xxd) and retry." >&2
    exit 1
fi

# gen <binary-asset> <output-include> <C-symbol-name>
#
# xxd -i names the array after the input file; we rewrite it to the symbol
# main.cpp actually references, so the asset filename and the C name can differ.
gen() {
    _in="$1"; _out="$2"; _sym="$3"
    ( cd "$(dirname "$_in")" && xxd -i "$(basename "$_in")" ) \
      | sed -e "s/unsigned char [A-Za-z0-9_]*\[\]/unsigned char ${_sym}[]/" \
            -e "s/unsigned int [A-Za-z0-9_]*_len/unsigned int ${_sym}_len/" \
      > "$_out"
    echo "  ${_out}  <-  ${_in}  (${_sym})"
}

echo "Regenerating src/assets/*.include from assets/:"
gen assets/ip/cd/IP.BIN          src/assets/ip_cd.include                   IP_BIN_MILCD
gen assets/ip/gd/nearest/IP.BIN  src/assets/ip_gd.hda.nearest.include       IP_BIN_GDROM_HDA_NEAREST
gen assets/ip/gd/zero/IP.BIN     src/assets/ip_gd.hda.zero.include          IP_BIN_GDROM_HDA_ZERO
gen assets/iplogo/default-mr.mr  src/assets/iplogo.default.include          IP_BIN_DEFAULT_MR
echo "Done."
