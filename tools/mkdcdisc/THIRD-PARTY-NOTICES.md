# Third-party notices

mkdcdisc's own source code is under the MIT license, see [LICENSE](LICENSE).

A **built mkdcdisc binary is not only mkdcdisc**: it is a combined work that also
contains the components listed below, several of them under licenses that are not
MIT. Anyone redistributing a binary, or the sources as a whole, has to satisfy
those terms as well as MIT's. This file records what is in there and where each
component's own license text lives; it is a factual inventory, not legal advice.

Each vendored component keeps its own license. Nothing here has been relicensed,
with one deliberate exception, noted in its place.

## Compiled into the binary

### libisofs: GPL-2.0-or-later

The ISO 9660 filesystem writer, and the most consequential entry here: it is
**copyleft**, and it is present in every build.

- Upstream: <https://dev.lovelyhq.com/libburnia/libisofs.git>; the revision the
  bundled copy was taken from is in
  [git_info.json](src/third_party/libisofs/git_info.json)
- License text: [src/third_party/libisofs/COPYING](src/third_party/libisofs/COPYING),
  with authors and the notice in
  [COPYRIGHT](src/third_party/libisofs/COPYRIGHT)
- Copyright © 2007–2018 Vreixo Formoso, Mario Danic, Vladimir Serbinenko,
  Thomas Schmitt

How it is linked differs by platform, which matters when reasoning about
distribution:

| Build | How libisofs is used |
|---|---|
| MinGW-w64 (native Windows `.exe`) | the lightly patched copy in this repository, compiled in and **statically linked** |
| Linux, macOS, MSYS2 | the system libisofs, dynamically linked |

See [src/third_party/libisofs/README.md](src/third_party/libisofs/README.md) for
what the Windows patches are and why they exist.

### libedc: CDDL-1.0

The Reed-Solomon EDC/ECC encoder that computes the error detection and
correction fields of every raw sector written.

- Upstream: <https://codeberg.org/schilytools/schilytools> (schilytools, the
  successor to cdrtools); exact revision in
  [git_info.json](src/third_party/libedc/git_info.json)
- License text: [src/third_party/libedc/CDDL.Schily.txt](src/third_party/libedc/CDDL.Schily.txt)
  — Jörg Schilling's own copy, which closes with a notice under section 9 of the
  license placing it under German law and the courts of Berlin
- Copyright © 1998–2002, 2008 Heiko Eissfeldt; © 2002–2013 Jörg Schilling

The CDDL is a per-file, weak copyleft license. Seven files in that directory are
covered by it; the rest of the directory is mkdcdisc's own. Which is which, and
why, is set out in [src/third_party/libedc/COPYRIGHT](src/third_party/libedc/COPYRIGHT).

### elf_parser: MIT

Reads a SuperH ELF executable and flattens it into the flat binary a Dreamcast
loads.

- Upstream: <https://github.com/finixbit/elf-parser>
- License text: [src/third_party/elf_parser/LICENSE](src/third_party/elf_parser/LICENSE)
- Copyright © 2018 finixbit; © 2022–2023 Colton Pawielski

A fork rather than a vendored copy — see
[src/third_party/elf_parser/README.md](src/third_party/elf_parser/README.md).

### scramble

The Dreamcast bootstrap scrambler, rewritten in C++ from Marcus Comstedt's
`scramble.c`.

- Upstream: <https://mc.pp.se/dc/files/scramble.c>, also carried by KallistiOS
  under `utils/scramble/`
- **The original carries no copyright notice and no license text**, and none
  accompanies it on its author's site. mkdcdisc treats it as public domain. The
  full position is recorded in
  [src/third_party/scramble/COPYRIGHT](src/third_party/scramble/COPYRIGHT).

### makeip: BSD-2-Clause

Two pieces of the boot header: the checksum written into its Device Info field,
and the PNG-to-MR conversion behind `-i`/`--bootstrap-mr-image`, which turns an
image into the format the license screen displays.

- Upstream: **IP creator** (`makeip`) 2.0.1, as carried by KallistiOS under
  [`utils/makeip/`](https://github.com/KallistiOS/KallistiOS/tree/master/utils/makeip)
- License text: [src/third_party/makeip/LICENSE](src/third_party/makeip/LICENSE),
  reproduced in full below
- Copyright © 2000, 2001, 2002, 2019, 2020 The KOS Team and contributors

These are copies rather than reimplementations — the code was taken from makeip
and adapted to C++ — so the notice travels with them, and sits at the top of
each source file in that directory.
[COPYRIGHT](src/third_party/makeip/COPYRIGHT) records who wrote which part: the
CRC is Marcus Comstedt's, from the original makeip; the MR and PNG code is
Andrew Kieschnick's, from `logotools`. makeip is a collective work, so no
contributor is in a position to relicense it, and none has.

The BSD 2-Clause License is permissive and not copyleft, so it constrains
nothing about how mkdcdisc may be distributed. Its second clause is why this
entry ends the way the others do not: a **binary** distribution has to reproduce
the notice in its accompanying documentation, and the license is short enough
that reproducing it here — rather than only pointing at a file in the source
tree — lets this file discharge that on its own.

> Copyright (C) 2000, 2001, 2002, 2019, 2020 The KOS Team and contributors.
> All rights reserved.
>
> This code was contributed to KallistiOS (KOS) by Andress Antonio Barajas
> (BBHoodsta). It was originally made by Marcus Comstedt (zeldin). Some portions
> of code were made by Andrew Kieschnick (ADK/Napalm). Heavily updated by
> SiZiOUS. Bootstrap replacement (IP.TMPL) was made by Jacob Alberty (LiENUS).
>
> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this
>    list of conditions and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above copyright notice,
>    this list of conditions and the following disclaimer in the documentation
>    and/or other materials provided with the distribution.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
> DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
> FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
> DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
> SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
> CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
> OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
> OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### IP.BIN bootstrap templates

The Dreamcast bootstrap (`IP.BIN`), which the BIOS loads from the start of a disc
before it ever reaches the executable. mkdcdisc ships three templates under
[assets/ip/](assets/ip/), embeds them in the binary as C arrays
(`src/assets/*.include`, regenerated by `tools/regen-includes.sh`), and writes one
onto **every image it produces**.

Each template is 32768 bytes, laid out as follows (see
[dreamcast.wiki/IP.BIN](https://dreamcast.wiki/IP.BIN) for the full description):

| Offset | Size | Section |
|---|---|---|
| `0x0000` | 256 | Metadata: title, product number, release date, boot filename, peripherals. mkdcdisc rewrites all of it from its own options. |
| `0x0100` | 512 | Table of contents |
| `0x0300` | 13312 | **SEGA license screen code** (see below) |
| `0x3700` | 256 | Area protection symbols |
| `0x3800` | 18432 | Bootstrap |

**The license screen section cannot be replaced.** The BIOS checks it against a
copy held in its own ROM, so a disc carrying anything else does not boot on
retail hardware: **there is no alternative that the console accepts**. It is Sega's
code, no license accompanies it, and none is claimed here. It is byte-identical
in all 3 templates (as it has to be) and it is why every Dreamcast toolchain
has carried these same 13312 bytes for as long as they have existed: the free
replacement efforts substitute their own code for everything *around* this
section and keep it untouched, because it cannot be anything else.

The bootstrap in the MIL-CD template is one of those free replacements, by Jacob
Alberty (LiENUS), and the reason it exists is worth recording: early Dreamcast
releases simply reused the `IP.BIN` lifted from a commercial game, which made them
straightforwardly infringing. Alberty rewrote it from scratch so they need not
be, leaving only the one section that could not be rewritten. It has been the
community's de facto bootstrap ever since, which is why the same code is found
in every toolchain that writes an `IP.BIN`, makeip included.

The reasoning usually applied to that remaining section is that reproducing it is
necessary for interoperability and that no substitute exists: an argument drawn
from *Sega v. Accolade* (US Court of Appeals, 9th Circuit, 1992), which turned on
a comparably small initialisation segment that had to be reproduced verbatim for
software to run on Sega hardware at all. That is the reading the Dreamcast
community has long worked from, recorded here as such; this project makes no
determination of its own, and none of the above is legal advice.

The metadata, by contrast, is entirely mkdcdisc's: it is written from the `-n`,
`-s`, `-r`, `-H` and `--boot-filename` options among others, and `-p/--bootstrap`
replaces the whole template with a file of your own.

## Not in the binary

### MDF/MDS format documentation

[docs/specs/mdf_mds.txt](docs/specs/mdf_mds.txt) is Henrik Stokseth's
description of the Alcohol 120% image format, redistributed here because the MDS
writer was written from it. Its own header reads
`License : Copyright (c) 2005. All rights reserved. ALL USE AT OWN RISK!`, so it
carries no grant to redistribute it. It is documentation and reaches no binary.
