# libedc

The EDC/ECC encoder every raw 2352-byte Mode 2 Form 1 sector goes through: the
CRC-32 error detection code and the Reed-Solomon P/Q parity that a real disc
carries and that a raw image therefore has to reproduce. Used by all four image
writers in `src/disc_image/formats/` (`cdi.c`, `gdi.c`, `mds.c`, `nrg.c`).

This is the `libedc` of **schilytools**, the successor to cdrtools. The source
comes from the [official schilytools repository](https://codeberg.org/schilytools/schilytools);
more information is in [git_info.json](git_info.json).

**Its license is not this project's** — see [COPYRIGHT](COPYRIGHT), which says
which files here are schilytools' and which are mkdcdisc's own, and
[CDDL.Schily.txt](CDDL.Schily.txt) for the license the former are under.

If you want to learn more regarding `libedc` itself — the sector formatting
stages it implements, and the encoding routines it offers — you can read the
official [README](README) file.

## The vendored files are untouched

Every file taken from upstream is byte-identical to it:

| File | Version |
|---|---|
| `edc_ecc.c` | 1.31 13/04/28 |
| `ecc.h` | 1.6 12/12/02 |
| `edc.h` | 1.3 12/12/02 |
| `crctable.out` | 1.2 02/10/17 |
| `encoder_tables` | 1.4 02/10/19 |
| `l2sq_table` | 1.2 02/10/19 |
| `scramble_table` | 1.2 02/10/19 |
| `README` | — (carries no version line) |

That is deliberate, and it is what the rest of this directory is for. The
previous vendored copy (libedc 1.22, carried from img4dc since 2022) had been
edited in place to build outside the cdrtools tree, which meant every local fix
was tangled up with upstream's code and an update meant re-doing them by hand.
Updating is now a matter of copying eight files over.

The four table files are `#include`d into `edc_ecc.c` rather than compiled
separately, which is why they carry no extension and are absent from the source
list in `meson.build`.

## What replaced the patches: `schily/`

`edc_ecc.c` reaches for its own build environment as `<schily/mconfig.h>`,
`<schily/utypes.h>`, `<schily/align.h>` and three headers that are just libc.
Upstream those come from the schilytools tree, which mkdcdisc does not have; the
[schily/](schily/) directory here answers them with the little they actually
need — five names in total:

- `__PR()`, the prototype guard every declaration in `ecc.h` and `edc.h` is
  wrapped in
- `Uchar`, `UInt32_t`, `UConst`
- `xaligned()`, an alignment test used once, to choose between scrambling a
  sector byte-wise and 32 bits at a time

and one configuration answer, `HAVE_MEMMOVE`. Saying memmove() exists — it does,
everywhere mkdcdisc builds — cuts out upstream's fallback that would otherwise
route it through `bcopy()` and `<schily/libport.h>`. That fallback is why the
old copy carried an `edc_bcopy()` of its own, in `patch.c`; both are gone.

The include path that makes `<schily/...>` resolve is scoped to this library's
own build target, so nothing else in mkdcdisc can reach these headers. Consumers
come through `libedc.h`, which pulls the same shims in by quoted path and so
needs no include path at all.

Two warnings are also silenced on this target, in `meson.build` rather than by
editing the sources: `-Wno-array-parameter` for upstream's own disagreement
about `do_encode_L2()`'s first parameter (`unsigned char inout[2352]` in
`edc_ecc.c`, `unsigned char *inout` in `ecc.h` — compatible in C, since the
array decays), and `-Wno-unused-const-variable` for the `sccsid[]` version
string upstream embeds for `what`/`ident` to find.

## What is actually reached

Two names, both from `ecc.h`: `do_encode_L2()` and the sector type to hand it —
`MODE_2_FORM_1` for the MIL-CD writers (`cdi.c`, `mds.c`, `nrg.c`), `MODE_1` for
the GD-ROM one (`gdi.c`). `libedc.h` is the one header the rest of mkdcdisc
includes, and it exists only to set the shims up before handing `ecc.h` over:
nothing is wrapped or renamed on the way.

There used to be a `libedc.c` here, offering an `edc_encode_sector()` that
wrapped `do_encode_L2()` with `MODE_2_FORM_1` baked in. Nothing ever called it —
and nothing could have called it from `gdi.c`, which needs Mode 1 — so it was
dropped. Sector assembly belongs to the writers in `src/disc_image/`, not here.

`edc.h` is vendored for completeness but contributes nothing to this build: it
is entirely behind `EDC_LAYER1`/`EDC_LAYER2`/`EDC_ENCODER`/`EDC_DECODER`/
`EDC_SUBCHANNEL`, none of which are defined. Upstream's decoder
(`edc_ecc_dec.c`) is not vendored at all — mkdcdisc only ever writes discs.

## Updating it

1. Copy the eight files in the table above from a schilytools checkout, and
   refresh [git_info.json](git_info.json).
2. Build. If `edc_ecc.c` asks for a `<schily/...>` header that
   [schily/](schily/) does not have, add it there — never edit the vendored file.
3. Run `meson test`. **`tests/unit/test_edc.c` pins the encoder's output**: it
   feeds fixed sectors through `do_encode_L2()` and checksums the result. If it
   fails, the update changed what gets written to a disc, and that needs
   explaining before it is accepted.

Do not try to verify an update by comparing whole disc images built before and
after: libisofs stamps the ISO9660 volume descriptors from the wall clock, so
two runs a second apart already differ and the comparison says nothing. That is
what `test_edc.c` is there to avoid.
