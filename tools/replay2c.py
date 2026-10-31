#!/usr/bin/env python3
"""Convert a .replay file to a C table for hardware tests: replay2c.py file offset_seconds."""
import sys
offset = float(sys.argv[2])
print("static const replay_t replay_events[] = {")
shot = 0
for line in open(sys.argv[1]):
    parts = line.split()
    if not parts or parts[0].startswith("#"):
        continue
    ms = int((float(parts[0]) + offset) * 1000)
    if parts[1] == "move":
        print(f"{{{ms}, 'm', {parts[2]}, {parts[3]}}},")
    elif parts[1] == "button":
        print(f"{{{ms}, 'b', {parts[2]}, 0}},")
    elif parts[1] == "shot":
        shot += 1
        print(f"{{{ms}, 's', {shot}, 0}},")
print("};")
