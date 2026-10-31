#!/usr/bin/env python3
"""Turn a raw 640x256 4bpp console screen dump into a PNG, using the desktop's greys."""
import subprocess, sys
raw = open(sys.argv[1], "rb").read()
out = bytearray(b"P5\n640 256\n255\n")
for byte in raw:
    out.append((byte & 15) * 17)
    out.append((byte >> 4) * 17)
pgm = sys.argv[2] + ".pgm"
open(pgm, "wb").write(out)
subprocess.check_call(["sips", "-s", "format", "png", pgm, "--out", sys.argv[2]], stdout=subprocess.DEVNULL)
