#!/usr/bin/env python3
"""Turn fbdump blocks in a serial log into PPM files: fbdump.py log prefix."""
import sys

lines = open(sys.argv[1], errors="replace").read().split("\n")
for index, line in enumerate(lines):
    if line.startswith("fbdump "):
        rows = lines[index + 1:index + 481]
        with open(f"{sys.argv[2]}{line.split()[1]}.ppm", "wb") as out:
            out.write(b"P6\n640 480\n255\n")
            for row in rows:
                for x in range(0, 2560, 4):
                    v = int(row[x:x + 4], 16)
                    out.write(bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)))
