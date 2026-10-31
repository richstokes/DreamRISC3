# CDDA Audio Tracks and MSINFO (LBA value)

When your disc has audio tracks in the first session, the data track does not start at a fixed LBA; it depends on the total size of the audio. This starting LBA is called **MSINFO**.

`mkisofs` needs to know the MSINFO **before** building the ISO (via `-C 0,<msinfo>`) so it can place the correct absolute addresses inside the ISO. This is why the [External ISO Mode workflow](cd-images.md) uses `-M` to pre-compute the value; see [docs/examples.md](examples.md) for the complete recipes.

## Computing MSINFO (LBA value)

Use `-M / --print-msinfo` to compute the data-track LBA:

```bash
mkdcdisc -M -c track1.wav -c track2.raw
# Output: 12608
```

If you specify no `-c` tracks, it prints the Audio/Data default (`11702`). With `-L data-data`, it always prints `0`, since a Data/Data image has no audio session.

## Getting it wrong

An ISO records absolute addresses, so `-C 0,<msinfo>` decides them before a
byte of the filesystem is written. Give mkisofs the wrong number and the ISO is
still perfectly well formed — every address inside it simply points somewhere
other than where the file is, and the console reads each file from the wrong
place. Nothing about the resulting image looks wrong.

So mkdcdisc checks it. When you pass an ISO with `-t/--external-data-track`, the
sector the ISO was built for is recovered from the ISO itself and compared with
the sector it is about to be written at, and a mismatch is reported with the
drift between the two:

```
Warning: the external ISO was built to start at sector 0, but it will be written
at sector 11702 (-11702). Every address recorded inside it is that far out, so
the console will read each file from the wrong place. Pass mkisofs -C 0,11702;
mkdcdisc -M prints that number for the options this disc is being built with.
```

That example is the common mistake: a plain ISO built without `-C` at all,
wrapped into the default Audio/Data layout.

It is a warning rather than a refusal, and stays one even with a large drift.
The figure mkdcdisc compares against is a model of where the session lands —
lead-in, lead-out and pregap lengths added up — rather than a measurement of
it, so a build refused on that basis would be a build refused on mkdcdisc's
opinion. A drift within one CD pregap (150 sectors) is not reported at all: that
is the one quantity two mastering models legitimately disagree about, some
counting the sectors between sessions as the tail of the one before and some as
the head of the one after. A first session is held to exactly `0`, with no such
latitude, there being nothing to model about the start of a disc.

See [cli-reference.md](cli-reference.md) for the other boot prerequisite
checks, and for `--disable-bootstrap-checks`.

## Audio Track Formats

Both `.wav` (RIFF PCM, 44100 Hz, stereo, 16-bit) and raw CD-DA (headerless PCM in the same format) are supported. Detection is automatic: if a file has a RIFF header, it's treated as WAV; otherwise, it's assumed to be raw. No file extension checking occurs.

**Some examples:**
- `-c intro.wav`: WAV file (auto-detected)
- `-c audio.raw`: raw CD-DA data (auto-detected)
- `-c sample.da`: raw CD-DA data (auto-detected)

**Raw CD-DA Requirements:**
- 44100 Hz sample rate
- Stereo (2 channels)
- 16-bit samples
- Little-endian PCM
- Exactly 2352 bytes per sector (44100 Hz × 4 bytes/sample / 75 sectors/sec)
- Minimum 300 sectors (~4 seconds)

Files smaller than 300 sectors will trigger a note but are not rejected.

A raw file whose length is not a whole number of sectors is not rejected either. mkdcdisc fills the last sector out with silence (at most 2351 bytes, under 1/75 s) and prints a note saying how many bytes it added.
