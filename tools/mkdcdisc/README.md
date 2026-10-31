# mkdcdisc

A modern command-line interface (CLI) tool for generating disc images for the
Sega Dreamcast.

mkdcdisc is the one-stop shop for creating Sega Dreamcast disc images, and the
successor to `img4dc` (`cdi4dc`, `mds4dc`). A single command does what
`scramble`, `makeip`, `objcopy` and the `img4dc` tools used to do together, and
covers the whole range of Dreamcast mastering needs: automatic disc padding,
CDDA audio tracks, alternative disc layouts, GD-ROM images, and wrapping
externally-built ISOs (e.g. from `mkisofs`) when you would rather build the
filesystem yourself.

Four output formats are supported:

* GD-ROM (`.gdi`), the Dreamcast's own disc format;
* Three CD-ROM container formats, each holding a self-booting
  [MIL-CD (Music Interactive Live-CD)](https://segaretro.org/Mil-CD) image, in
  the [disc layout](docs/cd-images.md#disc-layouts) of your choice:
    * Padus DiscJuggler (`.cdi`), the de facto standard and mkdcdisc's default
      output format;
    * Alcohol 120% (`.mds`/`.mdf`);
    * Nero Burning ROM (`.nrg`).

## Building

mkdcdisc is cross-platform and builds on **Linux**, **macOS**, and **Windows**
with `meson`. See [BUILDING.md](BUILDING.md) for platform-specific
dependencies and instructions (including the two Windows variants).

```
git clone https://gitlab.com/simulant/mkdcdisc
cd mkdcdisc

meson setup builddir
meson compile -C builddir

# run
./builddir/mkdcdisc -h
```

## Usage

mkdcdisc prints a short startup banner (to stderr, so it never pollutes
machine-readable stdout output such as `-M/--print-msinfo`); pass
`--no-banner` (or `-q/--quiet`) to suppress it.

Its output is colorized when it goes to a terminal, and left as plain text
when it is redirected into a file or a pipe. `--color always|never`
(or `--disable-color`, or the `NO_COLOR` environment variable) overrides that
— see [docs/cli-reference.md](docs/cli-reference.md#general).

Running mkdcdisc with no arguments at all only prints the banner, the
description, and the usage line, pointing you to `--help` for the full
option list:

```
mkdcdisc v0.0.4 - Sega Dreamcast Disc Image Generator
Copyright (C) 2022-2026 Simulant and contributors

A modern command-line interface (CLI) tool for generating disc images for the Sega Dreamcast.

This tool creates a GD-ROM (.gdi) or a self-booting MIL-CD image in Padus DiscJuggler (.cdi),
Alcohol 120% (.mds/.mdf), or Nero Burning ROM (.nrg) format. These images can be used directly on an
emulator or via a virtual drive, or on a physical console via a burned CD-R or an optical drive
emulator (ODE).

Usage: mkdcdisc [OPTION]... [-e ELF | -b|-B BIN | -t ISO] -o OUTPUT

Try 'mkdcdisc --help' for the list of valid options.
```

See [docs/cli-reference.md](docs/cli-reference.md) for the full option list
(the same text `--help` prints), grouped by what part of the disc each option
affects, plus the table of previous option names still accepted for backward
compatibility.

## Documentation

Full documentation lives in [docs/](docs/README.md).

See also: [BUILDING.md](BUILDING.md), [CHANGELOG.md](CHANGELOG.md), [CONTRIBUTORS.md](CONTRIBUTORS.md).

## License

mkdcdisc's own source code is under the MIT license: see [LICENSE](LICENSE).

A built binary also contains third-party components under other licenses,
copyleft ones included: libisofs is GPL-2.0-or-later and libedc is CDDL-1.0, and
both are in every build. If you redistribute a binary, their terms apply too.
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lists everything that goes in
and points at each component's own license text.
