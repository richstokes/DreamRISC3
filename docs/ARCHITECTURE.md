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

## Considerations

- Size: ~1.2MB of ARM becomes ~8.5MB of SH-4 plus ~1MB of tables, so ~7x. Out-of-line memory helpers and whole-ROM flag liveness are already in
- The old hardware registers are still the interface between ROM code and drivers. Replacing the kernel's drivers outright is a later step, not needed to reach the desktop
- Open question: Flycast timing is not hardware timing, and interpreter-free does not automatically mean fast. Needs a real console run

## Testing

Flycast's pointer can drift from the host mouse, so use scripted input on the harness.

```bash
tools/converge.sh tests/apps.replay 136
```

Replays hold `move`, `button`, `key` and `shot` lines. Slam to a corner first for a fixed origin. Pointer scale is ~1.5px per unit across and ~0.75 down in mode 12.

## Hardware Results (10/31)

The Good: 8 of 9 boots reached the desktop, checksum `C50F35BC`, same as the harness. The replay run matched all three checkpoints, so the Filer, BASIC and a C app behave identically on the SH-4. Boot is ~26-31s, at ~49 of 50 frames/s.

The Bad: boot 2 stuck at the 1bpp screen (`66733280`) for 90s+ with polls taking ~73us instead of ~15us. That looks like an interrupt or keyboard retry storm, but that's a guess: the run had no PC logging. 88 randomised clock runs on the harness and 6 more console boots did not reproduce it.

Not checked: actual speed under load (polls count branches and IO, so they are not a MIPS figure), physical input, and what the video output looks like on a screen.

Heartbeat builds use `HW_LOG=1`, and `HW_REPLAY=1` adds a generated `build/replay.h` from `tools/replay2c.py`. Screen checksums are the only way to verify a console run unattended. If a boot is stuck in 1bpp at 30s the heartbeat dumps IOC state and recent SWIs.

## Next Steps

1. Look at the real screen, try physical keyboard and mouse, and catch the boot hang if it recurs
2. Stream from disc to free RAM: page ROM data on demand through the page tables, and consider building `!Draw`, `!Paint` as overlays loaded on a dispatch miss
3. Storage: ADFS images from disc, then SD or VMU
4. Sound through AICA, and a true 640x480 desktop mode
