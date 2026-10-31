#!/usr/bin/env python3
"""Summarise an Arculator ARC_PCMAP trace against the ROM module map."""
import argparse
import collections
import struct

from romscan import ROM_BASE, find_chain, load_rom, module_info


def rom_hits(bits):
    hits = []
    start = ROM_BASE >> 5
    for index in range(start, start + (0x200000 >> 5)):
        byte = bits[index]
        if byte:
            for bit in range(8):
                if byte & (1 << bit):
                    hits.append((index * 8 + bit) * 4 - ROM_BASE)
    return hits


def low_hits(bits):
    return sum(bin(b).count("1") for b in bits[:ROM_BASE >> 5])


def ram_entries(blob):
    for (key,) in struct.iter_unpack("<Q", blob):
        if key:
            yield key & 0xFFFFFFFE, key >> 32


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rom_dir")
    parser.add_argument("trace", help="ARC_PCMAP prefix")
    args = parser.parse_args()
    rom = load_rom(args.rom_dir)
    with open(args.trace + ".bits", "rb") as f:
        bits = f.read()
    with open(args.trace + ".ram", "rb") as f:
        ram = list(ram_entries(f.read()))

    hits = rom_hits(bits)
    modules = [module_info(rom, offset) for offset in find_chain(rom)]
    per_module = collections.Counter()
    for pc in hits:
        name = "kernel"
        for m in modules:
            if m["offset"] <= pc < m["offset"] + m["length"]:
                name = m["title"]
                break
        per_module[name] += 1
    print(f"ROM instructions executed: {len(hits)} ({len(hits) * 4} bytes)")
    for name, count in per_module.most_common():
        print(f"  {count:7d} {name}")

    print(f"low-memory distinct PCs: {low_hits(bits)}, distinct (pc, insn) in RAM: {len(ram)}")
    regions = collections.Counter()
    for pc, _ in ram:
        regions[pc & ~0xFFFF] += 1
    for base, count in sorted(regions.items()):
        print(f"  {base:#09x} {count}")


if __name__ == "__main__":
    main()
