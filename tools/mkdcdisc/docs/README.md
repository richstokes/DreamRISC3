# mkdcdisc documentation

Everything past the quick-start in the main [README](../README.md): what each
option does, what the disc formats are, and how to get a particular kind of
image out of them.

- [CLI reference](cli-reference.md) — every option, and the deprecated names still accepted
- [CD images](cd-images.md) — CDI/MDS+MDF/NRG, disc layouts, and which consoles boot MIL-CD
- [GD-ROM images](gd-image.md) — `.gdi` output and GD-DA tracks
- [Examples](examples.md) — build recipes by goal
- [CDDA and MSINFO](msinfo.md) — audio tracks and the LBA value
- [Sort files](sort-file.md) — controlling file placement

New to mkdcdisc: start with [Examples](examples.md), which is organised by what
you are trying to produce, and reach for the [CLI reference](cli-reference.md)
once you know which options you need.

[specs/](specs/) holds third-party format documentation kept for reference
rather than written here; currently Henrik Stokseth's description of the
Alcohol 120% MDS/MDF format, which the `.mds` writer is built from.
