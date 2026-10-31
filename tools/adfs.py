#!/usr/bin/env python3
"""Add a tree of files to the root of an 800KB ADFS E image.

usage: adfs.py base.adf.gz out.adf src_dir

Files use the usual ",xxx" suffix for their RISC OS type, ",bas" is BASIC text to
tokenise, and directories become directories. The base image is one RISC OS formatted itself, so this only needs
to carve new fragments off the single free run at the end of the map. It checks
what it assumes about the image first and refuses anything else.
"""
import gzip
import pathlib
import struct
import sys

import basic

SECTOR = 1024
DIR_BYTES = 2048
ENTRY = 26
ENTRIES_END = 2007
TAIL_NAME = 2013
TAIL_TITLE = 2032
MAP_BITS_START = 64 * 8
ID_BITS = 15
BIT_BYTES = 128
MAX_ENTRIES = 47
TIMESTAMP = 0x39FE5C7F  # low word of the base image's own files


def ror13(v):
    return ((v >> 13) | (v << 19)) & 0xFFFFFFFF


def dir_check(d):
    end = 5
    while end < ENTRIES_END and d[end]:
        end += ENTRY
    c = p = 0
    while p + 4 <= end:
        c = struct.unpack_from("<I", d, p)[0] ^ ror13(c)
        p += 4
    while p < end:
        c = d[p] ^ ror13(c)
        p += 1
    for p in range(2008, 2044, 4):
        c = struct.unpack_from("<I", d, p)[0] ^ ror13(c)
    return (c ^ (c >> 8) ^ (c >> 16) ^ (c >> 24)) & 0xFF


def zone_check(m):
    v = [0, 0, 0, 0]
    for i in range(SECTOR - 4, 0, -4):
        v[0] += m[i] + (v[3] >> 8)
        v[3] &= 0xFF
        v[1] += m[i + 1] + (v[0] >> 8)
        v[0] &= 0xFF
        v[2] += m[i + 2] + (v[1] >> 8)
        v[1] &= 0xFF
        v[3] += m[i + 3] + (v[2] >> 8)
        v[2] &= 0xFF
    v[0] += v[3] >> 8
    v[1] += m[1] + (v[0] >> 8)
    v[2] += m[2] + (v[1] >> 8)
    v[3] += m[3] + (v[2] >> 8)
    return (v[0] ^ v[1] ^ v[2] ^ v[3]) & 0xFF


class Image:
    def __init__(self, data):
        self.d = bytearray(data)
        assert zone_check(self.d) == self.d[0], "zone check does not match"
        assert self.d[:SECTOR] == self.d[SECTOR:2 * SECTOR], "map copies differ"
        assert self.d[4:8] == bytes((10, 5, 2, 2)) and self.d[8] == ID_BITS and self.d[9] == 7
        self.free = 8 + (struct.unpack_from("<H", self.d, 1)[0] & 0x7FFF)
        assert self.get_bits(self.free) == 0, "expected one free fragment"
        self.next_id = 1 + max(self.get_bits(p) for p in self.fragments())

    def bit(self, p):
        return (self.d[p >> 3] >> (p & 7)) & 1

    def get_bits(self, p):
        return sum(self.bit(p + i) << i for i in range(ID_BITS))

    def set_bit(self, p, v):
        self.d[p >> 3] = (self.d[p >> 3] & ~(1 << (p & 7))) | (v << (p & 7))

    def fragments(self):
        p = MAP_BITS_START
        while p < self.free:
            yield p
            q = p + ID_BITS
            while not self.bit(q):
                q += 1
            p = q + 1

    def allocate(self, data):
        """Write data to a new fragment and return its disc address."""
        sectors = max((len(data) + SECTOR - 1) // SECTOR, 2)
        bits = sectors * SECTOR // BIT_BYTES
        frag_id, self.next_id = self.next_id, self.next_id + 1
        start = self.free
        assert start + bits + ID_BITS < 6400 + MAP_BITS_START, "disc full"
        for i in range(ID_BITS):
            self.set_bit(start + i, (frag_id >> i) & 1)
        for i in range(ID_BITS, bits - 1):
            self.set_bit(start + i, 0)
        self.set_bit(start + bits - 1, 1)
        self.free = start + bits
        for i in range(ID_BITS):
            self.set_bit(self.free + i, 0)
        offset = (start - MAP_BITS_START) * BIT_BYTES
        self.d[offset:offset + len(data)] = data
        return frag_id << 8

    def finish(self):
        struct.pack_into("<H", self.d, 1, 0x8000 | (self.free - 8))
        self.d[0] = zone_check(self.d)
        self.d[SECTOR:2 * SECTOR] = self.d[:SECTOR]


def riscos_name(name):
    return name.encode("latin-1").ljust(10, b"\0")[:10].replace(b"\0", b"\r", 1)


def add_entry(d, name, load, execute, length, address, attrs):
    entries = []
    p = 5
    while d[p]:
        entries.append(bytes(d[p:p + ENTRY]))
        p += ENTRY
    entry = riscos_name(name) if len(name) < 10 else name.encode("latin-1")[:10]
    entries.append(entry + struct.pack("<III", load, execute, length) +
                   address.to_bytes(3, "little") + bytes((attrs,)))
    assert len(entries) <= MAX_ENTRIES, "directory full"
    entries.sort(key=lambda e: e[:10].lower())
    d[5:5 + ENTRY * len(entries)] = b"".join(entries)


def seal(d):
    d[-1] = dir_check(d)


def new_dir(template, name, parent):
    d = bytearray(template)
    d[3 + ENTRIES_END:6 + ENTRIES_END] = parent.to_bytes(3, "little")
    field = riscos_name(name) if len(name) < 10 else name.encode("latin-1")[:10]
    d[TAIL_NAME:TAIL_NAME + 10] = field
    d[TAIL_TITLE:TAIL_TITLE + 10] = field
    return d


def add_tree(image, src, d, address, template):
    for path in sorted(src.iterdir()):
        if path.name.startswith("."):
            continue
        if path.is_dir():
            # Directories are written after their contents, into a held fragment
            child_addr = image.allocate(bytes(DIR_BYTES))
            child = new_dir(template, path.name, address)
            add_tree(image, path, child, child_addr, template)
            seal(child)
            offset = dir_offset(child_addr)
            image.d[offset:offset + DIR_BYTES] = child
            add_entry(d, path.name, 0xFFFFFD44, TIMESTAMP, DIR_BYTES, child_addr, 0x08)
        else:
            name, _, kind = path.name.partition(",")
            data = path.read_bytes()
            if kind == "bas":
                data, kind = basic.tokenise(data.decode("latin-1")), "ffb"
            filetype = int(kind or "fff", 16)
            load = 0xFFF00000 | (filetype << 8) | 0x44
            add_entry(d, name, load, TIMESTAMP, len(data), image.allocate(data), 0x33)


fragments = {}


def dir_offset(address):
    return fragments[address >> 8]


def main():
    base, out, src = sys.argv[1:]
    image = Image(gzip.open(base).read())
    # allocate() places fragments in order, so remember where each one lands
    original = image.allocate

    def allocate(data):
        offset = (image.free - MAP_BITS_START) * BIT_BYTES
        address = original(data)
        fragments[address >> 8] = offset
        return address

    image.allocate = allocate
    root = bytearray(image.d[0x800:0x800 + DIR_BYTES])
    template = bytes(image.d[0x1800:0x1800 + DIR_BYTES])
    assert dir_check(root) == root[-1] and dir_check(template) == template[-1]
    add_tree(image, pathlib.Path(src), root, 0x000203, template)
    seal(root)
    image.d[0x800:0x800 + DIR_BYTES] = root
    image.finish()
    pathlib.Path(out).write_bytes(image.d)


if __name__ == "__main__":
    main()
