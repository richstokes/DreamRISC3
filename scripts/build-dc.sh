#!/bin/bash
# Regenerate the recompiled ROM and build the Dreamcast ELF and CDI.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
# Bundled disc-image tool (tools/mkdcdisc). MKDCDISC overrides it with another binary.
# Built with the host toolchain, so this happens before the KOS environment is sourced.
if [ -z "${MKDCDISC:-}" ]; then make mkdcdisc; MKDCDISC=build/mkdcdisc/mkdcdisc; fi
set +u; source "${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}"; set -u
[ "${1:-}" = --skip-gen ] || make gen
make -j"$(getconf _NPROCESSORS_ONLN)" dc
mkdir -p dist
"$MKDCDISC" --allow-overwrite --disable-data-track-padding --main-elf build/dreamrisc.elf \
  --title 'DreamRISC' --author 'richstokes' --bootstrap-mr-image assets/acorn-logo.png --output dist/DreamRISC.cdi
sh-elf-size build/dreamrisc.elf
