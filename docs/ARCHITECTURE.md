# Architecture

## Problem Statement

Dream-TOS could compile EmuTOS because it is C. RISC OS 3.11 only exists as 2MB of 26-bit ARM code, and the goal is a native port with no emulator or JIT.

## Proposal

1. `tools/romscan.py` maps the ROM (kernel plus 88 modules) and finds code
2. `tools/armxlat.py` turns every reachable ARM instruction into C. Blocks are functions joined by `musttail` calls, so ARM calls never use the host stack
3. `src/mach.c` is the machine layer: PSR and register banks, page tables, and native drivers behind the old MEMC, IOC, VIDC, keyboard and CMOS register addresses
4. `src/dc/main.c` is KOS video and Maple input. `src/host/main.c` is a Mac harness that runs the same generated C with deterministic time

## How code gets found

Boot traces come from a dev-only hook in Arculator (`ARC_PCMAP`). Static discovery adds module entry points, command tables, ADR targets, jump tables, stored ROM addresses and `lr`-saving prologues. Network and printer modules are skipped unless a trace proves they run.

A jump to code that was not recompiled stops the harness with its address. `tools/converge.sh` turns those stops into `trace/seeds.txt` and `trace/ramwords.txt`. On the console it raises a prefetch abort, so RISC OS reports an error and only the task dies.

## Code that runs from RAM

RISC OS copies and generates a little code at runtime (the IRQ head, SWI stubs, sound voices, BASIC's `SYS`, SharedCLibrary branch tables).

Each known instruction word gets one precompiled routine that takes its PC as an argument, looked up by word. SWI words, branch words and the `LDMIA r12!,{list}` that C apps build on the stack for SWI veneers share a routine each, since those read the word for their number, offset or register list.

**This is the part closest to an emulator, so to be plain about it:** RAM code runs one routine per instruction through a table lookup, and those three routines do read the instruction word. Nothing is generated at runtime, and unknown words do not run. New ARM binaries cannot run; apps have to be part of the build, like Dream-TOS.

## Disc streaming

The disc holds `rom.bin` and `ov_N.bin`. Under dcload the console reads the same files over `/pc`.

- ROM data: hot pages load into RAM at boot; 12 cold pages stream through an 8-slot cache, using the page tables. Fewer are cold than you'd hope. The kernel walks module headers on every service call and ResourceFS scans Messages and ROMFonts. Those cold pages also go hot once `!Draw` runs (28k disc reads with 3 slots, 21 with 8), so only ~130KB leaves RAM
- Code: blocks outside the boot trace in `!Draw`, `!Paint` and a misc group (printing, network, DOSFS) become overlay units linked to one fixed region. A dispatch miss loads the unit. Blocks keep no native stack, so swapping at any dispatch is safe. Boot never loads code from disc
- SharedCLibrary, BASIC and FileCore stay resident on purpose, as every app uses them and paging them would mean a seek per task switch

Console check (10/31): the `tests/draw.replay` drawing session matched the harness at both checkpoints, with its two key events removed on both sides because hardware replays do not support keys yet.


Net saving is ~0.9MB. Running `!Draw` and `!Paint` together will reload a unit each time focus moves between them, ~1s each: untested and it will be ugly.

Machine state lives in r14 on SH-4 (`-ffixed-r14` is required, or GCC borrows the register) which saves ~0.5MB and gained ~14% idle throughput on the console.

## Bundled apps

Dropped ARM binaries still cannot run, so a bundled app is recompiled like the ROM. `apps/` has one folder per app and [apps/README.md](../apps/README.md) covers the manifest. There are two parts, and neither loads at boot:

- Files: `tools/mkapps.py` appends a ResourceFS pack to `rom.bin`, mapped at `0x3C00000` through the cold page cache. A small `DreamApps` module, added to the ROM's module chain, registers it, so the app appears under `Apps` like any other. Every ResourceFS lookup walks every file header, so the pack keeps headers in its first page and files only cost a disc read when opened. Boot reads that one page
- Code: each app is an overlay unit. It runs at its load address, which other tasks share, so a unit only starts from its entry point with the right instruction there, and entries from outside are checked against the instruction they were compiled from

Decision: extend the Apps folder, not a new filing system. A virtual drive would need a FileSwitch filing system plus a Filer icon, and it would get us nothing until a game needs more than 2MB of files, the size of the pack window. Down the road a real drive is the way to go for that, and for the game's own save files.

App units are small enough to spend code on speed. Each is one file at `-O2`, memory access and `LDM`/`STM` are inline, and returns go through a direct map. On the console that took Lander's heavy scene from 25 to ~48 frames/s, worst case 17 to 40, for 47KB to 93KB of unit, which only exists in memory while the app runs.

Lander's physics run per frame, with only the vsync wait to limit them, so that speed made it far too fast. `code.frame_us` sets the app's vertical sync period, without touching OS timers. Lander uses 25Hz. An 8MHz ARM2 would manage ~15 frames/s going by ~150-290K ARM instructions a frame at ~4 MIPS, which is an estimate and not a measurement on real Acorn hardware, so it's probably still a bit quicker than the original. Open question: does it feel right on a pad?

## Storage

`src/fdc.c` is a native driver behind the WD1772 registers, so RISC OS's own ADFS and FileCore run unchanged, including their FIQ byte transfers. The disc (`assets/adfs.adf.gz`) is an 800KB ADFS E image that RISC OS formatted itself on the harness via `tests/format.replay`. It loads into RAM at boot, so writes work but vanish at power off.

Things that bit us, for whoever touches this next:

- IOC control bit 2 set means a disc is present
- Sector IDs have to arrive at about 40ms each. ADFS collects a track's IDs into a 512-byte stack buffer until a timer says one revolution passed, so answering instantly overruns the buffer and smashes the stack
- ADFS copies a different FIQ routine to the vector page per operation, and runs one from the SVC stack. Those words are in `trace/ramwords.txt`

Harness: `DREAM_ADF` loads an image (a missing file means blank), `DREAM_ADF_OUT` saves it on exit, and the replay verb `type` drives the keyboard.

Console check (10/31): opened `:0` in the Filer with the checksum matching the harness. Reading from a physical disc or GDEMU is untested, and writing has only run on the harness.

## Considerations

- Size: ~1.2MB of ARM becomes ~8.5MB of SH-4 plus ~1MB of tables, so ~7x. Out-of-line memory helpers and whole-ROM flag liveness are already in, and the latter only saved ~1%
- The old hardware registers are still the interface between ROM code and drivers. Replacing the kernel's drivers outright is a later step, not needed to reach the desktop
- Open question: Flycast timing is not hardware timing, and interpreter-free does not automatically mean fast. Needs a real console run

## Testing

Flycast's pointer can drift from the host mouse, so use scripted input on the harness.

```bash
tools/converge.sh tests/apps.replay 136
```

Replays hold `move`, `button`, `key` and `shot` lines. Slam to a corner first for a fixed origin. Pointer scale is ~1.5px per unit across and ~0.75 down in mode 12.

The harness and the screen checksums read guest RAM, so neither one sees bugs in the Dreamcast display path in `src/dc/main.c`. Check that in Flycast by eye.

## Hardware Results (10/31)

The Good: 8 of 9 boots reached the desktop, checksum `C50F35BC`, same as the harness. The replay run matched all three checkpoints, so the Filer, BASIC and a C app behave identically on the SH-4. Boot is ~26-31s, at ~49 of 50 frames/s.

The Bad: boot 2 stuck at the 1bpp screen (`66733280`) for 90s+ with polls taking ~73us instead of ~15us. That looks like an interrupt or keyboard retry storm, but that's a guess: the run had no PC logging. 88 randomised clock runs on the harness and 6 more console boots did not reproduce it.

Not checked: actual speed under load (polls count branches and IO, so they are not a MIPS figure), physical input, and what the video output looks like on a screen.

Heartbeat builds use `HW_LOG=1`, and `HW_REPLAY=1` adds a generated `build/replay.h` from `tools/replay2c.py`. Screen checksums are the only way to verify a console run unattended. If a boot is stuck in 1bpp at 30s the heartbeat dumps IOC state and recent SWIs.

Lander on the console (10/31): 124-125 game frames per 5s for a whole scripted flight, ~60% of the CPU left over in the heaviest scene, launch with no visible stall over dcload. Before pacing, the profile put the heavy scene at ~22% vertex projection (software division), ~23% triangle setup and ~12% span fills.

The video output was checked from a framebuffer dump in Flycast (`HW_DUMP=1` and `tools/fbdump.py`): full width, correct colours, matching the harness. It is still not a real TV. The dump also found a line that checksums cannot see, as they only cover guest memory: a line hash built on rotates cancelled on solid 256 pixel fills and left stale pixels.

Console and harness checkpoints stop matching in Lander once it moves, because replays run on wall time and the frame the game has reached differs. Static scenes matched.

## Next Steps

1. Look at the real screen, try physical keyboard and mouse, and catch the boot hang if it recurs
2. Persist saves: write the disc image to SD, or at least a small area to VMU
3. Sound through AICA, and a true 640x480 desktop mode
