#!/usr/bin/env python3
"""Fetch the Archimedes software archive and bundle it as an ADFS hard disc image.

Each zip the archive lists is downloaded once into the cache and the floppy inside it
unpacked onto the disc under Apps, Games, Demos or Compilatns, one folder per title.
Variant dumps ([a], [b], [cr] and so on) of a title are skipped when a plain one is
there, and the disks of a multi-disk title share a folder. A ReadMe in the root maps
each folder back to its archive name.

usage: archive-hd.py OUT [--cache DIR] [--index URL] [--limit N] [--refresh]
"""
import argparse
import html
import os
import re
import sys
import time
import urllib.parse
import urllib.request
import zipfile

import adfs

INDEX = ("https://ia903209.us.archive.org/view_archive.php?archive=/24/items/"
         "Acorn_Archimedes_TOSEC_2012_04_23/Acorn_Archimedes_TOSEC_2012_04_23.zip")
CACHE = "/tmp/archimedes-archive"
FOLDERS = {"Applications": "Apps", "Games": "Games", "Demos": "Demos", "Compilations": "Compilatns"}
DISC_NAME = "Archive"
BAD_DUMP = ("b", "o", "u", "v")  # TOSEC flags for dumps known to be broken


def fetch(url, path, refresh=False):
    """Download url to path unless it is cached. Three tries, then give up."""
    if os.path.exists(path) and not refresh:
        return True
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=60) as r, open(path + ".part", "wb") as f:
                f.write(r.read())
            os.replace(path + ".part", path)
            return True
        except Exception as e:  # noqa: BLE001 - any failure just means another try
            print("  retry %s: %s" % (os.path.basename(path), e), file=sys.stderr)
            time.sleep(2 * (attempt + 1))
    return False


def listing(cache, index_url, refresh):
    """[(download url, archive path)] for every zip in the archive's index page."""
    path = os.path.join(cache, "index.html")
    if not fetch(index_url, path, refresh) and not os.path.exists(path):
        sys.exit("cannot fetch the archive index and nothing is cached")
    text = open(path, encoding="utf-8", errors="replace").read()
    rows = []
    for m in re.finditer(r'<a href="(//archive\.org/download/[^"]+\.zip)">', text):
        url = html.unescape(m.group(1))
        rows.append(("https:" + url, urllib.parse.unquote(url.split(".zip/", 1)[1])))
    if not rows:
        sys.exit("the index page lists no zips; the archive's layout may have changed")
    return rows


def parse_name(stem):
    """TOSEC name -> (title, disk number or 0, variant rank: 0 plain, 1 alternate, 2 bad)."""
    title = stem.split(" (", 1)[0].strip()
    disk = re.search(r"\(Disk (\d+) of \d+\)", stem)
    flags = re.findall(r"\[([a-z]+)[^\]]*\]", stem)
    rank = 0
    for f in flags:
        if f in BAD_DUMP:
            rank = 2
        elif f in ("a", "cr", "h", "t", "f", "m", "p", "tr") and rank < 1:
            rank = 1
    return title, int(disk.group(1)) if disk else 0, rank


def folder_name(title, taken):
    """A unique ADFS name for the title: the words run together, cut to ten.

    A cut prefix (OhNoMoreLe) reads better in a Filer window than dropped vowels,
    and keeps the alphabetical order of the full titles. The ReadMe has the rest."""
    words = re.findall(r"[A-Za-z0-9]+", title) or ["Untitled"]
    name = "".join(w[:1].upper() + w[1:] for w in words)
    number = re.search(r" (\d+)$", title)
    if len(name) > adfs.NAME_LEN and number:
        # Multi-Program 14 stays MultiPro14, not a numbered clash with the others
        name = name[:adfs.NAME_LEN - len(number.group(1))] + number.group(1)
    name = name[:adfs.NAME_LEN]
    base, n = name, 2
    while name.lower() in taken:
        suffix = str(n)
        name = base[:adfs.NAME_LEN - len(suffix)] + suffix
        n += 1
    taken.add(name.lower())
    return name


def legal(name):
    """Make a floppy's file name legal on the hard disc (spaces become hard spaces)."""
    out = "".join(chr(0xA0) if c == " " else ("_" if c in adfs.BAD_CHARS else c) for c in name)
    return out[:adfs.NAME_LEN] or "_"


def sanitise(node):
    """Rename everything under node to names the hard disc accepts."""
    children = list(node.children.values())
    node.children = {}
    for child in children:
        child.name = legal(child.name)
        if child.is_dir:
            sanitise(child)
        node.add(child)


def merge(dest, src, into_disk=None):
    """Copy src's children into dest. A clash that is not the same bytes goes under DiskN."""
    for child in src.children.values():
        have = dest.get(child.name)
        if have is None:
            dest.add(child)
        elif have.is_dir and child.is_dir:
            merge(have, child, into_disk)
        elif not have.is_dir and not child.is_dir and have.data == child.data:
            continue
        elif into_disk:
            dest.subdir(into_disk).add(child)


def split_big(folder):
    """Directories hold 77 entries; past that, titles go under alphabetical ranges."""
    if len(folder.children) <= adfs.DIR_ENTRIES:
        return
    titles = sorted(folder.children.values(), key=lambda e: e.name.lower())
    groups = -(-len(titles) // adfs.DIR_ENTRIES)
    per = -(-len(titles) // groups)
    folder.children = {}
    for i in range(0, len(titles), per):
        chunk = titles[i:i + per]
        first, last = chunk[0].name[0].upper(), chunk[-1].name[0].upper()
        sub = folder.subdir(first if first == last else "%s-%s" % (first, last))
        for t in chunk:
            sub.add(t)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--cache", default=CACHE)
    ap.add_argument("--index", default=INDEX)
    ap.add_argument("--limit", type=int, default=0, help="only the first N titles (for testing)")
    ap.add_argument("--refresh", action="store_true", help="fetch the index again")
    ap.add_argument("--map-unit", type=int, default=0, help="log2 bytes per map bit (default: smallest that fits)")
    args = ap.parse_args()

    rows = listing(args.cache, args.index, args.refresh)
    # Best dump per (category, title, disk)
    best = {}
    for url, path in rows:
        parts = path.split("/")
        category = re.sub(r"^Acorn Archimedes - ", "", parts[1]).split(" (")[0]
        folder = FOLDERS.get(category)
        if not folder:
            print("skipping unknown category %s" % category, file=sys.stderr)
            continue
        stem = os.path.splitext(parts[-1])[0]
        title, disk, rank = parse_name(stem)
        key = (folder, title, disk)
        if key not in best or rank < best[key][0]:
            best[key] = (rank, url, stem)

    root = adfs.Entry("$", attrs=0x08)
    readme = ["DreamRISC software archive", "", "Folder: archive name", ""]
    taken = {}
    titles = sorted({(folder, title) for folder, title, disk in best})
    if args.limit:
        titles = titles[:args.limit]
    done = missing = broken = 0
    for folder, title in titles:
        name = folder_name(title, taken.setdefault(folder, set()))
        dest = adfs.Entry(name, attrs=0x08)
        sources = []
        for disk in sorted(d for f, t, d in best if (f, t) == (folder, title)):
            rank, url, stem = best[(folder, title, disk)]
            zip_path = os.path.join(args.cache, "zips", os.path.basename(urllib.parse.unquote(url)))
            if not fetch(url, zip_path):
                print("  cannot download %s" % stem, file=sys.stderr)
                missing += 1
                continue
            try:
                z = zipfile.ZipFile(zip_path)
                members = [m for m in z.namelist() if m.lower().endswith((".adf", ".adl", ".img"))]
                # A zip can hold a plain dump and an [a] one; the plain one sorts first
                members.sort(key=lambda m: parse_name(os.path.splitext(m)[0])[2])
                problems = []
                tree = adfs.read_floppy(z.read(members[0]), problems)
            except (zipfile.BadZipFile, ValueError, IndexError) as e:
                print("  unreadable %s: %s" % (stem, e), file=sys.stderr)
                broken += 1
                continue
            for p in problems:
                print("  %s: %s" % (stem, p), file=sys.stderr)
            sanitise(tree)
            merge(dest, tree, "Disk%d" % disk if disk else None)
            sources.append(stem + (" (bad dump)" if rank == 2 else ""))
        if not dest.children:
            continue
        root.subdir(folder).add(dest)
        readme.append("%s.%s: %s" % (folder, name, "; ".join(sources)))
        done += 1
    for folder in root.children.values():
        split_big(folder)
    text = ("\n".join(readme) + "\n").encode("latin-1", "replace")
    root.add(adfs.Entry("ReadMe", 0xFFFFFFFF, 0, 0x13, text))

    if args.map_unit:
        image, log2_bpmb = adfs.build_disc(root, DISC_NAME, args.map_unit), args.map_unit
    else:
        image, log2_bpmb = adfs.build_smallest(root, DISC_NAME)
    with open(args.out, "wb") as f:
        f.write(image)
    with open(args.out + ".txt", "w") as f:
        f.write("\n".join(readme) + "\n")
    print("%s: %d titles, %d MB, %d byte map units; %d downloads missing, %d images unreadable"
          % (args.out, done, len(image) >> 20, 1 << log2_bpmb, missing, broken))


if __name__ == "__main__":
    main()
