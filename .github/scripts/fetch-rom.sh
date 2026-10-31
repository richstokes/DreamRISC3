#!/bin/bash
# Download the 2MB ROM image from $ROM_URL, check it and split it into the chip
# files the build expects. GitHub secrets are capped at 48KB, so the secret
# holds a private URL, not the ROM.
set -euo pipefail
: "${GITHUB_OUTPUT:?}"
: "${ROM_SHA256:?}"
output=${1:?usage: fetch-rom.sh <directory>}

if [ -z "${ROM_URL:-}" ]; then
    echo '::notice::ROM_URL secret is not available, skipping the build'
    echo 'ready=false' >> "$GITHUB_OUTPUT"
    exit 0
fi

mkdir -p "$output"
curl --fail --silent --show-error --location --retry 3 --output "$output/rom.bin" "$ROM_URL"
echo "$ROM_SHA256  $output/rom.bin" | sha256sum --check --quiet

index=0
for chip in IC24.ROM IC25.ROM IC26.rom IC27.ROM; do
    dd if="$output/rom.bin" of="$output/$chip" bs=524288 skip=$index count=1 status=none
    index=$((index + 1))
done
rm "$output/rom.bin"
echo 'ready=true' >> "$GITHUB_OUTPUT"
