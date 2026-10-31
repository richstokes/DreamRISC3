# Bundled apps

Each folder here describes one RISC OS application that ships on the disc. Nothing in `build/` is committed: the game binaries are fetched at build time, same as the ROM stays out of the repo.

`tools/mkapps.py` reads every `app.json` and writes `build/rom.bin`, which is the ROM with a small `DreamApps` module added, followed by a ResourceFS pack of the app files. The app folder then appears in the Apps window. `tools/armxlat.py` recompiles the app's ARM code ahead of time into its own overlay unit, `ov_N.bin` on the disc.

Neither part is loaded at boot. Pack pages stream through the ROM page cache when a file is read, and the code unit loads when the app first jumps to its entry point.

## app.json

- `name`: application folder, listed under `Apps`
- `upstream`: files to download, pinned by commit and sha256
- `files`: folder contents, from `local` files here or `upstream` keys, with a filetype or a load and exec address
- `code.file`: the file holding ARM code, which must have `load` and `exec`
- `code.seeds`: extra entry points, as `tools/converge.sh` finds them
- `code.tables`: tables of code pointers, as `[address, count]`
- `code.frame_us`: vertical sync period while the app runs, for games whose speed depends on frame rate. Lander runs at 25Hz. An 8MHz ARM2 managed ~15 frames/s by instruction count, so raise this for a slower, closer game
