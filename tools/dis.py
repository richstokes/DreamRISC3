#!/usr/bin/env python3
"""Disassemble a ROM range: dis.py <hex addr> [count]. Uses Xcode's llvm-objdump."""
import subprocess, sys, tempfile, os
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from romscan import ROM_BASE, load_rom

rom = load_rom(os.environ.get("ROM_DIR", str(Path(__file__).parent.parent.parent / "arculator-mac/roms/riscos311")))
addr = int(sys.argv[1], 16)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 32
offset = addr - ROM_BASE if addr >= ROM_BASE else addr
with tempfile.TemporaryDirectory() as tmp:
    s = Path(tmp) / "a.s"
    data = rom[offset:offset + count * 4]
    s.write_text(".text\n" + "".join(f".inst 0x{int.from_bytes(data[i:i+4],'little'):08x}\n" for i in range(0, len(data), 4)))
    obj = Path(tmp) / "a.o"
    subprocess.check_call(["xcrun", "clang", "-target", "armv4-none-eabi", "-c", str(s), "-o", str(obj)])
    text = subprocess.check_output(["xcrun", "llvm-objdump", "-d", "--triple=armv4-none-eabi", str(obj)], text=True)
for line in text.splitlines():
    parts = line.split(":", 1)
    if len(parts) == 2 and parts[0].strip().isalnum() and "\t" in parts[1] or (len(parts)==2 and parts[0].strip() and all(c in "0123456789abcdef" for c in parts[0].strip())):
        print(f"{int(parts[0].strip(),16)+addr:07x}:{parts[1]}")
