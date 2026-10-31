# DreamRISC3

RISC OS 3.11 running natively on the Sega Dreamcast, built on KallistiOS.

**This is not an emulator and there is no JIT.** The Acorn ROM is ARM code with no public source, so a tool recompiles it ahead of time into C and then SH-4. The ELF holds the native code, and the original hardware pokes land in native drivers.

![RISC OS desktop from the recompiled ROM on the Mac harness](docs/desktop-apps.png)

## Status

- Boots to the full desktop in Flycast. **Not yet tested on a real Dreamcast**
- On the Mac harness: Filer windows, menus, BASIC apps (`!Calc`, `!Chars`) and a C app (`!Edit`) all open
- No sound output, no storage beyond ROM and RamFS, and screen modes are stretched to 640x480
- Memory is tight: ~11.8MB ELF plus 4MB guest RAM out of 16MB

## Build

You need your own RISC OS 3.11 ROM. It is never committed. Default location is `../arculator-mac/roms/riscos311`; override with `ROM_DIR`.

```bash
scripts/build-dc.sh
```

```bash
scripts/run-flycast.sh
```

`scripts/stop-flycast.sh` stops only the instance this checkout launched.

Controls: mouse left/right/middle are Select/Menu/Adjust. On a controller, stick or D-pad moves the pointer and A/B/X are Select/Menu/Adjust.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it works and how to test it.
