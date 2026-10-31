#!/usr/bin/env python3
"""Build rom.bin: the ROM, a module that registers bundled apps, and a file pack.

The pack follows the 2MB ROM in rom.bin and is mapped at PACK_BASE by mach.c, one
cold page at a time. ResourceFS only ever sees ordinary memory.
"""
import hashlib
import json
import struct
import sys
import urllib.request
from pathlib import Path

import basic
from romscan import find_chain, load_rom, module_info, word
from scorpress import unpack

PACK_BASE = 0x3C00000
PAGE = 0x8000
MODULE_AT = 0x1FFD00  # padding after the credits text, before the checksum words
ROM_END = 0x1FFFE0
ROOT = Path(__file__).resolve().parent.parent


def fetch(app, cache):
    """Download the app's pinned upstream files once."""
    paths = {}
    upstream = app.get("upstream", {})
    for key, entry in upstream.get("files", {}).items():
        path = cache / app["name"] / key
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            url = upstream["base"] + urllib.request.quote(entry["path"])
            print(f"fetching {url}")
            with urllib.request.urlopen(url) as reply:
                path.write_bytes(reply.read())
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            sys.exit(f"{path}: sha256 {digest} is not the pinned {entry['sha256']}")
        paths[key] = path
    return paths


def load_apps(cache):
    apps = []
    for manifest in sorted((ROOT / "apps").glob("*/app.json")):
        app = json.loads(manifest.read_text())
        paths = fetch(app, cache)
        for entry in app["files"]:
            if "basic" in entry:
                source = manifest.parent / entry["basic"]
                entry["data"] = basic.tokenise(source.read_text(encoding="latin-1"))
                continue
            source = paths[entry["upstream"]] if "upstream" in entry else manifest.parent / entry["local"]
            entry["data"] = source.read_bytes()
            if entry.get("unpack") == "scorpress":
                entry["data"] = unpack(entry["data"])
        apps.append(app)
    return apps


def file_record(app, entry):
    """One ResourceFS file, minus the leading link to the next one."""
    if "type" in entry:
        load, exec_addr = 0xFFF00043 | int(entry["type"], 16) << 8, 0
    else:
        load, exec_addr = int(entry["load"], 16), int(entry["exec"], 16)
    data = entry["data"]
    name = f"Apps.{app['name']}.{entry['name']}".encode() + b"\0"
    name += bytes(-len(name) & 3)
    head = struct.pack("<IIII", load, exec_addr, len(data), 3) + name
    return head + struct.pack("<I", len(data) + 4) + data + bytes(-len(data) & 3)


def build_pack(apps):
    """Files ascending by size, so headers share as few pages as possible.

    Every ResourceFS lookup walks every header. The last file links back to a
    zero word ahead of it rather than on past its data, so that walk does not
    pull in the page where the largest file ends.
    """
    records = sorted((file_record(app, e) for app in apps for e in app["files"]), key=len)
    pack = bytearray()
    for index, record in enumerate(records):
        link = 4 + len(record)
        if index == len(records) - 2:
            link += 4
        elif index == len(records) - 1:
            pack += bytes(4)
            link = -4 & 0xFFFFFFFF
        pack += struct.pack("<I", link) + record
    return bytes(pack)


def arm_module():
    """Registers the pack; also answers ResourceFS restarting."""
    code = [
        0,                # start
        0x1C,             # init
        0,                # final
        0x30,             # service
        0x50,             # title
        0x5C,             # help
        0,                # commands
        # init
        0xE92D4000,       # STMFD sp!,{lr}
        0xE3A0050F,       # MOV r0,#PACK_BASE
        0xEF061B40,       # SWI XResourceFS_RegisterFiles
        0xE1500000,       # CMP r0,r0 (a failure must not fail the boot)
        0xE8BD8000,       # LDMFD sp!,{pc}
        # service
        0xE3510060,       # CMP r1,#&60 (Service_ResourceFSStarting)
        0x11A0F00E,       # MOVNE pc,lr
        0xE92D4001,       # STMFD sp!,{r0,lr}
        0xE3A0050F,       # MOV r0,#PACK_BASE
        0xE1A0E00F,       # MOV lr,pc
        0xE1A0F002,       # MOV pc,r2
        0xE8BD8001,       # LDMFD sp!,{r0,pc}
    ]
    assert len(code) * 4 == 0x4C
    body = struct.pack(f"<{len(code)}I", *code) + bytes(4)
    body += b"DreamApps\0\0\0"
    assert len(body) == 0x5C
    body += b"DreamApps\t0.01 (31 Oct 2026)\0"
    return body + bytes(-len(body) & 3)


def patch_rom(rom):
    """Hang the module off the end of the chain, which is full of credits text."""
    rom = bytearray(rom)
    chain = find_chain(bytes(rom))
    last = chain[-1]
    if word(rom, last + word(rom, last)) != 0 or rom[MODULE_AT:ROM_END] != b"\xff" * (ROM_END - MODULE_AT):
        sys.exit("this ROM's layout is not the expected 3.11 image")
    module = arm_module()
    struct.pack_into("<I", rom, last, MODULE_AT - last)
    rom[MODULE_AT:MODULE_AT + 4 + len(module) + 4] = struct.pack("<I", len(module) + 4) + module + bytes(4)
    return bytes(rom)


def main():
    rom_dir, out = sys.argv[1], Path(sys.argv[2])
    apps = load_apps(out / "upstream")
    pack = build_pack(apps)
    patched = patch_rom(load_rom(rom_dir))
    out.mkdir(exist_ok=True)
    (out / "rom.bin").write_bytes(patched + pack + bytes(-len(pack) & (PAGE - 1)))
    print(f"{len(apps)} bundled apps, pack {len(pack)} bytes")


if __name__ == "__main__":
    main()
