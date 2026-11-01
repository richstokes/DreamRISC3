#!/usr/bin/env python3
"""ADFS disc images: read the files off a floppy, write a populated new-map hard disc.

Reading covers old-map (L, D) and new-map (E) floppies with either directory format.
Writing lays down the same disc hdformat.c does, with the files already in place:
512 byte sectors, 15 bit ids, zone spare 32, both map copies and the root at the
start of the middle zone. Run as a script it lists an image.
"""
import gzip
import struct
import sys

SECTOR = 512
ZONE_BITS = SECTOR * 8
ZONE_SPARE = 32
ZONE0_BITS = 480  # the disc record sits in zone 0
ZONE_ALLOC = ZONE_BITS - ZONE_SPARE
ID_LEN = 15
BAD_ID = 1
ROOT_ID = 2
FIRST_ID = 3
DIR_BYTES = 2048
DIR_ENTRIES = 77
ENTRY = 26
MAX_ZONES = 120  # keeps the root's sector offset within one byte
BOOT_ADDR = 0xC00
HEADS, SPT = 16, 63
NAME_LEN = 10
FLOPPY_SPT = 5
BAD_CHARS = set('$&%@\\^:.#*"| ')


class Entry:
    """A file (data) or directory (children, by name)."""

    def __init__(self, name, load=0, exec_=0, attrs=0, data=None, children=None):
        self.name = name
        self.load = load
        self.exec_ = exec_
        self.attrs = attrs
        self.data = data
        self.children = children if children is not None else ({} if data is None else None)

    @property
    def is_dir(self):
        return self.data is None

    def add(self, entry):
        self.children[entry.name.lower()] = entry
        return entry

    def get(self, name):
        return self.children.get(name.lower())

    def subdir(self, name):
        """The named child directory, made if missing."""
        child = self.get(name)
        if child is None:
            child = self.add(Entry(name, attrs=0x08))
        return child

    def walk(self, path=""):
        for child in sorted(self.children.values(), key=lambda e: e.name.lower()):
            here = path + "." + child.name if path else child.name
            yield here, child
            if child.is_dir:
                yield from child.walk(here)


def valid_name(name):
    """Legal on an ADFS disc: ten characters at most, none that FileCore reserves."""
    return (0 < len(name) <= NAME_LEN and not (set(name) & BAD_CHARS)
            and all(32 < ord(c) < 127 or 0xA0 <= ord(c) < 256 for c in name))


# --- reading ---


def _entries(block):
    fmt = block[1:5]
    if fmt not in (b"Nick", b"Hugo"):
        raise ValueError("not a directory")
    count = DIR_ENTRIES if fmt == b"Nick" else 47
    for i in range(count):
        r = block[5 + i * ENTRY:5 + (i + 1) * ENTRY]
        if r[0] == 0:
            break
        raw = r[:NAME_LEN]
        name = ""
        for c in raw:
            if c & 0x7F < 32:
                break
            name += chr(c & 0x7F)
        load, exec_, length = struct.unpack("<III", r[10:22])
        address = r[22] | r[23] << 8 | r[24] << 16
        attrs = r[25]
        if not attrs:  # old directories keep the attributes in the name's top bits
            for bit, out in ((0, 1), (1, 2), (2, 4), (3, 8), (5, 0x10), (6, 0x20)):
                if raw[bit] & 0x80:
                    attrs |= out
        yield name, load, exec_, length, address, attrs


def _new_map_fragments(image, rec):
    """Fragment id -> [(byte address, byte length)] in zone order."""
    sector = 1 << rec[0]  # a zone is one sector of map
    zone_bits = sector * 8
    log2_bpmb, zones = rec[5], rec[9]
    zone_spare = struct.unpack("<H", rec[10:12])[0]
    alloc = zone_bits - zone_spare
    bpmb = 1 << log2_bpmb
    idlen = rec[4]
    mid = zones // 2
    map_addr = (mid * alloc - ZONE0_BITS) * bpmb if mid else 0
    frags = {}
    for z in range(zones):
        zone = image[map_addr + z * sector:map_addr + (z + 1) * sector]
        bit = lambda n: (zone[n >> 3] >> (n & 7)) & 1
        # Allocation bits follow the 4 byte header (and the record in zone 0). Any
        # spare beyond that is unused space at the end of the zone
        base = z * alloc - ZONE0_BITS - 32  # global bit = base + local bit
        p = 32 + (ZONE0_BITS if z == 0 else 0)
        # Free fragments are chained from the header, and their id field is the link
        link = (zone[1] | zone[2] << 8) & 0x7FFF
        gap = 8 + link if link else None
        while p < zone_bits - (zone_spare - 32):
            v = 0
            for i in range(idlen):
                v |= bit(p + i) << i
            q = p + idlen
            while q < zone_bits and not bit(q):
                q += 1
            q += 1
            if q > zone_bits:
                break
            if p == gap:
                gap = p + v if v else None
            else:
                frags.setdefault(v, []).append(((base + p) * bpmb, (q - p) * bpmb))
            p = q
    # Object 2 holds the root, and FileCore starts it at the map zone
    if 2 in frags:
        frags[2].sort(key=lambda piece: piece[0] < map_addr)
    return frags, sector, struct.unpack("<I", rec[12:16])[0]


def read_jfd(raw):
    """A flat 1K-sector image from a JFD track dump, and the byte offsets of sectors
    it lacks. Copy protection lives in extra or odd tracks; the standard sectors
    around it are taken by their ids, whatever order they come in."""
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    if raw[:4] != b"JFDI":
        raise ValueError("not a JFD")
    tracks_at, sectors_at, data_at = struct.unpack("<3I", raw[24:36])
    if data_at == 0xFFFFFFFF:
        raise ValueError("JFD holds no data")
    count = (sectors_at - tracks_at) // 4
    tracks = struct.unpack("<%dI" % count, raw[tracks_at:sectors_at])

    def sectors(track):
        at = sectors_at + tracks[track]
        while struct.unpack("<I", raw[at:at + 4])[0] != 0xFFFFFFFF:
            yield struct.unpack("<II", raw[at:at + 8])
            at += 8

    sizes = [sum(h & 3 == 3 for h, _ in sectors(t)) for t in range(count) if tracks[t] != 0xFFFFFFFF]
    spt = 10 if sizes and max(set(sizes), key=sizes.count) > 5 else FLOPPY_SPT  # F or D/E
    image = bytearray(160 * spt * 1024)
    holes = set()
    for track in range(160):
        have = {}
        if track < count and tracks[track] != 0xFFFFFFFF:
            for header, offset in sectors(track):
                number = (header >> 8) & 255
                if header & 3 == 3 and not header & 0x30 and number < spt:
                    have.setdefault(number, offset)
        for number in range(spt):
            where = (track * spt + number) * 1024
            if number in have:
                start = data_at + have[number]
                image[where:where + 1024] = raw[start:start + 1024]
            else:
                holes.add(where)
    return bytes(image), holes


def read_floppy(image, problems=None, holes=()):
    """The root Entry of an ADFS image. Raises ValueError for anything else.

    Dumps are often a few K short. Reads past the end come back as zeros, as off a
    real disc with a bad track, and whatever that breaks is noted in problems. A
    hard disc image reads too, through the record in its boot block. holes lists
    the offsets of 1K sectors known to be missing, and a file on one is a problem."""
    if len(image) < 0x1000:
        raise ValueError("too small")
    if problems is None:
        problems = []
    image = bytes(image) + bytes(0xC8000 - len(image)) if len(image) < 0xC8000 else image
    missing = [0]

    def touch(start, length):
        if holes:
            for at in range(start & ~1023, start + length, 1024):
                missing[0] += at in holes

    if image[0x401:0x405] in (b"Nick", b"Hugo"):
        root_addr, unit = 0x400, 256
    elif image[0x201:0x205] == b"Hugo":
        root_addr, unit = 0x200, 256
    else:
        root_addr = unit = None
    if unit:
        def read(address, length):
            start = address * unit
            touch(start, length)
            return image[start:start + length]

        root_at = root_addr // unit
    else:
        rec = image[4:64]  # a floppy's map is at the start
        if rec[0] not in (8, 9, 10) or rec[4] < 8 or not rec[9]:
            rec = image[BOOT_ADDR + 0x1C0:BOOT_ADDR + 0x1C0 + 60]
        if rec[0] not in (8, 9, 10) or rec[4] < 8 or not rec[9]:
            raise ValueError("no ADFS map")
        frags, sector, root_at = _new_map_fragments(image, rec)

        def read(address, length):
            if not length:
                return b""
            pieces = frags.get(address >> 8)
            if not pieces:
                raise ValueError("fragment %x missing" % (address >> 8))
            offset = ((address & 255) - 1) * sector if address & 255 else 0
            skip, left = offset, length
            for a, n in pieces:
                use = min(n - skip, left) if skip < n else 0
                if use > 0:
                    touch(a + skip, use)
                    left -= use
                skip = max(0, skip - n)
            buf = b"".join(image[a:a + n] for a, n in pieces)
            return buf[offset:offset + length]

    def walk(address, name, path):
        node = Entry(name, attrs=0x08)
        for child, load, exec_, length, where, attrs in _entries(read(address, DIR_BYTES)):
            if attrs & 8:
                try:
                    sub = walk(where, child, path + "." + child)
                except ValueError as e:
                    problems.append("%s: %s" % (path + "." + child, e))
                    continue
                sub.load, sub.exec_, sub.attrs = load, exec_, attrs
                node.add(sub)
            else:
                missing[0] = 0
                data = read(where, length)
                if len(data) != length:
                    problems.append("%s.%s: %d of %d bytes" % (path, child, len(data), length))
                    data = data + bytes(length - len(data))
                if missing[0]:
                    problems.append("%s.%s: on a missing sector" % (path, child))
                node.add(Entry(child, load, exec_, attrs, data))
        return node

    return walk(root_at, "$", "$")


# --- writing ---


def _put32(buf, at, v):
    buf[at:at + 4] = struct.pack("<I", v & 0xFFFFFFFF)


def _put_bits(zone, pos, value, count):
    for i in range(count):
        if value >> i & 1:
            zone[(pos + i) >> 3] |= 1 << ((pos + i) & 7)


def _zone_check(b):
    s0 = s1 = s2 = s3 = 0
    for r in range(SECTOR - 4, 0, -4):
        s0 += b[r] + (s3 >> 8)
        s3 &= 255
        s1 += b[r + 1] + (s0 >> 8)
        s0 &= 255
        s2 += b[r + 2] + (s1 >> 8)
        s1 &= 255
        s3 += b[r + 3] + (s2 >> 8)
        s2 &= 255
    s0 += s3 >> 8
    s1 += b[1] + (s0 >> 8)
    s2 += b[2] + (s1 >> 8)
    s3 += b[3] + (s2 >> 8)
    return (s0 ^ s1 ^ s2 ^ s3) & 255


def _boot_check(b):
    total = carry = 0
    for i in range(510, -1, -1):
        total = total + b[i] + carry
        carry = total >> 8
        total &= 255
    return total


def _dir_check(b, used):
    """Words over the used entries, the odd bytes after them, then the tail."""
    h = 0
    rot = lambda h: ((h >> 13) | (h << 19)) & 0xFFFFFFFF
    end = 5 + used * ENTRY
    i = 0
    while i + 4 <= end:
        h = rot(h) ^ struct.unpack_from("<I", b, i)[0]
        i += 4
    while i < end:
        h = rot(h) ^ b[i]
        i += 1
    for i in range(DIR_BYTES - 40, DIR_BYTES - 4, 4):
        h = rot(h) ^ struct.unpack_from("<I", b, i)[0]
    return (h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) & 255


def _record(zones, log2_bpmb, size, root, name):
    r = bytearray(60)
    r[0] = 9
    r[1] = SPT
    r[2] = HEADS
    r[4] = ID_LEN
    r[5] = log2_bpmb
    r[9] = zones
    r[10] = ZONE_SPARE
    _put32(r, 12, root)
    _put32(r, 16, size)
    r[20], r[21] = 0x5A, 0xA5
    r[22:32] = name.encode("latin-1")[:10].ljust(10)
    return r


class _Frag:
    def __init__(self, bits, objects):
        self.bits = bits
        self.objects = objects  # [(entry, sector offset)]
        self.pieces = []  # (zone, local bit, bits) once placed
        self.id = 0


class TooBig(Exception):
    pass


def build_disc(root, name, log2_bpmb, min_zones=3):
    """Image bytes for the tree under root, or TooBig when it needs more zones.

    Small files share a fragment with their siblings and nothing else, which is the
    only sharing *CheckMap follows (see ReallyShared in FileCore)."""
    bpmb = 1 << log2_bpmb
    bin_bits = ID_LEN + 1
    bin_bytes = bin_bits * bpmb
    sectors_per_bin = bin_bytes // SECTOR
    # Objects are read by the sector, so with map units smaller than a sector every
    # fragment has to start and end on a sector boundary
    align = max(1, SECTOR // bpmb)
    aligned = lambda bits: -(-bits // align) * align

    objects = []
    for path, entry in root.walk():
        if not valid_name(entry.name):
            raise ValueError("bad name %r" % path)
        if entry.is_dir and len(entry.children) > DIR_ENTRIES:
            raise ValueError("%s has %d entries" % (path, len(entry.children)))
        objects.append(entry)

    size_of = lambda e: DIR_BYTES if e.is_dir else len(e.data)
    sectors_of = lambda e: max(1, (size_of(e) + SECTOR - 1) // SECTOR)
    # An empty file owns no fragment; FileCore gives it disc address 1 (id 0), and
    # *CheckMap counts anything else against the map
    empty = [e for e in objects if not e.is_dir and not e.data]
    objects = [e for e in objects if e.is_dir or e.data]
    parent = {id(c): id(root) for c in root.children.values()}
    for _, entry in root.walk():
        if entry.is_dir:
            parent.update((id(c), id(entry)) for c in entry.children.values())
    frags = []
    bins = {}  # parent -> [[sectors left, frag]]
    for entry in sorted(objects, key=size_of, reverse=True):
        if size_of(entry) > bin_bytes or entry.is_dir:
            frags.append(_Frag(aligned(max(bin_bits, (size_of(entry) + bpmb - 1) // bpmb)), [(entry, 0)]))
            continue
        need = sectors_of(entry)
        siblings = bins.setdefault(parent[id(entry)], [])
        for b in siblings:
            if b[0] >= need:
                b[1].objects.append((entry, sectors_per_bin - b[0]))
                b[0] -= need
                break
        else:
            frag = _Frag(bin_bits, [(entry, 0)])
            siblings.append([sectors_per_bin - need, frag])
            frags.append(frag)
    if len(frags) + FIRST_ID >= 1 << ID_LEN:
        raise TooBig("%d fragments" % len(frags))

    defect_bits = aligned(max(bin_bits, (0x1000 + bpmb - 1) // bpmb))
    total = defect_bits + sum(f.bits for f in frags)
    zones = None
    for z in range(min_zones, MAX_ZONES + 1):
        map_bits = aligned(max(bin_bits, (2 * z * SECTOR + DIR_BYTES + bpmb - 1) // bpmb))
        # Zone ends waste a little, so leave room for that
        if total + map_bits + z * bin_bits + ZONE0_BITS <= z * ZONE_ALLOC:
            zones, root_bits = z, map_bits
            break
    if zones is None:
        raise TooBig("%d bits" % total)
    mid = zones // 2

    # Place fragments: biggest first, first zone with room, splitting only when none
    # has. Every zone keeps a free fragment at its end, as a disc FileCore formatted
    # always has one, so the free chain never has to express a full zone.
    free = [ZONE_ALLOC - (ZONE0_BITS if z == 0 else 0) - bin_bits for z in range(zones)]
    used = [0] * zones
    placed = []

    def place(frag, z, bits):
        start = ZONE_SPARE + (ZONE0_BITS if z == 0 else 0) + used[z]
        frag.pieces.append((z, start, bits))
        used[z] += bits
        free[z] -= bits
        placed.append(frag)

    defect = _Frag(defect_bits, [])
    defect.id = BAD_ID
    place(defect, 0, defect_bits)
    root_frag = _Frag(root_bits, [])
    root_frag.id = ROOT_ID
    place(root_frag, mid, root_bits)
    for frag in sorted(frags, key=lambda f: f.bits, reverse=True):
        for z in range(zones):
            if free[z] >= frag.bits:
                place(frag, z, frag.bits)
                break
        else:
            if len(frag.objects) != 1 or frag.objects[0][1]:
                raise TooBig("cannot split a shared fragment")
            left = frag.bits
            for z in range(zones):
                if left and free[z] >= 2 * bin_bits:
                    take = min(left, free[z] // align * align)
                    if left - take and left - take < bin_bits:
                        take = left - bin_bits
                    place(frag, z, take)
                    left -= take
            if left:
                raise TooBig("no room for %d bits" % left)
    for z in range(zones):
        free[z] += bin_bits

    next_id = FIRST_ID
    for frag in frags:
        frag.id = next_id
        next_id += 1

    total_bits = zones * ZONE_ALLOC - ZONE0_BITS
    size = total_bits * bpmb
    image = bytearray(size)
    root_addr = (ROOT_ID << 8) | (2 * zones + 1)
    map_addr = (mid * ZONE_ALLOC - ZONE0_BITS) * bpmb

    def byte_address(z, local):
        base = 0 if z == 0 else z * ZONE_ALLOC - ZONE0_BITS
        return (base + local - ZONE_SPARE - (ZONE0_BITS if z == 0 else 0)) * bpmb

    address_of = {id(e): 1 for e in empty}
    for frag in frags:
        for entry, offset in frag.objects:
            address_of[id(entry)] = (frag.id << 8) | (offset + 1 if len(frag.objects) > 1 else 0)
    address_of[id(root)] = root_addr

    def serialise(entry, parent_addr):
        b = bytearray(DIR_BYTES)
        b[0] = 1
        b[1:5] = b"Nick"
        children = sorted(entry.children.values(), key=lambda e: e.name.lower())
        for i, child in enumerate(children):
            at = 5 + i * ENTRY
            b[at:at + NAME_LEN] = (child.name.encode("latin-1") + b"\r").ljust(NAME_LEN, b"\0")[:NAME_LEN]
            struct.pack_into("<III", b, at + 10, child.load, child.exec_, size_of(child) if not child.is_dir else DIR_BYTES)
            addr = address_of[id(child)]
            b[at + 22:at + 25] = struct.pack("<I", addr)[:3]
            b[at + 25] = child.attrs
        b[DIR_BYTES - 38:DIR_BYTES - 35] = struct.pack("<I", parent_addr)[:3]
        title = entry.name.encode("latin-1")
        b[DIR_BYTES - 35:DIR_BYTES - 35 + min(19, len(title))] = title[:19]
        b[DIR_BYTES - 16:DIR_BYTES - 16 + min(10, len(title))] = title[:10]
        b[DIR_BYTES - 6] = 1
        b[DIR_BYTES - 5:DIR_BYTES - 1] = b"Nick"
        b[DIR_BYTES - 1] = _dir_check(b, len(children))
        return b

    parent_of = {id(root): root_addr}
    for entry in [root] + [e for _, e in root.walk() if e.is_dir]:
        for child in entry.children.values():
            parent_of[id(child)] = address_of[id(entry)]
    for frag in frags:
        start = byte_address(*frag.pieces[0][:2])
        if len(frag.pieces) > 1:
            entry = frag.objects[0][0]
            data = entry.data
            for z, local, bits in frag.pieces:
                at = byte_address(z, local)
                chunk, data = data[:bits * bpmb], data[bits * bpmb:]
                image[at:at + len(chunk)] = chunk
            continue
        for entry, offset in frag.objects:
            at = start + offset * SECTOR
            data = serialise(entry, parent_of[id(entry)]) if entry.is_dir else entry.data
            image[at:at + len(data)] = data

    rec = _record(zones, log2_bpmb, size, root_addr, name)
    cross = 0
    for z in range(zones):
        zone = bytearray(SECTOR)
        if z == 0:
            zone[4:64] = rec
        for frag in placed:
            for zz, local, bits in frag.pieces:
                if zz == z:
                    _put_bits(zone, local, frag.id, ID_LEN)
                    _put_bits(zone, local + bits - 1, 1, 1)
        _put_bits(zone, ZONE_BITS - 1, 1, 1)
        if free[z]:
            free_at = ZONE_SPARE + (ZONE0_BITS if z == 0 else 0) + used[z]
            zone[1] = (free_at - 8) & 255
            zone[2] = ((free_at - 8) >> 8) | 0x80
        zone[3] = (cross ^ 0xFF) if z == zones - 1 else 0
        cross ^= zone[3]
        zone[0] = _zone_check(zone)
        for copy in range(2):
            at = map_addr + (copy * zones + z) * SECTOR
            image[at:at + SECTOR] = zone
    at = map_addr + 2 * zones * SECTOR
    image[at:at + DIR_BYTES] = serialise(root, root_addr)

    boot = bytearray(SECTOR)
    _put32(boot, 0, 0x20000000)  # empty defect list
    boot[0x1C0:0x1C0 + 60] = rec
    boot[SECTOR - 1] = _boot_check(boot)
    image[BOOT_ADDR:BOOT_ADDR + SECTOR] = boot

    track = HEADS * SPT * SECTOR
    image += bytes(-len(image) % track)
    return bytes(image)


def build_smallest(root, name):
    """The image at the finest map unit that fits, and its log2 unit size."""
    for log2_bpmb in range(7, 13):
        try:
            return build_disc(root, name, log2_bpmb), log2_bpmb
        except TooBig:
            continue
    raise TooBig("even 4K units")


if __name__ == "__main__":
    tree = read_floppy(open(sys.argv[1], "rb").read())
    for where, item in tree.walk():
        print("%-40s %08X %08X %8d %02X" % (where, item.load, item.exec_, 0 if item.is_dir else len(item.data), item.attrs))
