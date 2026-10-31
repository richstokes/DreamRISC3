# libisofs for Windows

This is the `libisofs` library from the `libburnia` project, lightly patched for
building and use on Windows.

This source code comes from the [official `libburnia` repository](https://dev.lovelyhq.com/libburnia/libisofs.git).
More information is available in the [`git_info.json`](git_info.json) file.

If you want to learn more regarding `libisofs`, you can read the official
[`README`](README) file.

**Note:** This version of the library has only been ported to the `mkdcdisc`
project. It has not been fully tested as a whole. It may or may not work fully.

## What is patched

Out of the 65 upstream files under `libisofs/`, **13 have been modified**. No files have been added or removed from the upstream directory itself. 

Alongside the upstream source, we maintain two custom additions to support Windows builds:
* `config.h`: A hand-written replacement for the `configure`-generated header.
* `winpatch/`: A directory providing stub headers (`fnmatch.h`, `pwd.h`, `unistd.h`, etc.) missing from MinGW-w64.

To verify our changes against a pristine upstream copy, run:
```bash
diff -rq --strip-trailing-cr <upstream>/libisofs libisofs/

```

### Portability Changes

The 13 modified files primarily contain **portability fixes** ensuring the library compiles and resolves paths correctly on Windows. These include header guards, `#include` adjustments, wrapping `printf` behind `_DEBUG`, and Windows drive/separator handling.

*Files modified:* `aaip-os-dummy.c`, `aaip_0_2.c`, `builder.c`, `ecma119.c`, `find.c`, `fs_image.c`, `fs_local.c`, `libisofs.h`, `tree.c`, `util.c`, `filters/external.c`, `filters/gzip.c`, `filters/zisofs.c`

### Critical Windows Inode Patch

One file, **`fs_local.c`**, contains a critical patch that alters behavior to prevent data corruption.

**The Bug:** MinGW's `stat()` never populates a real `st_ino`. Because libisofs caches file sources using a `(fs_id, dev_id, ino_id, size)` tuple, retaining the upstream `ISO_LOCAL_FS_ID` causes a severe issue: any two distinct files of the same size on the same drive are silently merged into a single data extent. One file's contents overwrite the other.

**The Fix:** In `fs_local.c`, `lfs_get_id()` is patched to return `WINPATCH_ISO_LOCAL_FS_NO_STAT_ID` (defined as `0` in our custom `config.h`). Returning `0` acts as a sentinel value, explicitly instructing libisofs **not** to trust `st_dev`/`st_ino`. (This value also correctly gates trust for symlinks and special files in `builder.c`).

### Maintenance & Updating

All custom modifications in the vendored source are tagged with the comment `mkdcdisc-patch`. There are two kinds, and the tag says which:

* `mkdcdisc-patch: portability` — the mechanical ones. Fifteen sites across twelve files, nearly all of them a two-line `#ifndef _WIN32` around a header MinGW-w64 does not ship. Losing one breaks the build immediately and loudly.
* `mkdcdisc-patch: for behaviour` — the single critical one, in `fs_local.c`. Losing it breaks nothing visibly. See above.

To list only the critical patch:
```bash
grep -rn "mkdcdisc-patch:" src/third_party/libisofs/libisofs/ | grep -v ": portability"
```

**After every upstream update, you MUST perform these checks:**

1. **Verify the patch remains intact:**
```bash
grep -rn "mkdcdisc-patch" src/third_party/libisofs/libisofs/

```


2. **Run the test suite:**
The test corpus entry `cdi-duplicates-kept` (in `tests/fixtures/iso-manifest.txt`) is specifically designed to catch this regression. It relies on an input tree containing the suite's only pair of same-size, different-content files.

*Losing this patch will not break the build or fail loudly—it will silently corrupt discs.*

## Acknowledgments

This patched version is based on preliminary work by Unknown Shadow (@UnknownShadow200)
for LXDream, [available here](https://gitlab.com/UnknownShadow200/lxdream-nitro/-/tree/win-fixes?ref_type=heads).

However, the approach is slightly different as the changes in the `libisofs`
files have been made minimally to facilitate updates to this dependency in the
future.
