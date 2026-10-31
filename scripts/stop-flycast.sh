#!/bin/bash
# Stop only the Flycast this checkout launched. Other sessions run their own.
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
file=$root/build/flycast.pid
[ -f "$file" ] || exit 0
pid=$(cat "$file")
if ps -p "$pid" -o command= | grep -q Flycast; then kill "$pid"; fi
rm -f "$file"
