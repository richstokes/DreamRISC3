# Examples

Recipes organized by what you're trying to build, rather than by concept.
See [docs/cd-images.md](cd-images.md) and [docs/gd-image.md](gd-image.md) for
the reasoning behind each Layout and Mode; this page is just the commands.

## MIL-CD (Audio/Data layout, default)

### Standard Mode

```bash
mkdcdisc -e game.elf -d data/ -o game.cdi
```

With CDDA audio tracks:

```bash
mkdcdisc -e game.elf -d data/ -c intro.wav -c music.wav -o game.cdi
```

### External ISO Mode

`IP.BIN` and the contents of `cd_root/` are yours to produce in this mode;
see [From a KallistiOS program to an external ISO](#from-a-kallistios-program-to-an-external-iso)
below for where they come from.

```bash
# Step 1: compute MSINFO for your audio tracks
MSINFO=$(mkdcdisc -M -c track01.wav -c track02.raw)

# Step 2: build the ISO with that MSINFO
# Where "cd_root" is a directory containing game data files
mkisofs -C 0,$MSINFO -V "GAME_NAME" -G IP.BIN -joliet -rock -l -o data.iso cd_root/

# Step 3: wrap into CDI with the same audio tracks
mkdcdisc -t data.iso -c track01.wav -c track02.raw -o disc.cdi

# ...or wrap into an Alcohol 120% image instead
mkdcdisc -t data.iso -c track01.wav -c track02.raw -o disc.mds

# ...or wrap into a Nero Burning ROM image instead
mkdcdisc -t data.iso -c track01.wav -c track02.raw -o disc.nrg
```

## Data/Data (`-L data-data`)

### Standard Mode

```bash
mkdcdisc -e game.elf -d data/ -L data-data -o game.cdi
```

### External ISO Mode

Since Data/Data has no audio session, build the ISO at MSINFO 0 instead:

```bash
# You may omit -C 0,0 in that case
# Where "cd_root" is a directory containing game data files
mkisofs -C 0,0 -V "GAME_NAME" -G IP.BIN -joliet -rock -l -o data.iso cd_root/
mkdcdisc -t data.iso -L data-data -o disc.cdi
```

## Merged Data/Data (`-L merged-data`)

Only exists as an External ISO Mode variant (`-t`, given twice): session 1's
ISO is built first (no `mkisofs -G`, it never boots directly), then session 2
is built as a `mkisofs -M` merge of session 1 plus new files, with the real
`-G` IP.BIN:

```bash
# Step 1: build session 1's ISO; no -G, it's never booted from directly
# (session1_data/ must be at most 68,808,704 bytes for this example's MSINFO to hold)
mkisofs -V "GAME_NAME" -o session1.iso session1_data/

# Step 2: compute session 2's MSINFO from session 1's ISO
MSINFO2=$(mkdcdisc -M -L merged-data -t session1.iso)
# Output: 45000

# Step 3: build session 2 as a merge of session 1's files with new ones,
# with the real IP.BIN this time
mkisofs -C 0,$MSINFO2 -V "GAME_NAME" -G IP.BIN -M session1.iso -o session2.iso session2_data/

# Step 4: wrap both ISOs into the final disc image
mkdcdisc -L merged-data -t session1.iso -t session2.iso -o disc.cdi
```

## From a KallistiOS program to an external ISO

Every External ISO Mode recipe above starts from two things mkdcdisc does not
build for you once `-t` is in play: a `cd_root/` tree holding the scrambled
boot binary alongside your data files, and the `IP.BIN` that `mkisofs -G`
embeds. Standard Mode's `-e game.elf` does all of that internally; with `-t`
you produce them yourself, with the utilities KallistiOS ships in
`$KOS_BASE/utils` (given by full path here, as that directory isn't on the
`PATH` in a plain KOS install):

```bash
# Step 1: build the release ELF -- no debugging symbols, no gdb_init() call
make            # ...then "make dist" if your Makefile defines it

# Step 2: convert the ELF to an unscrambled binary
$KOS_BASE/utils/elf2bin/elf2bin game.elf game.bin

# Step 3: scramble it into cd_root/ as the boot binary
mkdir -p cd_root
$KOS_BASE/utils/scramble/scramble game.bin cd_root/1ST_READ.BIN

# Step 4: generate the bootstrap
$KOS_BASE/utils/makeip/makeip -v IP.BIN
```

`cd_root/` is now the directory to hand to `mkisofs`, and `IP.BIN` the file to
pass it as `-G`, exactly as the recipes above do. The disc metadata that
`-n`/`-a`/`-s`/`-r`/`-P`/`-i` would carry in Standard Mode is `makeip`'s
business at step 4 instead, since those options are rejected alongside `-t`;
see its own documentation for how to set the fields.

None of this is needed if you don't want `mkisofs`'s control over the
filesystem: Standard Mode covers steps 2 to 4 itself, straight from the ELF,
with the data files in their own tree rather than mixed in with the boot
binary.

```bash
mkdcdisc -e game.elf -D data/ -o game.cdi
```

The intermediate forms remain usable there too, if you already have them:
`-b/--main-binary-unscrambled` takes the step 2 output, and
`-B/--main-binary-scrambled` the step 3 one.

## GD-ROM (`.gdi`)

Always Standard Mode, no Layout choice (see [docs/gd-image.md](gd-image.md)):

```bash
mkdcdisc -e game.elf -d data/ -o game.gdi
```

With GD-DA audio tracks (placed inside the high-density area, between an
opening data track and a closing one):

```bash
mkdcdisc -e game.elf -d data/ -c music1.raw -c music2.raw -o game.gdi
```

A smaller image for an ODE that accepts one, without the ~1GB of physical-size
padding a standard GD-ROM carries. Not the build to hand an emulator — see
[gd-image.md](gd-image.md):

```bash
mkdcdisc -e game.elf -d data/ --gdi-truncate -o game.gdi
```

## Disc metadata, IP.BIN, and the license screen (any layout)

These flags apply to any Standard Mode build above -- MIL-CD, Data/Data, or
GD-ROM -- since they all go through the same internal IP.BIN generation.
They don't apply to External ISO Mode, where `mkisofs -G` embeds the IP.BIN
instead. See [docs/cli-reference.md](cli-reference.md) for the full list.

With disc metadata (title, author, serial, release date, version, and the
ISO9660 volume label):

```bash
mkdcdisc -e game.elf -d data/ \
  -n "My Game" -a "My Studio" -s "T-00001" -r 20260101 -P V1.000 -V MY_GAME \
  -o game.cdi
```

With a custom license screen image, replacing the default one that reads
"... is what official games display, but this is 100% unofficial and
unlicensed.":

```bash
mkdcdisc -e game.elf -d data/ -i license.mr -o game.cdi
```

...or no license screen image at all:

```bash
mkdcdisc -e game.elf -d data/ -m -o game.cdi
```

With a fully custom IP.BIN, bypassing all of the above -- `-n`/`-a`/`-s`/
`-r`/`-P`/`-i`/`-m` are then ignored in favor of the file's own values:

```bash
mkdcdisc -e game.elf -d data/ -p custom_ip.bin -o game.cdi
```
