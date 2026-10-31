# makeip

Two things a Dreamcast boot header needs, both taken from
[**IP creator** (`makeip`)](https://github.com/KallistiOS/KallistiOS/tree/master/utils/makeip),
the tool that used to be invoked to produce an `IP.BIN` before mkdcdisc built
one itself:

- **`crc.cpp`** — the checksum the header carries in its Device Info field,
  from `crc.c`. Used once, by `update_ipbin_crc()` in `src/ip_boot.cpp`.
- **`mr.cpp`** — PNG-to-MR conversion, from `mr.c`. MR is Sega's palettized,
  run-length-compressed image format, and an MR image is what the license
  screen displays. Reached through `convert_png_to_mr()` in
  `src/mr_image.cpp`, which is what `-i`/`--bootstrap-mr-image` ends up
  calling for a PNG.

**Its license is not this project's** — see [COPYRIGHT](COPYRIGHT) for what was
taken and from whom, and [LICENSE](LICENSE) for the BSD 2-Clause text the code
is under. That notice is also at the top of all four source files, which is
where clause 1 requires it.

`mr.cpp` is compiled only in a build with libpng; `meson.build` adds it inside
the `png_dep.found()` branch, so a build without PNG support simply does not
have it. `crc.cpp` is unconditional.

## Divergences from the original

The algorithms are upstream's, unchanged — `mr_compress()`, `read_png_rgba()`
and the MR header layout transcribe `mr_compress()`, `png_read()` and
`mr_convert_raw()`; `calc_crc()` transcribes its namesake. What differs is the
shape they are offered in:

- **A library, not a program.** Upstream's functions are reached from its own
  `main()` and talk to the user on the way: `mr_convert_raw()` calls
  `log_error()` and `log_notice()`, and its callers `halt()` the process.
  `png_to_mr()` here returns an `MrStatus` instead and says nothing;
  `src/mr_image.cpp` turns that into the message a user reads. This is the one
  rule the directory keeps strictly — nothing here includes a mkdcdisc header,
  which is what makes the differently licensed code a leaf of the build.
- **The caller owns the buffers.** Upstream `malloc()`s an output buffer and
  hands ownership over (`mr_output_t`, freed by `mr_destroy()`); here the
  result lands in a caller-supplied `std::vector`. The `malloc()`s inside
  `read_png_rgba()` are still raw, deliberately: libpng reports errors by
  `longjmp()`ing back to the `setjmp()`, and only trivial-destructor locals may
  be live across that jump.
- **Placement is not decided here.** Upstream's `update_crc()` and
  `mr_inject()` also know *where* the result goes — offsets `0x20` and `0x3820`
  into a 32K buffer. Only the computation was taken; mkdcdisc addresses its own
  `IPBin` struct instead.
- **`0x8000` is written as itself.** Upstream tests the CRC's high bit with
  `n & INITIAL_PROGRAM_SIZE`, a macro that means the 32768-byte size of an
  `IP.BIN` and coincides with the mask wanted. It is spelled `0x8000` here.
- **Everything upstream that mkdcdisc does not need was left behind**: reading
  and dumping `.mr` files, injecting one into a bootstrap, the `ip.txt` field
  parser, and the MR decoder.
- **A `makeip` namespace.** Neither `calc_crc` nor `mr_compress` is a name that
  should be loose in a project this size, and it marks the code as somebody
  else's at every call site.

## What pins it

Neither of these announces a mistake: a wrong checksum is four plausible hex
digits, and a wrong MR is a license screen full of garbage on a disc that
otherwise builds and boots. Both are therefore pinned by tests, against values
that came from upstream rather than from this implementation.

`tests/unit/test_makeip_crc.cpp` holds vectors produced by compiling and running
makeip's own `crc.c`. Two of them also settle *which* CRC-16 this is: the
catalogue check value — the CRC of `"123456789"` — is `0x29B1`, identifying
**CRC-16/IBM-3740** (CCITT-FALSE), and the empty input returns the `0xFFFF` it
starts from. mkdcdisc used to describe it as CRC-16/XMODEM, which is the same
polynomial started from `0x0000` and answers `0x31C3`; the code was right and
the comment was wrong.

`tests/unit/test_makeip_mr.cpp` pins the format and the result separately. A
crafted single-row image holds a run of every length the encoder treats
differently (1, 127, 128, 255, 256, 383 and 384, the last of which has to be
split), which fixes every field of the header, the palette's BGR byte order and
all four encoding branches. Then the bundled logo:
[`assets/iplogo/default-mr.png`](../../../assets/iplogo/default-mr.png)
converted has to equal the committed
[`default-mr.mr`](../../../assets/iplogo/default-mr.mr) beside it, byte for
byte. It is registered only in a build with libpng, for the same reason
`mr.cpp` is compiled only there.

## Updating it

This is not a vendored drop tracking an upstream release, it is code that was
copied in once. Should upstream change something that matters here, the change
has to be transcribed by hand and the notice at the top of each file has to
survive it.
