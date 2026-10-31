# Building mkdcdisc

mkdcdisc is cross-platform and builds on **Linux**, **macOS**, and **Windows**.
In every case you will need a C++ compiler, `git`, `meson`, `libisofs`, and
`libpng`.

`libpng` is the only **optional** one, and it serves a single purpose: letting
`-i/--bootstrap-mr-image` take the bootstrap (`IP.BIN`) license-screen logo as a
PNG and convert it to MR itself. Nothing else in mkdcdisc uses it. Meson picks it
up when it is there and turns PNG support on silently, so building without it is
not an error — such a build still accepts an already-converted `.mr` logo, it
just refuses PNG files, and its `--help` line for `-i` says which of the two it
takes. Install it unless you deliberately want a build without PNG support.

That silence is convenient locally and dangerous in a pipeline, so the choice can
be made explicit with `-Dpng=`:

| `meson setup builddir …` | Behaviour |
|:---|:---|
| *(nothing)*, or `-Dpng=auto` | The default. PNG support if libpng is there, silently none if it isn't. |
| `-Dpng=enabled` | A missing libpng fails `meson setup`. What the CI build jobs use, so they cannot publish a binary whose PNG support silently vanished. |
| `-Dpng=disabled` | libpng is never looked up, even when installed. |

## Linux

Fedora:

```
dnf install git meson gcc gcc-c++ libisofs-devel libpng-devel
```

Ubuntu / Debian:

```
sudo apt install git meson build-essential pkg-config libisofs-dev libpng-dev
```

## macOS

```
brew install git meson pkg-config libisofs libpng
```

## Windows

On Windows, mkdcdisc is always built with **MinGW-w64**, but you can pick
between two variants depending on the kind of binary you want.

### MSYS2 variant (`MSYSTEM=MSYS`)

This builds against the MSYS2 POSIX emulation layer. The resulting
`mkdcdisc` binary runs **only inside the MSYS2 environment** (from its
command line); it is not a standalone Windows executable.

Two of the dependencies have to be built from source here: the `msys`
repository packages neither `libisofs` nor `libpng` (only the
`mingw-w64-*` variants of the latter exist, and those are of no use to an
MSYS build). Skip the `libpng` part if you don't want PNG support.

Both are regenerated (`./bootstrap`, `autoreconf -fi`) before being
configured, and that step is what names the DLLs they install. MSYS2
patches the cygwin branch of `libtool.m4` to rewrite `lib` into `msys-`,
so only a package built with the *installed* libtool comes out as
`msys-png16-16.dll`; libpng's release tarball ships a pre-generated
`configure` carrying upstream's own libtool, which would give you
`cygpng16-16.dll` instead. Both load equally well, but mkdcdisc's
packaging expects the `msys-` name.

```
pacman -S --noconfirm git meson gcc autotools libtool make libiconv-devel zlib-devel

curl -LO https://files.libburnia-project.org/releases/libisofs-1.5.8.tar.gz
tar xf libisofs-1.5.8.tar.gz
cd libisofs-1.5.8
./bootstrap
./configure --prefix=/usr --disable-static
sed -i.bak -e "s/allow_undefined=yes/allow_undefined=no/" libtool
(make || (sed -i.bak -e "s/allow_undefined=yes/allow_undefined=no/" libtool && make))
make install
cd ..

curl -LO https://download.sourceforge.net/libpng/libpng-1.6.58.tar.gz
tar xf libpng-1.6.58.tar.gz
cd libpng-1.6.58
autoreconf -fi
./configure --prefix=/usr --disable-static
make
make install
cd ..
```

### Native MinGW-w64 variant (`MSYSTEM=MINGW64`)

This produces a **native Windows executable** that can run anywhere,
including outside of MSYS2. Because upstream `libisofs` does not support
Windows out of the box, this variant builds against the lightly **patched
copy of libisofs bundled in this repository**. The patches are intentionally
minimal and non-intrusive; they only make the library compile under Windows,
and were applied by hand.

Since the resulting `mkdcdisc.exe` has to stand on its own outside of MSYS2,
this variant links everything statically — `libpng` and `zlib` included, which
`meson.build` asks for explicitly. The executable must therefore import no DLL
beyond the system ones; `objdump -p builddir/mkdcdisc.exe | grep 'DLL Name'`
is the way to check that after touching the link setup.

```
pacman --noconfirm -Syu git mingw-w64-x86_64-autotools mingw-w64-x86_64-gcc mingw-w64-x86_64-libtool mingw-w64-x86_64-make mingw-w64-x86_64-libiconv mingw-w64-x86_64-libpng mingw-w64-x86_64-git mingw-w64-x86_64-meson
```

## Download and Build
```
# clone source
git clone https://gitlab.com/simulant/mkdcdisc
cd mkdcdisc

meson setup builddir
meson compile -C builddir

# run
./builddir/mkdcdisc -h
```

## Regenerating the embedded assets (maintainers only)

mkdcdisc embeds a few binaries directly into the executable: the MIL-CD and
GD-ROM bootstrap programs (`IP.BIN`) and the default MR license-screen
logo. The source binaries live under `assets/`, and their C-array form is
checked in under `src/assets/*.include` so that a **normal build needs no extra
tooling**.

If you change one of the source binaries in `assets/`, regenerate the includes
with the dedicated Meson target:

```
meson compile -C builddir regen-includes
```

This maps each asset to its include and C symbol:

| Asset | Include | Symbol |
|---|---|---|
| `assets/ip/cd/IP.BIN` | `src/assets/ip_cd.include` | `IP_BIN_MILCD` |
| `assets/ip/gd/nearest/IP.BIN` | `src/assets/ip_gd.hda.nearest.include` | `IP_BIN_GDROM_HDA_NEAREST` |
| `assets/ip/gd/zero/IP.BIN` | `src/assets/ip_gd.hda.zero.include` | `IP_BIN_GDROM_HDA_ZERO` |
| `assets/iplogo/default-mr.mr` | `src/assets/iplogo.default.include` | `IP_BIN_DEFAULT_MR` |

The two GD-ROM bootstraps are the `--gdi-fpu-rounding` variants, and are
expected to differ only in their second-stage bootstrap: they must keep the
same 256-byte header, since the low-density `IP0000.BIN` is built from it. The
e2e suite checks exactly that, so a regenerated pair that drifts apart in the
header fails the tests rather than shipping.

The target is **never run as part of a normal build**, only when you invoke it
explicitly. It relies on `xxd` (from Vim), which is therefore required *only*
for regeneration, not for building mkdcdisc. Commit the regenerated
`src/assets/*.include` files alongside the changed binaries.
