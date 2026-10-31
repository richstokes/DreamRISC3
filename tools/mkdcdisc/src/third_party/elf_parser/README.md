# elf_parser

Reads a SuperH ELF executable and flattens its loadable segments into the flat
binary image a Dreamcast expects at `0x8c010000` — the job `sh-elf-objcopy -O
binary` used to be invoked for.

Based on [finixbit/elf-parser](https://github.com/finixbit/elf-parser) (MIT,
© 2018), a dependency-free ELF inspection library, adapted for mkdcdisc by
Colton Pawielski in 2022–2023. See [LICENSE](LICENSE) for the notice both carry,
also reproduced at the top of each source file.

## Divergences from the original

This is a fork rather than a vendored copy: it is maintained here, and there is
no upstream release to resynchronise against. Upstream parses for inspection
(it is the ELF front-end of the author's `ltrace`/`ftrace`); here the same
parsing feeds a linker-like flattening step that upstream has no equivalent of.

- **32-bit SuperH instead of amd64.** Upstream targets amd64 only and works in
  `Elf64_*` structures; everything here is `Elf32_*`, and `Parser::Load()`
  rejects an ELF that is not `ELFCLASS32` or not `EM_SH`.
- **Reduced to what a disc build needs.** Upstream also decodes the symbol
  table, the relocation entries and the sections (its `readelf -s` / `readelf
  -r` / `readelf -S` equivalents); only the ELF header and the segments are
  carried over. Flattening reads segments, so the section headers, the section
  name string table and every section body were parsed and copied for nothing —
  roughly twice the ELF held in memory, including a zero-filled buffer the size
  of `.bss`, for a result no caller could reach.
- **`elf.h` is bundled** rather than taken from the system, which has no
  `<elf.h>` on Windows or macOS. It declares only the subset the parser reads,
  transcribed from the [TIS ELF specification][tis].
- **The output is Dreamcast-specific.** `Parser::fill_bin()` keeps `PT_LOAD`
  segments at or above `DC_RAM_START_ADDR` (`0x8c010000`) — dcload and libronin
  ELFs carry a `.stack` segment below it — rejects an ELF whose lowest segment
  starts anywhere else, and drops the zero-`p_filesz` `LOAD` segment some
  toolchains emit for `.bss` — dropped outright rather than merely skipped when
  copying, since its address sits past the last real byte and would otherwise
  define the end of the image. The image ends where the *file* data ends
  (`p_filesz`), not where the segment ends in RAM (`p_memsz`): `.bss` is zeroed
  by KOS at startup, so carrying it in `1ST_READ.BIN` would only pad the boot
  binary. This matches `objcopy -O binary`, which emits sections and has
  nothing to emit for an `SHT_NOBITS` one.
- **Every file-supplied offset is bounds-checked.** Upstream trusts the header
  fields it indexes with, which is defensible for a tool run on its own build
  output but not for mkdcdisc, which is handed ELFs from anywhere. `Load()`
  refuses a file shorter than an `Elf32_Ehdr`, a file without the `\x7fELF`
  signature, an `e_phentsize` smaller than an `Elf32_Phdr`, a program header
  table that does not fit, and any segment whose `p_offset + p_filesz` runs past
  the end; `Segment` clamps to what it was given as a second line of defence.
  The signature test is a real check and not the `assert()` it used to be, which
  compiled out under `NDEBUG` and left release builds dereferencing the header
  of a non-ELF file.
- **Diagnostics go through the project.** Failures are streamed with
  `term::error` and progress with `logging::debug()`/`logging::verbose()`, so a
  malformed ELF is reported like every other mkdcdisc error rather than in this
  module's own voice. This is the one place where code under `third_party/`
  reaches upwards into the project; it compiles because `src` is on the include
  path, and it is why these files cannot simply be replaced wholesale.

[tis]: https://www.cs.cmu.edu/afs/cs/academic/class/15213-f00/docs/elf.pdf
