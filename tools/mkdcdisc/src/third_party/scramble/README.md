# scramble

The Dreamcast bootstrap scrambler: the transformation a boot binary has to be
stored under on a disc, and its inverse.

This is a C++ rewrite of Marcus Comstedt's `scramble.c`, placed in the public
domain and [available from his site](https://mc.pp.se/dc/files/scramble.c).
KallistiOS carries the same file under
[`utils/scramble/`](https://github.com/KallistiOS/KallistiOS/tree/master/utils/scramble),
imported in 2014 and never modified since, with a man page that documents the
`-d` (descramble) mode; being a mirrored repository rather than a personal site,
that is the copy to consult.

mkdcdisc used to invoke the tool as an external program; it was brought in as a
library in December 2022, so that a build needs nothing but mkdcdisc itself.

## Divergences from the original

Both directions are the original's, and so is the pseudo-random generator that
drives them: `scramble()` transcribes `save_file()`/`save_chunk()` and
`unscramble()` transcribes `load_file()`/`load_chunk()` (reached upstream through
`descramble()`, the tool's `-d` mode). What differs is the shape, not the
algorithm:

- **A library, not a program.** `scramble()` and `unscramble()` take and return
  a `std::vector<char>`; the original worked on `FILE*` streams from its own
  `main()`, and exited the process on any I/O error.
- **The permutation is separated from the I/O.** Upstream fuses the two: the
  slice order is applied *while* reading or writing, which is what makes its two
  directions the same loop over `idx[]` with only `load()`/`save()` swapped.
  Here both are buffer-to-buffer, so that one loop becomes the
  `reorder_slices`/`restore_slices` pair — the scatter/gather difference now
  showing as which side of the `memcpy()` is indexed by `idx[]`.
- **`unscramble()` arrived late.** The 2022 port carried only the scrambling
  direction, the inverse being of no use to a tool that only writes discs. It
  was transposed when `-B` (a boot binary supplied already scrambled) had to be
  accepted for `-F gdi`, which needs a plain `1ST_READ.BIN`.

`tests/unit/test_scramble.cpp` covers the round-trip, starting from either end.

## License

See [COPYRIGHT](COPYRIGHT). In short: the original carries no license text at
all, and the "public domain" wording in `scramble.cpp`'s header is this
project's own characterization rather than a statement by the author.
