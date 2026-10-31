# CD Images (CDI / MDS+MDF / NRG)

DiscJuggler (`.cdi`), Alcohol 120% (`.mds`/`.mdf`) and Nero Burning ROM
(`.nrg`) all share the same logical CD-shaped disc structure: a Layout
choice (Audio/Data, Data/Data, or Merged Data/Data) built through one of two
Modes of Operation (Standard or External ISO). GD-ROM (`.gdi`) is a
different type entirely, it always builds its own filesystem, with no
Layout or Mode choice so it has its own doc, [docs/gd-image.md](gd-image.md).

```
# Padus DiscJuggler, inferred from the extension
mkdcdisc -e game.elf -o game.cdi

# Padus DiscJuggler, selected explicitly
mkdcdisc -e game.elf -o game.cdi --format cdi

# Alcohol 120%, selected explicitly
mkdcdisc -e game.elf -o game.mds --format mds

# Alcohol 120%, inferred from the extension
mkdcdisc -e game.elf -o game.mds

# Nero Burning ROM, selected explicitly
mkdcdisc -e game.elf -o game.nrg --format mds

# Nero Burning ROM, inferred from the extension
mkdcdisc -e game.elf -o game.nrg
```

See [docs/examples.md](examples.md) for complete recipes organized by what
you're trying to build.

## Disc Layouts

mkdcdisc supports 3 disc layout modes, each with different compatibility profiles. One option picks between them, `-L/--disc-layout`, whose value is `audio-data` (the default), `data-data` or `merged-data`. It is a MIL-CD option: `-F gdi` rejects it, a GD-ROM having only one possible structure.

### Audio/Data Layout (`-L audio-data`, the default, MIL-CD format)

The classic layout used by SEGA's mastering tools, implementing the MIL-CD (Music Interactive Live CD) format. MIL-CD is a digital optical storage format created by Sega that allows code to be loaded on the Dreamcast via standard compact disc media, consisting of audio tracks before the track containing Dreamcast-bootable data. Session 1 contains a CDDA audio track (either from your `.wav` files with `-c`, or a 4-second silence filler), and Session 2 contains the data track (the ISO9660 filesystem).

This layout is the most feature-rich (you can include background music or multimedia content via CDDA), and every Dreamcast boots it except the last ones built: Sega removed MIL-CD support late in the console's life. Exactly which units, and what that means for your choice of layout, is covered in [Which consoles boot MIL-CD](#which-consoles-boot-mil-cd) below.

Use audio tracks with `-c` (e.g., `-c intro.wav -c music.wav`), up to 98 of
them: a disc holds 99 tracks in all, Red Book's ceiling, and the data track
takes one. Past that mkdcdisc refuses the invocation rather than building a
disc no writer will accept.

### Data/Data Layout (`-L data-data`)

An alternative layout where both sessions are data tracks. Session 1 holds the full ISO (built as MSINFO 0), and Session 2 holds a "boot head", a duplicate of the ISO's first 18 sectors (the IP.BIN system area plus ISO9660 volume descriptors, padded to the 300-sector minimum). It comes at the cost of not supporting CDDA audio tracks: if you specify both `-L data-data` and `-c`, mkdcdisc will error and exit.

This is still a MIL-CD -- same multi-session disc, same boot path, with the audio session swapped for a data one. That variation is precisely the point: it keeps booting on the later units where an Audio/Data disc no longer does (see [Which consoles boot MIL-CD](#which-consoles-boot-mil-cd)), because what Sega tightened up there was the audio-session check, not the multi-session boot itself. Data/Data slips through what is left.

Use Data/Data mode with `-L data-data` (incompatible with `-c/--cdda-track`). Works with both Standard Mode and External ISO Mode (`-t`).

### Merged Data/Data Layout (`-L merged-data`)

Another Data/Data variant, but where the second session is a genuine, independently-built ISO instead of a boot-head duplicate: it's the full merge (via `mkisofs -M`) of session 1's files with new ones, plus its own IP.BIN. Unlike `data-data`, session 1 needs no IP.BIN of its own -- it's never booted from directly, only its files are read (either directly, or through session 2's merged view of them).

This mode only exists as an External ISO Mode variant (`-t`, given twice): mkdcdisc does not build either filesystem itself here, it only wraps the two ISOs you already built with `mkisofs` into the final disc image. There is no Standard Mode equivalent. Incompatible with `-c/--cdda-track`.

### Choosing a layout

If audio tracks are important to your project, stick with Audio/Data (default). If you're targeting maximum hardware compatibility or working around reported disc-reading issues on certain Dreamcast units, you may try Data/Data (`-L data-data`) or Merged Data/Data (`-L merged-data`).

In practice Audio/Data is the right default: the units that cannot boot it are only the tail end of a console whose production stopped a few months later, so they are uncommon compared to everything made before. Reach for a Data/Data layout when you know you are targeting one, or when a user reports a disc that won't boot.

#### Which consoles boot MIL-CD

Sega did not drop MIL-CD in one go, and it does not line up with the "V2"
console revision the way it is often told: the change landed *part-way through*
the V2 production run, as a BIOS and hardware configuration change. So the
revision alone tells you nothing, the manufacture date does, and it is printed
on the label on the underside of the console:

| Manufactured | Boots MIL-CD |
| --- | --- |
| October 2000 and earlier | Yes |
| November 2000 | The changeover, either way |
| December 2000 and later | No |

These are the dates the community generally agrees on, not something Sega
published, so treat November 2000 as a grey zone: burn a disc and try rather
than assume. This is the whole reason the two Data/Data layouts above exist.

Other write-ups break the same story down by console revision rather than by
date, and put the cutoff earlier, see the compatibility table in
[Dreamcast CDI Burner (DCDIB)](https://alex-free.github.io/dcdib/),
which has Audio/Data already failing on late Rev 1 units. The dates above are
the ones the community most widely agrees on, and this doc sticks with them,
but the disagreement is one more reason to test rather than assume.

Sources: [Sega Retro: Mil-CD](https://segaretro.org/Mil-CD),
[this r/dreamcast thread](https://www.reddit.com/r/dreamcast/comments/6zudpb/question_about_boot_discs/),
and [Dreamcast CDI Burner (DCDIB)](https://alex-free.github.io/dcdib/).

## Modes of Operation

mkdcdisc supports two workflows:

### Standard Mode (default)

Build everything from scratch: you provide an ELF/binary, mkdcdisc creates the filesystem, embeds the IP.BIN, generates the ISO internally, and assembles the disc image (CDI, MDS+MDF, or NRG; also GD-ROM, see [docs/gd-image.md](gd-image.md)).

```bash
mkdcdisc -e game.elf -d data/ -o game.cdi
```

### External ISO Mode (`-t/--external-data-track`)

Delegate filesystem and `IP.BIN` creation to external tools like `mkisofs`, then wrap the resulting ISO into a CDI, MDS+MDF, or NRG image, per `-F/--format` (or the output extension, same as the normal build). Not available for GD-ROM output, which always builds its own filesystem. Useful when you need fine control over the filesystem (e.g. Joliet, Rock Ridge, custom sort order) or when reusing ISOs built elsewhere.

The mode is still called External *ISO* Mode -- what you hand it is an ISO built externally by `mkisofs` -- even though `-t`'s long name is `--external-data-track`, not `--external-iso-track` as it used to be: that rename was about the flag's *role* once mkdcdisc has it (it becomes the disc's data track), part of a broader pass aligning every track-related option on a `-track` suffix (see the deprecated option names table in [docs/cli-reference.md](cli-reference.md)). The two names describe different things, not a renamed mode.

When using `-t`, options that build the filesystem (`-a`, `-b`, `-B`, `-d`, `-D`, `-e`, `-f`, `-i`, `-I`, `-m`, `-n`, `-N`, `-p`, `-r`, `-s`, `-S`) are rejected; only `-c` (CDDA tracks), `-F`/`--format`, `-L`/`--disc-layout`, and `-o` are allowed alongside `-t`.

Because mkdcdisc did not build the ISO, it checks it before wrapping it: that a
boot header is there and says what it must, that the license screen code is
untouched, that the file the header names is at the root of the filesystem, and
that the ISO was built for the sector it will be written at. A disc that fails
one of the first three does not boot, so the build stops rather than producing
it; `--disable-bootstrap-checks` builds it anyway. See "Boot prerequisite
checks" in [docs/cli-reference.md](cli-reference.md).

This is the one part of External ISO Mode that can reject an invocation which
earlier releases accepted — an ISO built without `-G`, say, or with a modified
license screen zone. Such an image never booted; it was simply written without
comment.

See [docs/examples.md](examples.md) for complete `mkisofs` recipes covering
MIL-CD, Data/Data, and Merged Data/Data via External ISO Mode.

## Format Internals

### About the CDI format

DiscJuggler `.cdi` is the single-file image format created by Padus
DiscJuggler burning software. Unlike the MDS+MDF pair, a CDI keeps
everything in one file: the raw track data comes first, followed by a
descriptor appended at the very end that lists the sessions, tracks, track
modes and disc metadata. A small offset stored in the last four bytes of the
file tells a reader where that trailing descriptor begins, so the image is
parsed back-to-front.

It became the de-facto format for Dreamcast homebrew because it cleanly
represents the console's two-session "trick" layout (an audio session
followed by a bootable data session) in a single file that emulators and
burning tools read directly. This is the format mkdcdisc produces by default,
and the role historically filled by the `cdi4dc` tool.

### About the MDS+MDF format

Alcohol 120% images come as a pair of files sharing the same base name:

- **`.mds`**: the *Media Descriptor*, a small file holding the disc layout:
  sessions, tracks, track modes, pregaps, LBAs and the offsets of each track
  inside the data file.
- **`.mdf`**: the *Media Data File*, containing the actual disc contents as
  full raw 2352-byte sectors.

For a Dreamcast disc, mkdcdisc writes the usual two-session layout: a first
session holding the CDDA audio track(s) (or a short silent placeholder track
when no audio is supplied), followed by a second data session in XA Mode 2
Form 1. Data sectors are fully encoded; sync, header, subheader and EDC/ECC
are generated via libedc, exactly as on a real disc, so the resulting image
boots in emulators and can be burned as-is.

Track pregaps are declared in the descriptor but not stored in the `.mdf`,
which is the convention Alcohol uses for disc-at-once images; the burning
software synthesizes them on the fly. The `.mds` footer references its data
file with a `*.mdf` wildcard, so the pair keeps working even if both files
are renamed together.

This support is a modern reimplementation of the MDS+MDF writer originally
shipped in `mds4dc`.

### About the NRG format

Nero Burning ROM `.nrg` is, like CDI, a single-file format: raw track data
(including pregaps, which are physically stored here) comes first, followed
by a trailer of self-describing chunks, followed by a 12-byte footer at the
very end of the file pointing back at the start of that trailer -- so, like
CDI, it's parsed back-to-front.

Nero images can be written in two very different recording modes, which show
up as different trailer chunks: **TAO** (Track-At-Once, `ETNF`/`ETN2`
chunks), which has no cue sheet and imposes automatic gaps between tracks,
and **DAO** (Disc-At-Once, `CUEX`/`DAOX`/`SINF` chunks), a full cue sheet
plus per-track descriptors with sector-accurate offsets. mkdcdisc only ever
produces **DAO** images: the Dreamcast's two-session layout needs a
sector-precise MSINFO, a -150 pregap on track 1, and precise session
boundaries, none of which TAO's automatic gaps can guarantee.

For each session, mkdcdisc writes a `CUEX` cue sheet (pregap/data start LBAs
for every track, plus the session's lead-out), a `DAOX` chunk (per-track
byte offsets into the file, sector size and mode), and a `SINF` chunk
(that session's track count) -- mirroring the same Audio + XA Mode 2 Form 1
raw-sector encoding (sync, header, subheader, EDC/ECC via libedc) that the
CDI and MDS+MDF writers already produce.
