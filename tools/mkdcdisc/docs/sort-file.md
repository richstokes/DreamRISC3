# Sort file

When using the `-S` flag, you must provide a sort file that specifies the order
in which files and directories are written to the ISO image. This ordering can
influence the performance of data retrieval, where data on the outer edges of
the disc can be read faster.

A GD-ROM (`-F gdi`) has two independent filesystems, so it takes two sort
files: `-S/--sort-file` orders the high-density one, where the game lives, and
`--gdi-lda-sort-file` orders the low-density area (see
[gd-image.md](gd-image.md)). Both use the format below, and each one only sees
its own tree — a path is resolved against the area its sort file belongs to,
never the other.

**Understanding the Sort File:**

- **Structure:** Each line in the sort file corresponds to a file or directory
path, followed by a weight value. The format is:

```
/path/to/file_or_directory weight
```

**Weights:**
- **Default Weight:** All files/directories are assigned a default value of 0.
- **Positive Weights:** Higher positive values place files closer to the center
  of the disc.
- **Negative Weights:** Lower negative values place files nearer to the outer
  edge of the disc for faster read times.
- **Directory Handling:** Assigning a weight to a directory applies that weight
  to all its contents recursively.
- **Comments and Blank Lines:** Lines starting with `#` are treated as comments
  and ignored. Blank lines are also ignored.
- **Paths cannot contain spaces:** the line is split at the first run of
  whitespace, so `/my file.txt 100` reads as the path `/my` followed by the
  weight `file.txt`, which is not a number. Such a line is skipped with a
  warning naming its line number — the build still succeeds, and produces a
  disc ordered differently from what was asked for, so the warning is worth
  reading. A sort file that cannot be opened at all is warned about the same
  way.
- **Paths are those of the disc, not of your source tree.** This is the usual
  way a sort file quietly does nothing. `-D data/` puts the *contents* of
  `data/` at the root of the volume, so a file at `data/logo.png` is
  `/logo.png` on the disc, not `/data/logo.png`; `-d data/` keeps the
  directory, so the same file is `/data/logo.png`. A leading `/` is required
  either way. An entry matching nothing on the finished volume is reported —
  individually when there are a few, as a count when a whole file was written
  against the wrong prefix (`-v 2` then lists them).

**Example Sort File:**

```
# Position the introductory video at the outer edge for immediate playback
/media/videos/intro.mp4 -1000

# Place high-resolution images for quick access
/media/images/high_res/image1.jpg -800
/media/images/high_res/image2.jpg -800

# Default weight files are written before the files below

# Standard resolution images can be placed closer to the center
/media/images/standard_res/image3.jpg 200
/media/images/standard_res/image4.jpg 200
```

## Interaction with `--duplicates-once`

`--duplicates-once` stores byte-identical files once and points every copy at
a single extent. That extent sits at one place on the disc, so the copies
cannot each land where a different weight asks them to.

The weights are not ignored, and the files are not held back from sharing:
the shared extent takes the **highest weight** of the set, which is the rule
`mkisofs` settled on. A file weighted below that is written where the heavier
copy asked for, and the build warns and names it, something `mkisofs` does
silently.

For instance, if `/A.TXT`, `/B.TXT` and `/C.TXT` all hold the same contents
and the sort file gives `/A.TXT` a weight of 100 and `/C.TXT` a weight of
9000, all three share one extent, that extent is placed at 9000, and `/A.TXT`
is named in a warning because 100 is not what decided its position.

Unweighted copies are never warned about: nothing was asked for them.
