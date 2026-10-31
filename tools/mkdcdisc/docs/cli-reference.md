# CLI Reference

The full option list `mkdcdisc --help` prints, the details its one-line
descriptions leave out, and the table of previous long option names still
accepted for backward compatibility. See the main [README](../README.md) for a
quick-start and links to the other docs.

```
mkdcdisc v0.0.4 - Sega Dreamcast Disc Image Generator
Copyright (C) 2022-2026 Simulant and contributors

A modern command-line interface (CLI) tool for generating disc images for the Sega Dreamcast.

This tool creates a GD-ROM (.gdi) or a self-booting MIL-CD image in Padus DiscJuggler (.cdi),
Alcohol 120% (.mds/.mdf), or Nero Burning ROM (.nrg) format. These images can be used directly on an
emulator or via a virtual drive, or on a physical console via a burned CD-R or an optical drive
emulator (ODE).

Usage: mkdcdisc [OPTION]... [-e ELF | -b|-B BIN | -t ISO] -o OUTPUT

General:
These options decide what the tool prints about its own run, and where it does its work. None of
them affect the generated disc image itself.

  -h, --help                          print this help screen
  -v, --verbosity                     log level: '0' for silent, '1' for one line per build step,
                                      '2' adds each file and track handled, '3' adds the build plan
                                      and the internals; defaults to '1'
  -q, --quiet                         print nothing at all; same as '-v 0'
      --no-banner                     suppress the startup banner
      --color                         colorize the output: 'auto' (only on a terminal), 'always' or
                                      'never'; defaults to 'auto'
      --disable-color                 shorthand for '--color never'
      --temp-dir                      directory to stage the disc contents in; defaults to the
                                      system temporary directory

Output Control:
These options control the output the tool generates. By default, a MIL-CD Audio/Data disc image in
Padus DiscJuggler (CDI) format is produced.

  -o, --output                        output filename (required)
  -F, --format                        output format: 'cdi', 'mds', 'nrg' or 'gdi'; inferred from the
                                      output file extension when omitted; defaults to 'cdi'
      --allow-overwrite               write over the output files when they already exist, instead
                                      of refusing
  -I, --dump-iso                      also write the data track to a separate .iso (not with -F gdi)
      --gdi-truncate                  -F gdi only: don't pad the high-density track to full GD-ROM
                                      size; smaller, but non-standard: only boots from an ODE or
                                      emulator that accepts it
      --gdi-sector-size               -F gdi only: data track sector size, '2352' (track??.bin) or
                                      '2048' (track??.iso); defaults to 2352

Main Executable (1ST_READ.BIN):
The program the console runs at boot, given in exactly one of these 3 forms, or in none of them when
-t/--external-data-track supplies an ISO that already carries it.

  -e, --main-elf                      executable file in ELF format
  -b, --main-binary-unscrambled       executable file in unscrambled binary format
  -B, --main-binary-scrambled         executable file in scrambled binary format
      --boot-filename                 name of the main executable on disc; defaults to
                                      '1ST_READ.BIN'

Disc Metadata Bootstrap (IP.BIN):
The Initial Program (IP) bootstrap every Dreamcast disc needs: the code that loads the executable,
the fields the console reads back, and the customized license screen, if any. This tool carries its
own bootstrap, so these options exist to fill its fields in; each one left out keeps what that
default bootstrap already has. Its license screen reads "... is what official games display, but
this is 100% unofficial and unlicensed."; you may replace that MR image with your own, or leave it
out entirely.

  -p, --bootstrap                     bootstrap file (IP.BIN) to use instead of the built-in one
  -a, --author                        author/company that made the disc/software
  -n, --title                         title of the software
  -s, --serial                        disc serial; defaults to 'IND-' plus a boot binary hash
  -P, --product-version               product version, in Vx.yyy format; defaults to V1.000
  -r, --release                       release date in YYYYMMDD format; defaults to today
  -H, --peripherals                   peripheral support: 5 hex digits plus VGA and Windows CE
                                      flags, e.g. 'E000F10'; defaults to the bootstrap's value
  -i, --bootstrap-mr-image            MR or PNG image for the license screen
  -m, --disable-bootstrap-mr          embed no license screen image at all; takes precedence over -i
      --disable-bootstrap-checks      build even when the disc fails the checks that say it can
                                      boot; the failures are reported as warnings instead
      --gdi-fpu-rounding              -F gdi only: FPU rounding mode the bootstrap leaves set,
                                      'nearest' (the default) or 'zero'

Data Track Contents:
What the ISO9660 filesystem holds besides the main executable, which is added for you. With -F gdi
these fill the high-density area, the low-density one having its own --gdi-lda-* options. They are
all disabled when -t/--external-data-track is used, as that ISO is already built.

  -d, --directory                     add a directory tree, root included; repeat for multiple
  -D, --directory-contents            add a directory tree, root excluded; repeat for multiple
  -f, --file                          add a single file; repeat for multiple files
  -S, --sort-file                     path to sort file
  -N, --disable-data-track-padding    disable data track padding (padding optimizes loading speed)
                                      (not with -F gdi)
      --duplicates-once               write byte-identical files once for optimizing storage
  -V, --volume-label                  ISO9660 volume label; sanitized. Defaults to -n/--title
      --copyright                     file to use as COPYRIGH.TXT; with -F gdi it defaults to a line
                                      built from -n/-a/-r
      --abstract                      likewise for ABSTRACT.TXT
      --biblio                        likewise for BIBLIOGR.TXT
      --gdi-disable-copyright         -F gdi only: leave COPYRIGH.TXT off the disc entirely; takes
                                      precedence over --copyright
      --gdi-disable-abstract          -F gdi only: likewise for ABSTRACT.TXT
      --gdi-disable-biblio            -F gdi only: likewise for BIBLIOGR.TXT
      --gdi-lda-directory             -F gdi only: as -d, but into the low-density area
      --gdi-lda-directory-contents    -F gdi only: as -D, but into the low-density area
      --gdi-lda-file                  -F gdi only: as -f, but into the low-density area
      --gdi-lda-sort-file             -F gdi only: as -S, but for the low-density area

Disc Layout:
In MIL-CD mode, an Audio/Data disc is generated by default, the only layout that supports CD-DA
audio tracks (see -c/--cdda-track). -F gdi has no layout choice: a GD-ROM always has the same
structure.

  -L, --disc-layout                   session arrangement: 'audio-data' (the default), 'data-data',
                                      or 'merged-data', whose second session is a full merged ISO
                                      and which requires two -t ISOs
  -M, --print-msinfo                  print the data-track start LBA to feed mkisofs as -C 0,MSINFO,
                                      then exit

Disc Tracks:
The tracks around the data one: CD-DA audio, either in the MIL-CD audio session or inside a GD-ROM's
high-density zone. -t/--external-data-track is the exception: it replaces the data track itself with
an ISO built elsewhere.

  -c, --cdda-track                    audio track (.wav or raw CD-DA); repeat for multiple tracks
      --gdi-lda-cdda-track            -F gdi only: audio for the low-density CDDA track (track 2);
                                      defaults to silence
  -t, --external-data-track           use a pre-built ISO as the data track; disables the data track
                                      and IP.BIN options. In 'merged-data', give it twice, once per
                                      session (not with -F gdi)

Examples:
  mkdcdisc -e game.elf -o game.cdi
  mkdcdisc -e game.elf -d data/ -n "My Game" -o game.cdi
  mkdcdisc -t data.iso -c track01.wav -o game.mds
  mkdcdisc -e game.elf -d data/ -n "My Game" -o game.gdi

```

## Option details

`--help` keeps every description to one or two lines. The behaviour those
descriptions compress is spelled out below; options not listed here do exactly
what their one-liner says.

Two markers in that list say which disc type an option belongs to, since most
apply to both and only the exceptions are worth naming: `-F gdi only:` leads
the description of an option a GD-ROM alone accepts (all of them named with the
`--gdi-` prefix, so `mkdcdisc --help | grep gdi` lists the mode in one go), and
a trailing `(not with -F gdi)` marks one a GD-ROM refuses. Both are written
from the same field of the option table that mkdcdisc validates against, so an
option cannot be described one way and enforced another. A group that is
MIL-CD only from end to end says so once in its own description instead —
see "Disc Layout".

### General

| Option | Details |
| --- | --- |
| `-v/--verbosity` | See the level table below. |
| `-q/--quiet` | Exactly `-v 0`. |
| `--color` | `auto`, the default, colorizes only what actually goes to a terminal: with stdout or stderr redirected into a file or a pipe — a build log, `mkdcdisc -M` feeding a script, a CI job — that stream keeps exactly the plain text it had before there were colors. The two are decided separately, so redirecting one doesn't decolorize the other. `always` writes the escape sequences whatever they go to, and `never` writes none at all. |
| `--disable-color` | Exactly `--color never`. If both are given, `--color` wins. |
| `--temp-dir` | Where the disc contents are staged while the image is built. mkdcdisc copies everything the disc will hold into a randomly-named directory here, plus the data track padding — up to a 74-minute CD of it — so the space needed is roughly the size of the finished image. The default is the system temporary directory (`TMPDIR` and friends, `%TEMP%` on Windows), which on most current Linux distributions is a RAM-backed `tmpfs`: a large disc root then fills memory rather than a disk. Point this at ordinary storage when building anything big. The directory must already exist; mkdcdisc creates its own subdirectory inside it and removes only that, whatever the build does. |

#### Verbosity levels

`-v/--verbosity` takes a level from 0 to 3, each adding to the one below it:

| Level | What it prints |
| --- | --- |
| `-v 0` (`-q`) | Nothing. Not less output — none: no banner, no steps, no warnings on stdout. Errors still go to stderr, and `-M/--print-msinfo` still prints its number, because that is the output that was asked for rather than a log of the run. |
| `-v 1` (default) | One line per step of the build, in the order they happen — loading the executable, scrambling it, gathering the disc contents, building the IP.BIN, padding, generating the data track, writing the image. Each ends in `Done.`, and the long ones count themselves up to it. This says what mkdcdisc is doing, never how. |
| `-v 2` | Adds one line per thing handled inside a step: each input file loaded or copied, each audio track added, each option that changed what a step did. These are indented under the step they belong to. |
| `-v 3` | Opens with the build plan below, then adds the internals: byte sizes, ELF load addresses, the staging directory, and the before/after sort-weight dumps of `-S/--sort-file` and `--gdi-lda-sort-file`. This is the level to attach to a bug report. |

#### The build plan

`-v 3` starts the run with a block naming everything it has settled on, before
any of it is acted on:

```
Build plan:
Disc type               : MIL-CD
Image format            : Padus DiscJuggler (CDI)
Image layout            : Audio/Data
Output                  : game.cdi
Data track              : built by mkdcdisc from the files below
Volume label            : MY_GAME
Boot filename           : 1ST_READ.BIN
Main executable         : game.elf (ELF)
Executable scrambling   : scrambled for MIL-CD boot
Bootstrap (IP.BIN)      : the built-in template
License screen          : the built-in default MR image
Software title          : My Game
Author                  : Unknown Author
Serial                  : generated from the boot binary
Product version         : V1.000
Release date            : 20260807
Peripherals             : E799F10
Disc contents           : 2 directories, 1 file
Sort file               : none
Volume text files       : none
Audio session           : one blank 4-second track
Data track padding      : enabled, the track is padded out to the full CD size
```

The rest of the log says what mkdcdisc is doing as it does it, which answers
how far a run got but not what it was asked for — and that is spread across
forty options, their defaults, and the several places one option overrides
another. Every value here is the one the build is about to use, read from the
same place the builder reads it from: an unset `-P/--product-version` and
`-H/--peripherals` show what the bundled IP.BIN template actually carries, and
a `-p/--bootstrap` file's own metadata replaces the whole block, since the
options that would have filled it are ignored.

A GD-ROM run adds one field, `FPU rounding`, naming the bootstrap variant
`--gdi-fpu-rounding` selected.

The one value that cannot be given is an unset `-s/--serial`: its default is a
hash of the boot binary, which has not been read yet.

Which fields appear depends on the run. `-F gdi` adds the low-density area's
contents and its own track settings; `-t/--external-data-track` drops the
filesystem and IP.BIN half altogether and names the ISO it was handed instead,
because none of those options apply there.

A step that has nothing to count is still announced, so the default level is a
complete checklist of the run rather than only its slow parts. When stdout is
not a terminal — redirected into a build log, piped, or captured by a test —
the in-place percentages are dropped and each step prints one plain line.

The colors are ANSI escape sequences. On Windows they need a console that
interprets them, which is Windows 10 and up — mkdcdisc switches the console
into that mode itself, and an older Windows simply stays uncolored. The
MSYS2/mintty terminal DreamSDK runs in is recognized as a terminal too, even
though Windows does not consider it a console.

In `auto` mode two environment variables are honoured, in this order:
`NO_COLOR` set to anything non-empty turns colors off (see
[no-color.org](https://no-color.org/)), and `CLICOLOR_FORCE` set to anything
but `0` turns them on even when the output is redirected. `TERM=dumb` turns
them off. A `--color` on the command line overrides all three.

#### Reproducible images: `SOURCE_DATE_EPOCH`

Two runs over the same files normally produce different images, because a disc
records when it was made: the ISO9660 volume timestamps, a timestamp per file,
and the IP.BIN release date when `-r/--release` does not give one.

Setting `SOURCE_DATE_EPOCH` to a whole number of seconds since the Unix epoch
replaces every one of those with the instant it names, so the image depends on
its inputs and nothing else. It is the
[cross-ecosystem convention](https://reproducible-builds.org/docs/source-date-epoch/)
for this, read from the environment rather than given as an option so that a
build system can set it once for every tool it drives.

```sh
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) mkdcdisc -e game.elf -o game.cdi
```

Unset, mkdcdisc stamps the current time, as it always has. Set to something
that is not a number of seconds, it says so and carries on as if unset — an
environment variable nobody asked for should not stop a disc from being
written.

Two things to know before relying on it:

- **The per-file timestamps are written in local time**, with the offset
  recorded beside them, so the same instant is different bytes in two
  timezones. Fix `TZ` as well (`TZ=UTC`) for images that must match byte for
  byte across machines. The volume timestamps need no such help — they are
  recorded as GMT.
- **The original modification times of the files being added are discarded**,
  which is what makes the result independent of how the sources were obtained;
  a `git clone` stamps every file with the time of the clone.

One known gap: two builds still differ by 8 bytes per file. Those bytes are the
file serial numbers in the Rock Ridge `PX` entries. libisofs hands them out from
a counter of its own, in the order a sort by each source file's `(filesystem,
device, inode)` puts the files in — and mkdcdisc stages the disc contents into a
fresh temporary directory, so the operating system picks those inode numbers
anew on every run. The same serial numbers are therefore always present; which
file carries which is not fixed, wherever inode numbers are not handed out in
creation order. Windows/MSYS2 is one such platform, and a volume with
subdirectories reshuffles on any of them.

They are metadata no Dreamcast reads, and the filesystem layout and file
contents are identical either way.

It applies to every volume on the disc, not only the one holding the game. A
GD-ROM's low-density area is a filesystem of its own, so a `--gdi-lda-*` tree
is subject to it just as `track03` is.

A few bytes per node is also less than it looks on a `--gdi-sector-size 2352`
track, where the serial numbers are not the only thing that moves. Measured on
one such disc, two runs of the same binary over the same inputs produced
`track01` files differing in 128 bytes out of 4.6 MB: ten of them the `PX`
serial numbers themselves, and the other 118 the error-correction bytes of the
four raw sectors carrying them, recomputed because their contents changed.

Do not read a matching digest as proof either. Fourteen runs of one binary over
one set of inputs produced two distinct `track01` files, eleven of the first
and three of the second, so two builds agreeing byte for byte can simply be a
repeated draw. To compare two builds meaningfully, mask the `PX` serial numbers
first, the way `iso_read::mask_px_serials()` in `tests/common/iso_read.h` does.

### Output Control

| Option | Details |
| --- | --- |
| `-F/--format` | `cdi` is Padus DiscJuggler, `mds` is Alcohol 120% (writes both `.mds` and `.mdf`), `nrg` is Nero Burning ROM, and `gdi` is a GD-ROM: a directory of track files next to a `.gdi` descriptor rather than a single file. When omitted, the format comes from the output extension (`.mds`/`.mdf`, `.nrg`, `.gdi`), falling back to `cdi`. See [cd-images.md](cd-images.md) and [gd-image.md](gd-image.md). |
| `--allow-overwrite` | Lets the run write over output files that are already there. Without it, a build that would replace any of them refuses before writing anything, naming every file in the way, and exits `4`. What counts is the whole set the run produces, not only the name `-o` was given: the image itself, the `.mdf` beside an `.mds`, the `.iso` of `-I/--dump-iso`, and — with `-F gdi` — the `track??.bin`/`track??.iso`/`track??.raw` files in the descriptor's directory, whoever put them there. That last set is why the check is worth having: a track file carries no part of the `.gdi`'s name, so `-o other.gdi` into a directory that already held a set used to overwrite part of it and leave two descriptors sharing one mixed pile of tracks. With the option, the files about to be replaced are named at the default verbosity and the build goes ahead — which is what every release up to now did unconditionally, and in silence. |
| `-I/--dump-iso` | Writes the data track as a plain `.iso` next to the output image, which can use a large amount of extra disk space. Rejected with `-F gdi`. |
| `--gdi-truncate` | Skips padding the high-density data track out to the full physical GD-ROM size. The result is far smaller, but not a standard GD-ROM: its high-density area ends far short of LBA 549150, and whether it boots is up to whatever reads it — an ODE that builds its TOC from the `.gdi` accepts one, nullDC and Demul refuse. It also drops the capacity check, so an oversized disc root gets a warning rather than the refusal a padded build would give. See [gd-image.md](gd-image.md). |
| `--gdi-sector-size` | `2352` writes raw Mode 1 sectors as `track??.bin`, matching what real GD-ROM dumps use; `2048` writes plain user data as `track??.iso`. Defaults to `2352`. |

### Main Executable (1ST_READ.BIN)

| Option | Details |
| --- | --- |
| `--boot-filename` | Sets the name the main executable takes both on the disc and in the IP.BIN's Boot Filename field, instead of the default `1ST_READ.BIN`. The two always match, since the console looks the file up by the name the header declares. A name whose case ISO9660 cannot keep ends up on the disc three ways, one per reader, all naming the same file: `--boot-filename game.bin` gives `GAME.BIN` in the primary tree, because ECMA-119 has no lower case and libisofs upper-cases into it; `GAME.BIN` in the header field, whose job is to match that tree, since it is the one the console reads at boot; and `game.bin` under Joliet and Rock Ridge, which have no such restriction and keep it — those being the names KallistiOS resolves at runtime. `-v 2` reports the header's spelling when it differs. With `-p/--bootstrap`, that header already declares a name and is used as-is; `--boot-filename` still overrides it, rewriting the field to match. |

### Disc Metadata Bootstrap (IP.BIN)

| Option | Details |
| --- | --- |
| `-p/--bootstrap` | With `-F gdi` the given IP.BIN drives both the low- and high-density boot areas (the high-density copy also gets the TOC), matching how the single IP.BIN of a MIL-CD image is used. |
| `-s/--serial` | Defaults to `IND-` followed by a 6-digit hash of the boot binary, so a given build keeps a stable serial. |
| `-H/--peripherals` | A bitfield of 5 hex digits (controller and peripheral requirements) followed by two `0`/`1` flags for VGA support and Windows CE use, e.g. `E000F10` (VGA on, Windows CE off). Defaults to the bootstrap template's own value. |
| `-i/--bootstrap-mr-image` | Replaces the default license screen image. Accepts MR format, or PNG (palettized to at most 128 colors) when the build has PNG support. That is a build-time choice — meson enables it when libpng is found — so this option's `--help` line states which of the two the binary at hand accepts. |
| `-m/--disable-bootstrap-mr` | Embeds no MR boot image at all, not even the default one (which reads: "... is what official games display, but this is 100% unofficial and unlicensed."). Takes precedence over `-i/--bootstrap-mr-image`. |
| `--disable-bootstrap-checks` | Builds the image even when it fails the boot prerequisite checks below. The failures are still reported, as warnings instead of errors, and the run ends with a note saying how many were overridden. For the case where you know something this does not. |
| `--gdi-fpu-rounding` | Picks which of the two bundled GD-ROM bootstraps goes on the disc: `nearest` (the default, and what every earlier release shipped) or `zero`. They differ only in the second-stage bootstrap, which leaves the SH-4 `FPSCR` rounding mode set one way or the other before handing over to the main executable. Programs built against KallistiOS set the mode themselves and are unaffected; the choice matters to code that inherits whatever the bootstrap left. `-F gdi` only, and ignored — with a warning — when `-p/--bootstrap` supplies a bootstrap of its own. |

#### Boot prerequisite checks

Before writing the image, mkdcdisc checks that the disc meets the minimum a
Dreamcast needs in order to boot from it. The step appears in the output as
`Checking boot prerequisites...`.

The checks matter most with `-t/--external-data-track`, where the filesystem
and the IP.BIN inside it were built by mkisofs from whatever you gave it. They
run over the filesystem mkdcdisc builds itself as well, where they should never
fire — one that does is a bug in mkdcdisc rather than a mistake in your
invocation, and worth reporting.

| Check | If it fails |
| --- | --- |
| A boot header is present, and the fields that are nobody's to choose say what they must: the Hardware ID `SEGA SEGAKATANA `, the Maker ID `SEGA ENTERPRISES`, and a Boot Filename field naming a plain file. | **Error** |
| The SEGA license screen code, IP.BIN offsets `0300-36FF`, is unmodified. | **Error** |
| The file the Boot Filename field names is at the **root** of the filesystem. A boot binary one directory down is not a boot binary. | **Error**, or a **warning** when the file is there under a different case, or when the volume's root directory cannot be read |
| The volume was built for the sector it is about to be written at — the `-C 0,MSINFO` given to mkisofs. See [msinfo.md](msinfo.md). | **Warning** |

An error stops the build (exit code `5`) and can be overridden with
`--disable-bootstrap-checks`. A warning never stops it.

What separates the two is how strongly the answer is known, not how bad the
consequence would be. The license screen code is an error because it is a
documented hardware fact: the boot ROM holds its own copy of those 13312 bytes
and compares the disc's against it byte for byte, refusing to boot on any
difference at all, so a disc that fails it does not boot on any console. The
MSINFO check is a warning because mkdcdisc's figure is a model of where the
session lands rather than a measurement of it, and a build refused on the
strength of a model is a build refused on an opinion.

Only the volume the console boots is checked. With `-L merged-data` that is
session 2; session 1 is read through session 2's merged view of it and never
booted, so it needs no header of its own, and one it happens to carry is
reported as unused. With `-F gdi` it is the high-density volume: the
low-density one carries a 256-byte copy of the same header by design, naming a
boot file that lives in the high-density area, and no license screen code at
all, because nothing ever boots from it.

### Data Track Contents

| Option | Details |
| --- | --- |
| `-d/--directory` | Includes the directory recursively, the root directory itself included. Repeat for multiple directories. |
| `-D/--directory-contents` | Includes the directory's contents recursively, excluding the root directory itself. Repeat for multiple directories. |
| `-f/--file` | Adds a single file to the root of the data track. Repeat for multiple files. |
| `-S/--sort-file` | Controls where files are laid out on the disc; see [sort-file.md](sort-file.md). With `-F gdi` it orders the high-density filesystem, the low-density one having its own `--gdi-lda-sort-file`. |
| `-N/--disable-data-track-padding` | Turns off the filler that pads the data track out to a 74-minute CD. The padding is written first and weighted to the lowest block addresses, which pushes the real content to the outer edge of the disc where a CD reads fastest, so switching it off trades load speed for a much smaller image. It is off for `-F gdi` regardless, which pads its own high-density track instead; use `--gdi-truncate` there. Disc contents that already reach a 74-minute disc leave nothing to pad with, which is noted at `-v 2` and is not an error. |
| `--duplicates-once` | Files with identical byte-for-byte content are written only once to the data track, and each copy points to that single extent. This is what the `-duplicates-once` option of `mkisofs` does, and like it the test is on content: files are matched by reading them, never by the device or inode numbers of the source filesystem. This option is disabled by default, and its absence does not alter the image in any way. Candidate files are first grouped by size, so that a directory tree containing no files of the same length is never read. Files of the same size are then digested over their first 64 KiB, the same first pass `mkisofs` makes, so that files which merely happen to share a length are told apart without being read whole; only those whose prefixes match are digested entirely. Any pair about to be merged is then compared byte by byte, so that a digest collision cannot place two different files on the same extent -- a check `mkisofs` does without, trusting its digest outright. If multiple copies match, the one whose path is sorted first retains the extent, ensuring reproducibility of the result. Four files are never shared and retain their own extent, even if their contents match something else: the boot binary, because a GD-ROM places it at the very end of the filesystem and a shared extent has only one position; and the data track filler, the GD-DA spacer and the closing track filler, because each exists solely to occupy a range of addresses -- and since they are written from zeros, any two of the same length are byte-identical, which makes such a match a certainty rather than a remote risk. Files the sort file places are deduplicated like any other: a shared extent has a single position, so the set takes the highest weight among it, as `mkisofs` does. A file weighted below that is placed where the heavier copy asked for, and is named in a warning -- the one part `mkisofs` leaves silent. With `-F gdi`, this applies to the high-density area. The low-density area, consisting of a few hundred text file sectors, is not modified. |
| `-V/--volume-label` | Used as given when passed, and derived from the IP.BIN title (`-n/--title`) otherwise. Either way it is sanitized to at most 32 uppercase characters, with non-alphanumeric ones replaced by `_`. |
| `--copyright` | The file's contents become `COPYRIGH.TXT`, referenced by the ISO9660 volume's Copyright File Identifier. With `-F gdi` it goes in the low-density area (where pressed discs keep it) and defaults to a line built from `-n`/`-a`/`-r`; in the other formats it is only added when given. |
| `--abstract`, `--biblio` | The same, for `ABSTRACT.TXT` (Abstract File Identifier) and `BIBLIOGR.TXT` (Bibliographic File Identifier). |
| `--gdi-disable-copyright`, `--gdi-disable-abstract`, `--gdi-disable-biblio` | Opt out of one of the three: the file is not generated, its Identifier field in the Primary Volume Descriptor (PVD) is left empty, and a `--copyright`/`--abstract`/`--biblio` supplying it is ignored, with a warning. `-F gdi` only, because it is the only format that writes these files on its own: a MIL-CD carries just the ones you name, so leaving out the option is already the way to leave out the file, while a GD-ROM generates all three whether asked or not. |
| `--gdi-lda-*` | Put content in the GD-ROM low-density area. That area is not reachable from the high-density filesystem, so this is for content a PC or CD player reads, not for game data. See [gd-image.md](gd-image.md). |
| `--gdi-lda-sort-file` | Orders the low-density filesystem, exactly as `-S` orders the high-density one and in the same format. The two areas are separate filesystems whose paths both start at `/`, so each sort file is resolved against its own tree only; the three generated text files can be weighted alongside your own content. |

A disc that ends up longer than 360000 sectors — the nominal capacity of an
80-minute CD-R — is warned about by name and sector count before the image is
written, and then written anyway. It is a warning and not a refusal because CD-R
capacity is a property of the blank rather than of the format: 90- and
99-minute media exist, and overburning an 80-minute blank by a small margin
works on many drives. mkdcdisc cannot know which blank you have, only that the
image has outgrown the one nearly everyone does. `-F gdi` is exempt: a GD-ROM's
high-density zone is a single fixed size, so `build_gd` refuses an oversized
disc root outright instead — see [gd-image.md](gd-image.md). `-q` silences the
warning like every other advisory.

Two files are never added by `-d`, `-D` or `-f`, each with a warning naming it:

- **the file given to `-e`/`-b`/`-B`.** The disc already carries it, converted,
  as `1ST_READ.BIN`, so a second copy is dead weight — for an ELF, typically the
  size of the boot binary over again. It is matched by identity rather than by
  name, so `-e build/game.elf -D build/` is caught exactly like a tree holding a
  `1ST_READ.BIN`, and `-f` is treated no differently: naming the file explicitly
  does not make a second copy of it useful. If you really want it on the disc,
  copy it under another name first.
- **anything else carrying the boot filename**, `1ST_READ.BIN` unless
  `--boot-filename` says otherwise. Copying it would replace the binary that was
  just generated with one that does not boot.

The three generated defaults are single CRLF-terminated lines, the shape every
reference dump uses:

| File | Generated content |
| --- | --- |
| `COPYRIGH.TXT` | `<name> - Copyright (c) <year> <author>. All Rights Reserved` |
| `ABSTRACT.TXT` | `<name>` |
| `BIBLIOGR.TXT` | `<name> - Created with mkdcdisc v<version>` |

`<name>` and `<author>` are the IP.BIN title and company (`-n`/`-a`), and
`<year>` is the first four digits of `-r/--release`. A file of the same name in
the disc contents always wins over both the generated default and the option,
with a warning naming which copy was kept.

### Disc Layout

| Option | Details |
| --- | --- |
| `-L/--disc-layout` | Picks the arrangement of sessions, out of `audio-data`, `data-data` and `merged-data`. Absent, `audio-data` is used, so the option only ever has to be given to ask for one of the other two; naming it explicitly is the same run. `data-data` produces a Data/Data image, where a second data session holds a boot track. `merged-data` produces one whose second data session is instead a full ISO merging the first session's files with new ones (built externally via `mkisofs -M`), and requires exactly two `-t/--external-data-track` ISOs (session 1, then session 2). Neither of those two has an audio session, so both are incompatible with `-c/--cdda-track`. |
| `-M/--print-msinfo` | Prints the MSINFO (data-track start LBA) for the given `-c` audio tracks and exits; feed it to mkisofs as `-C 0,MSINFO` when building an ISO for `-t`. With no `-c` tracks, prints the Audio/Data default. With `-L data-data`, always prints 0. With `-L merged-data` and a single `-t` ISO given, prints the session 2 start LBA computed from that ISO's size, to feed mkisofs as `-C 0,MSINFO -M <iso>` when building session 2. See [msinfo.md](msinfo.md). |

The whole group is MIL-CD only, which is why neither of its two options carries
the `(not with -F gdi)` marker individually: a GD-ROM has one fixed structure,
so there is no layout to pick, and the MSINFO number exists to build an ISO for
`-t`, which `-F gdi` does not accept either.

`-L` takes a value, and has no meaning without one. It is the one option whose
earlier spellings are not kept as deprecated names: the layouts used to be a
flag each, and a flag cannot be turned into a value-taking option and still
mean what it did. An invocation written against the older releases is refused
outright rather than building something else.

### Disc Tracks

| Option | Details |
| --- | --- |
| `-c/--cdda-track` | Accepts `.wav` or raw CD-DA. See [msinfo.md](msinfo.md) for the accepted formats. Each track must run at least 4 seconds (Red Book's minimum). How many are accepted depends on the disc: at most **98** for a MIL-CD, since a disc holds 99 tracks and the data track takes one of them; at most **95** with `-F gdi`, where they become GD-DA and the high-density area spans tracks 3 to 99 with a data track at each end of the audio — which is also the size of the TOC written into the system area. Both are refused before any audio file is opened. See [gd-image.md](gd-image.md). |
| `--gdi-lda-cdda-track` | Replaces the ~7 seconds of generated silence in low-density track 2 with an audio file, in the same formats `-c` takes. The track is then sized from the file, so it must run at least 4 seconds (Red Book's minimum) and leave the whole low-density area below LBA 45000, where the high-density zone begins; both are checked before building starts. See [gd-image.md](gd-image.md). |
| `-t/--external-data-track` | Uses a pre-built ISO (made externally by mkisofs, with IP.BIN embedded via `-G`) as the data track, instead of building the filesystem and IP.BIN internally. Works with `-L/--disc-layout` as usual; with `-L merged-data`, give `-t` twice (session 1 ISO, then session 2 ISO). Build the ISO with `-C 0,MSINFO` (see `-M`). Disables all other data-track and IP.BIN related options. Not accepted with `-F gdi`, which always builds its own filesystem and IP.BIN. The ISO is checked against what a Dreamcast needs in order to boot from it — see "Boot prerequisite checks" above — since mkdcdisc did not build it and cannot otherwise vouch for it. |

## Exit codes

A script wrapping mkdcdisc can only tell one failure from another by the
number it exits with:

| Code | Meaning |
| --- | --- |
| `0` | The image was written, or what was asked for (`-M/--print-msinfo`, `--help`) was printed. |
| `2` | The invocation was refused: an unknown option, a bad value, or a combination that cannot mean anything (`-t` with `-F gdi`, `--cdda-track` with `-L data-data`, ...). Nothing was attempted, and the message is followed by a pointer to `--help`. |
| `4` | A file the run needs is missing or unreadable — the ELF, a `-t` ISO, a `--copyright` file — or a file it would write is already there and `--allow-overwrite` was not given. The options were right; the disc could not be built from them as things stand on disc. |
| `5` | The build itself failed, having got far enough to try — or was refused because the disc would not boot, which `--disable-bootstrap-checks` overrides (see "Boot prerequisite checks"). |
| `130` | Interrupted with Ctrl+C. More generally 128 plus the signal number, so a different signal yields a different code. |

The line between `2` and `4` is whether `--help` could have helped: it has
nothing to say about a missing ISO. Every code but `0` prints its explanation
on stderr, so `-q/--quiet` does not hide a failure — it only silences the
progress of a successful run.

## Deprecated option names

Several long options were renamed so that the options belonging to the same
part of the disc share a prefix and group together in `--help`. The short
flags are unchanged, and the previous long names are still accepted, so
existing scripts keep working; using one prints a warning on stderr naming its
replacement. They are not listed in `--help` and may be removed in a future
release.

An option can carry more than one, `-b` and `-B` having been renamed twice:
once into the group prefix, then again to spell `binary` out rather than
abbreviate it. Every spelling reaches the same option and points at the current
name.

| Previous name | Current name |
| --- | --- |
| `--elf` | `-e/--main-elf` |
| `--unscrambled-binary` | `-b/--main-binary-unscrambled` |
| `--main-bin-unscrambled` | `-b/--main-binary-unscrambled` |
| `--scrambled-binary` | `-B/--main-binary-scrambled` |
| `--main-bin-scrambled` | `-B/--main-binary-scrambled` |
| `--ipbin` | `-p/--bootstrap` |
| `--name` | `-n/--title` |
| `--image` | `-i/--bootstrap-mr-image` |
| `--no-mr` | `-m/--disable-bootstrap-mr` |
| `--no-padding` | `-N/--disable-data-track-padding` |
| `--cdda` | `-c/--cdda-track` |
| `--external-iso-track` | `-t/--external-data-track` |

The warning follows `-v/--verbosity` and is silenced by `-q/--quiet`. Like the
startup banner it goes to stderr, so it never mixes into the machine-readable
stdout of `-M/--print-msinfo`.
