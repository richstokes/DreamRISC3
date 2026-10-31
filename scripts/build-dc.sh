#!/bin/bash
# Regenerate the recompiled ROM and build the Dreamcast ELF and CDI.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
set +u; source "${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}"; set -u
[ "${1:-}" = --skip-gen ] || make gen
make -j"$(getconf _NPROCESSORS_ONLN)" dc
mkdcdisc=${MKDCDISC:-$(command -v mkdcdisc || echo "$HOME/Dropbox/code/dreamcast-EmuTOS-port/build/tools/mkdcdisc/build/mkdcdisc")}
mkdir -p dist
"$mkdcdisc" --allow-overwrite --disable-data-track-padding --main-elf build/dreamrisc.elf \
  --title 'DreamRISC' --author 'richstokes' --output dist/DreamRISC.cdi
cp build/dreamrisc.elf dist/dreamrisc.elf
sh-elf-size build/dreamrisc.elf
