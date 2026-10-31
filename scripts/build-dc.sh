#!/bin/bash
# Regenerate the recompiled ROM and build the Dreamcast ELF and CDI.
# DC_MAKE_ARGS adds make variables, DC_CDI names the image.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
# Bundled disc-image tool (tools/mkdcdisc). MKDCDISC overrides it with another binary.
# Built with the host toolchain, so this happens before the KOS environment is sourced.
if [ -z "${MKDCDISC:-}" ]; then make mkdcdisc; MKDCDISC=build/mkdcdisc/mkdcdisc; fi
set +u; source "${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}"; set -u
[ "${1:-}" = --skip-gen ] || make gen
make -j"$(getconf _NPROCESSORS_ONLN)" dc ${DC_MAKE_ARGS:-}
# Overlays run at a fixed address inside the ELF, so they link against it last.
region=0x$(sh-elf-nm build/dreamrisc.elf | awk '$3 == "_overlay_region" {print $1}')
bytes=$(grep -o 'OVERLAY_BYTES 0x[0-9a-f]*u' src/cpu.h | sed 's/.* //; s/u$//')
rm -rf build/disc; mkdir -p build/disc
shopt -s nullglob # app units are a single file
cp build/rom.bin build/disc/rom.bin
gunzip -c assets/adfs.adf.gz > build/disc/adfs.adf
for table in build/dc/ov_*_table.o; do
  unit=$(basename "$table" | cut -d_ -f2)
  sh-elf-ld -T tools/overlay.ld --defsym=REGION="$region" --just-symbols=build/dreamrisc.elf \
    "$table" build/dc/ov_"$unit"_[0-9]*.o -o build/ov_"$unit".elf
  sh-elf-objcopy -O binary build/ov_"$unit".elf build/disc/ov_"$unit".bin
  size=$(wc -c < build/disc/ov_"$unit".bin)
  echo "overlay $unit: $size bytes"
  [ "$size" -le $((bytes)) ] || { echo "overlay $unit does not fit in $bytes" >&2; exit 1; }
done

mkdir -p dist
"$MKDCDISC" --allow-overwrite --disable-data-track-padding --main-elf build/dreamrisc.elf --directory-contents build/disc \
  --title 'DreamRISC' --author 'richstokes' --bootstrap-mr-image assets/acorn-logo.png --output "${DC_CDI:-dist/DreamRISC.cdi}"
sh-elf-size build/dreamrisc.elf
