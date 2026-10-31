/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* GD-ROM building: see build_gd.hpp. */

#include <climits>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

#include "audio.hpp"
#include "boot_checks.hpp"
#include "build_gd.hpp"
#include "cli.hpp"
#include "disc_layout.hpp"
#include "duplicates.hpp"
#include "gd_track_plan.hpp"
#include "input_files.hpp"
#include "interrupt.hpp"
#include "ip_boot.hpp"
#include "iso_builder.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "progress.hpp"
#include "sort_file.hpp"
#include "terminal.hpp"

/* Inert files build_gd() drops into the data directory to shape the
 * high-density layout. "0.0" is the padding convention build_cd() already
 * uses; "0.1" reserves the address range the GD-DA tracks occupy. */
static const char* const GDI_PADDING_FILE = "0.0";
static const char* const GDI_SPACER_FILE = "0.1";

/* The other half of the GD-DA split reservation: the spacer's counterpart on
 * the far side, keeping the closing data track above the minimum track length
 * the way GDI_PADDING_FILE keeps track 3 above it. */
static const char* const GDI_BACK_PADDING_FILE = "0.2";

/* Lowest start LBA among the files of a freshly built high-density ISO.
 * Directory record extents are absolute, so the answer is a disc LBA; `iso`
 * itself begins at CD_GD_START_LBA. Returns 0 if the image can't be walked.
 *
 * This is how build_gd() locates the spacer, which cannot be looked up
 * directly: the filler files are hidden from every tree, so they have no
 * directory record at all, only their content occupies sectors. They are
 * ranked ahead of the real content, so the first file that does have a record
 * starts exactly where the spacer ends. */
static uint32_t find_first_hda_file_lba(const std::vector<uint8_t>& iso) {
    auto sector = [&](uint32_t lba) -> const uint8_t* {
        if(lba < CD_GD_START_LBA) {
            return nullptr;
        }
        size_t offset = (size_t) (lba - CD_GD_START_LBA) * 2048;
        return offset + 2048 <= iso.size() ? &iso[offset] : nullptr;
    };
    auto le32 = [](const uint8_t* p) -> uint32_t {
        return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
    };

    const uint8_t* pvd = sector(CD_GD_START_LBA + 16);
    if(!pvd || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        return 0;
    }

    std::vector<std::pair<uint32_t, uint32_t>> pending;  /* extent, length */
    std::set<uint32_t> seen;
    pending.push_back({le32(pvd + 156 + 2), le32(pvd + 156 + 10)});

    uint32_t first = 0;

    while(!pending.empty()) {
        auto [dir_lba, dir_len] = pending.back();
        pending.pop_back();

        if(dir_len == 0 || !seen.insert(dir_lba).second) {
            continue;
        }

        for(uint32_t s = 0; (size_t) s * 2048 < dir_len; ++s) {
            const uint8_t* dir = sector(dir_lba + s);
            if(!dir) {
                break;
            }

            for(size_t off = 0; off + 33 < 2048;) {
                unsigned rec_len = dir[off];
                if(rec_len == 0) {
                    break;
                }

                unsigned name_len = dir[off + 32];
                if(off + 33 + name_len > 2048) {
                    break;
                }

                uint32_t extent = le32(dir + off + 2);
                uint32_t length = le32(dir + off + 10);
                bool is_dir = (dir[off + 25] & 2) != 0;
                bool is_dot = (name_len == 1 && (dir[off + 33] == 0 || dir[off + 33] == 1));

                if(!is_dot && name_len > 0) {
                    if(is_dir) {
                        pending.push_back({extent, length});
                    } else if(length > 0 && (first == 0 || extent < first)) {
                        first = extent;
                    }
                }

                off += rec_len;
            }
        }
    }

    return first;
}

/* Lays out a complete ISO9660 image from input_dir for the GD-ROM high-density
 * track, and hands back the source that emits it. Called twice by build_gd():
 * once (no padding file present yet) to measure the unpadded content's size,
 * and once more (after the padding file, if any, has been written and the TOC
 * finalized into ip_bin) to produce the real track. Pins the IP.BIN's declared
 * boot file to the end of the filesystem, the placement real GD-ROM hardware
 * expects; build_gd() then sizes the filesystem so that end falls 150 sectors
 * short of the track's end, never inside its run-out.
 *
 * The layout is complete by the time this returns -- which is what lets the
 * measuring pass ask for its size and stop there -- but none of the disc
 * contents have been read yet. The caller owns the source and must hand it to
 * read_burn_source() or measure_burn_source(). */
static struct burn_source* create_hda_burn_source(const std::filesystem::path& input_dir,
                                                  const IPBin& ip_bin) {
    IsoImageRef image = new_iso_image(ip_bin, volume_label());
    IsoImage* iso = image.get();

    IsoDir* root = iso_image_get_root(iso);
    iso_tree_add_dir_rec(iso, root, PATH_TO_CSTR(input_dir));

    /* libisofs ranks sort weights by subtracting them (filesrc.c:
     * "cmp = g->sort_weight - f->sort_weight"), so a pair as far apart as
     * INT_MAX and INT_MIN overflows and silently inverts the very ordering it
     * was meant to force. These two are far enough apart to beat any realistic
     * -S/--sort-file weight, and near enough to zero that the subtraction
     * stays in range. */
    const int SORT_WEIGHT_FIRST = 1 << 24;
    const int SORT_WEIGHT_SPACER = 1 << 23;
    const int SORT_WEIGHT_BACK_PADDING = 1 << 22;
    const int SORT_WEIGHT_LAST = -(1 << 24);

    /* Hide the padding file (if this is pass 2 and one was written) from
     * directory listings, same treatment build_cd() gives its own "0.0".
     * Weighting it to the front makes it fill the bottom of the zone and push
     * the real content outwards, which is where the reference dumps have it. */
    IsoNode* padding_node = NULL;
    if(iso_dir_get_node(root, GDI_PADDING_FILE, &padding_node) == 1) {
        iso_node_set_sort_weight(padding_node, SORT_WEIGHT_FIRST);
        iso_node_set_hidden(padding_node, LIBISO_HIDE_ON_RR | LIBISO_HIDE_BUT_WRITE);
    }

    /* The GD-DA spacer, present only when there are audio tracks. It reserves
     * the address range they occupy, which no track file covers: libisofs
     * allocates contiguously and cannot leave the hole a pressed disc has
     * there, so one file has to span it. Ranked between the padding and the
     * real content, it lands exactly where that hole belongs, just before
     * the closing data track. Its bytes inside the hole are never written. */
    IsoNode* spacer_node = NULL;
    if(iso_dir_get_node(root, GDI_SPACER_FILE, &spacer_node) == 1) {
        iso_node_set_sort_weight(spacer_node, SORT_WEIGHT_SPACER);
        iso_node_set_hidden(spacer_node, LIBISO_HIDE_ON_RR | LIBISO_HIDE_BUT_WRITE);
    }

    /* Immediately behind the spacer, so it lands at the head of the closing
     * data track and gives that track the length it needs before any real file
     * is placed. Below the spacer's weight and above the default, which is
     * exactly the slot between the two. */
    IsoNode* back_padding_node = NULL;
    if(iso_dir_get_node(root, GDI_BACK_PADDING_FILE, &back_padding_node) == 1) {
        iso_node_set_sort_weight(back_padding_node, SORT_WEIGHT_BACK_PADDING);
        iso_node_set_hidden(back_padding_node, LIBISO_HIDE_ON_RR | LIBISO_HIDE_BUT_WRITE);
    }

    /* Weights are handed to --duplicates-once rather than used to exclude:
     * a shared extent has one position, so the set takes the highest weight
     * among it, and any file placed below that is named in a warning. */
    const std::map<IsoNode*, int> weights = apply_sort_file("sort-file", root);

    /* The boot binary must land at the very end of the filesystem, which is
     * where every reference dump has it (18 Wheeler and Namco both end theirs
     * at LBA 549000). libisofs sorts high weights towards LOW block addresses
     * ("the greater this value is, the closer from the beginning of image
     * the file will be written") so pinning a file last means giving it the
     * lowest weight, not the highest.
     *
     * Which file that is comes from boot_filename() rather than a hardcoded
     * name: a -p/--bootstrap can declare something other than 1ST_READ.BIN, and
     * the console will look for whatever the header says. Set after
     * -S/--sort-file processing so it always wins over a user weight.
     *
     * Asked of boot_filename() rather than read back out of the header field,
     * even though the two answer the same question, because the lookup below is
     * against the tree libisofs holds in memory -- whose nodes carry the name
     * the file was staged under. The header field is upper-cased to match the
     * ECMA-119 records libisofs will write, which happens later and does not
     * rename the node; looking up that upper-cased form would miss a boot
     * binary staged under any other case. boot_filename() is the staged name by
     * definition: it is what gather_files() wrote the file as. */
    const std::string boot_file = boot_filename();

    IsoNode* boot_node = NULL;
    if(!boot_file.empty()
        && iso_dir_get_node(root, boot_file.c_str(), &boot_node) == 1) {
        iso_node_set_sort_weight(boot_node, SORT_WEIGHT_LAST);
    } else if(logging::enabled(logging::Level::Normal)) {
        std::cerr << term::warning << "the IP.BIN boot file \"" << boot_file
                  << "\" is not present in the data track, so it cannot be placed at the end "
                     "of the high-density area where GD-ROM hardware expects it. The disc is "
                     "unlikely to boot." << std::endl;
    }

    /* Files --duplicates-once must leave alone, as node pointers rather than
     * names: the boot binary's name comes from -p/--bootstrap and is not
     * knowable in advance, and the three fillers are looked up above anyway.
     *
     * The fillers matter more here than the boot binary does. All three are
     * written from zeroes, so any two of the same length are byte-identical
     * by construction -- a certain match, not an unlikely one -- and every one
     * of them exists purely to occupy an address range. Sharing one away
     * would collapse the very reservation it stands for: the GD-DA spacer
     * marks out the hole the audio tracks live in, and the two padding files
     * set where the closing data track begins and ends.
     *
     * The boot binary is excluded for a plainer reason: it is pinned to the
     * very end of the filesystem just above, and an extent has one placement. */
    std::set<IsoNode*> undeduplicable;
    for(IsoNode* node: {padding_node, spacer_node, back_padding_node, boot_node}) {
        if(node) {
            undeduplicable.insert(node);
        }
    }

    apply_duplicates_once("high-density area", iso, root, undeduplicable, weights);

    IsoWriteOpts* opts = create_iso_write_opts(ip_bin, CD_GD_START_LBA, /* joliet */ false);

    struct burn_source* burn_src;
    iso_image_create_burn_source(iso, opts, &burn_src);

    /* Freed here rather than after the image has been read: libisofs clones
     * the options into the layout it just built (ecma119.c, via
     * iso_write_opts_clone()), so this copy has no reader left. */
    iso_write_opts_free(opts);

    return burn_src;
}

/* `track_bytes` is the length of the track the image is destined for, which
 * build_gd() pads it out to further down. Reserved here rather than left to
 * that resize: libisofs stops a few hundred kilobytes short of the track (the
 * run-out it cannot know about), and growing a gigabyte-long vector by that
 * much costs a full reallocation, with both copies live across it. */
static std::vector<uint8_t> build_hda_iso(const std::filesystem::path& input_dir,
                                           const IPBin& ip_bin,
                                           const progress_t* progress,
                                           uint64_t track_bytes) {
    return read_burn_source(create_hda_burn_source(input_dir, ip_bin), progress, track_bytes);
}

/* Adds whatever --gdi-lda-directory / --gdi-lda-directory-contents /
 * --gdi-lda-file ask for to the low-density root, mirroring -d/-D/-f:
 * --gdi-lda-directory brings the directory across as a subdirectory of the
 * volume root, --gdi-lda-directory-contents brings only what is inside it.
 *
 * Unlike the high-density track, nothing is staged through the temp
 * directory: these go straight into the image, so they cannot collide with
 * the disc contents gather_files() assembles.
 *
 * The directory's own name comes from filename() rather than -d's stem(),
 * which would drop everything after a dot in a directory name.
 *
 * Paths are handed over absolute and with POSIX separators. libisofs derives
 * a node's leaf name by splitting the path on '/', so a Windows path reaches
 * it whole: "./extras/IP0000.BIN" lands on the disc as "___EXTRAS_IP0000.BIN", and
 * an absolute one as "C__DREAMSDK_HOME_...BIN". generic_string() gives the
 * forward slashes it expects, which the Windows APIs accept just as well. */
static void add_lda_contents(IsoImage* iso, IsoDir* root) {
    if(OPTS.count("gdi-lda-directory")) {
        for(const std::filesystem::path given: OPTS["gdi-lda-directory"]) {
            std::filesystem::path dir = std::filesystem::absolute(given);
            std::string dir_path = dir.generic_string();
            std::string name = dir.filename().string();
            if(name.empty()) {
                /* Trailing separator: "foo/bar/" leaves filename() empty. */
                name = dir.parent_path().filename().string();
            }

            IsoDir* node = NULL;
            if(iso_tree_add_new_dir(root, name.c_str(), &node) < 0) {
                std::cerr << term::warning << "could not add directory " << name
                          << " to the low-density area." << std::endl;
                continue;
            }

            iso_tree_add_dir_rec(iso, node, dir_path.c_str());
        }
    }

    if(OPTS.count("gdi-lda-directory-contents")) {
        for(const std::filesystem::path given: OPTS["gdi-lda-directory-contents"]) {
            std::string dir_path = std::filesystem::absolute(given).generic_string();
            iso_tree_add_dir_rec(iso, root, dir_path.c_str());
        }
    }

    if(OPTS.count("gdi-lda-file")) {
        for(const std::filesystem::path given: OPTS["gdi-lda-file"]) {
            std::string file_path = std::filesystem::absolute(given).generic_string();
            if(iso_tree_add_node(iso, root, file_path.c_str(), NULL) < 0) {
                std::cerr << term::warning << "could not add file " << file_path
                          << " to the low-density area." << std::endl;
            }
        }
    }
}

/* Builds the low-density area's ISO9660 filesystem: the low-density boot
 * image as its 32K system area, followed by the three PVD-referenced text
 * files. Built at ms_block 0, since the LDA starts at LBA 0.
 *
 * Real discs put these three files here and nowhere else (the high-density
 * PVD leaves the same fields empty on every dump checked) which is why this
 * is separate from build_hda_iso() rather than a flag on it. There is no boot
 * binary to pin and no padding file to weight, so the two share only the
 * identifier setup in new_iso_image(). */
static std::vector<uint8_t> build_lda_iso(const IPBin& lda_ip_bin, const progress_t* progress) {
    IsoImageRef image = new_iso_image(lda_ip_bin, volume_label());
    IsoImage* iso = image.get();

    /* User content first, so that a COPYRIGH.TXT of their own is detected as
     * already present and wins over the generated one. */
    IsoDir* root = iso_image_get_root(iso);
    add_lda_contents(iso, root);

    VolumeTextFiles text_files = add_volume_text_files(root, lda_ip_bin, /* generate_defaults */ true);

    /* All three, unless a --gdi-disable-* flag kept one off the volume: an
     * identifier naming a file that isn't there would be a dangling reference,
     * and patch_pvd_file_ids() only rewrites the fields set here. */
    if(text_files.copyright) iso_image_set_copyright_file_id(iso, PVD_COPYRIGHT_FILE);
    if(text_files.abstract) iso_image_set_abstract_file_id(iso, PVD_ABSTRACT_FILE);
    if(text_files.biblio) iso_image_set_biblio_file_id(iso, PVD_BIBLIO_FILE);

    /* Applied last, so it can weight the three generated text files too. The
     * low-density area has its own sort file: it is a separate filesystem
     * whose paths have nothing to do with the high-density one's, so sharing
     * -S/--sort-file between them would mean one list of paths addressing two
     * unrelated trees. */
    apply_sort_file("gdi-lda-sort-file", root);

    IsoWriteOpts* opts = create_iso_write_opts(lda_ip_bin, 0, /* joliet */ false);

    struct burn_source* burn_src;
    iso_image_create_burn_source(iso, opts, &burn_src);
    std::vector<uint8_t> data = read_burn_source(burn_src, progress);
    iso_write_opts_free(opts);

    patch_pvd_file_ids(data, text_files);

    return data;
}

/* Writes the high-density session's TOC into ip_bin's system area, byte-for-
 * byte matching a real Sega GD-ROM's IP.BIN:
 *
 *   0x100: "TOC1"            -- ASCII magic (validated by GD-ROM Explorer)
 *   0x104: uint32 entry[97]  -- one per high-density track (tracks 3..99);
 *                               the low-density tracks 1 and 2 do NOT appear
 *                               here. entry[0] is the first HD track. Unused
 *                               entries are 0xFFFFFFFF.
 *   0x288: uint32 first      -- (CTRL<<28)|(ADR<<24)|(first_HD_track<<16)
 *   0x28C: uint32 last       -- likewise for the last HD track
 *   0x290: uint32 leadout    -- (CTRL<<28)|(ADR<<24)|leadout_FAD
 *
 * Each track entry packs FAD (= LBA + 150) in bits 0-23, ADR in bits 24-27,
 * CTRL in bits 28-31 (0 = audio, 4 = data), stored little-endian. mkdcdisc
 * builds from a homebrew (MIL-CD) template whose TOC area is blank, so it
 * writes the whole structure; magic and first/last/leadout included, not
 * just the entries.
 *
 * `hda_tracks` lists the high-density tracks in order, starting at track
 * number `first_hda_track`: one entry for a data-only disc, or the data track,
 * the GD-DA tracks and the closing data track when there is audio. */
struct GdiTocTrack {
    uint32_t lba;
    bool is_data;
};

static void write_gdi_hda_toc(IPBin* ip_bin, uint32_t first_hda_track,
                             const std::vector<GdiTocTrack>& hda_tracks,
                             uint32_t leadout_lba) {
    const uint32_t ADR = 1;
    const uint32_t CTRL_DATA = 4;
    const uint32_t CTRL_AUDIO = 0;
    const size_t TOC_ENTRIES = 97;

    auto make_entry = [&](uint32_t ctrl, uint32_t lba) -> uint32_t {
        return (ctrl << 28) | (ADR << 24) | ((lba + 150) & 0x00FFFFFFu);
    };
    auto make_point = [&](uint32_t ctrl, uint32_t track) -> uint32_t {
        return (ctrl << 28) | (ADR << 24) | ((track & 0xFFu) << 16);
    };

    uint32_t entry[TOC_ENTRIES];
    for(auto& e: entry) {
        e = 0xFFFFFFFF;
    }

    for(size_t i = 0; i < hda_tracks.size() && i < TOC_ENTRIES; ++i) {
        entry[i] = make_entry(hda_tracks[i].is_data ? CTRL_DATA : CTRL_AUDIO, hda_tracks[i].lba);
    }

    /* first/last carry the CTRL of the track they point at, which is always a
     * data track: the high-density area opens on one and, on every reference
     * dump carrying GD-DA, closes on one too. */
    uint32_t last_hda_track = first_hda_track + (uint32_t) hda_tracks.size() - 1;
    uint32_t first = make_point(CTRL_DATA, first_hda_track);
    uint32_t last = make_point(CTRL_DATA, last_hda_track);
    uint32_t leadout = make_entry(CTRL_DATA, leadout_lba);

    /* ip_bin->toc is at system-area offset 0x100 (meta is exactly 256 bytes). */
    uint8_t* toc = (uint8_t*) ip_bin->toc;
    std::memcpy(toc, "TOC1", 4);
    std::memcpy(toc + 4, entry, sizeof(entry));
    std::memcpy(toc + 4 + sizeof(entry), &first, sizeof(first));
    std::memcpy(toc + 4 + sizeof(entry) + 4, &last, sizeof(last));
    std::memcpy(toc + 4 + sizeof(entry) + 8, &leadout, sizeof(leadout));
}

/* Builds a Sega GD-ROM (.gdi) image: a low-density zone (a small dummy data
 * track + a short blank CDDA track, present so the .gdi is directly usable
 * the way a real dump's is) followed by the high-density data track holding
 * the real ISO9660 filesystem, built at ms_block = CD_GD_START_LBA the same
 * way build_cd() builds its data session at the Audio/Data offset.
 *
 * The ISO is always built on 2048-byte logical sectors; --gdi-sector-size
 * only decides how those sectors are framed when the track file is written,
 * so it doesn't affect any of the sizing done here. */
bool build_gd(const std::filesystem::path& input_dir) {
    std::string output_path = OPTS["output"][0];

    /* The disc-format constants and the track sizing that reads them live in
     * gd_track_plan.hpp, where a unit test can reach them. The zone's end is
     * not among them here: hda_end_lba below derives it from the track that
     * plan_track() sized, so a truncated build gets its own end rather than
     * the physical one. */
    using gd_track::TRACK_END_RESERVED_SECTORS;

    /* The high-density boot image (start of the high-density data track):
     * the GD-ROM HD template filled from the metadata options, or a
     * user-supplied -p/--bootstrap used verbatim. The bundled template already
     * carries "GD-ROM1/1" in device_info; a custom -p is trusted as-is. */
    IPBin ip_bin;
    prepare_ip_bin(&ip_bin, input_dir, gd_hda_template(), gd_hda_template_len());

    /* The low-density boot image (IP0000.BIN, start of the low-density data
     * track): the header above, repeated over sixteen otherwise empty sectors.
     *
     * It is derived from the high-density image rather than carried as a
     * template of its own because the header is all the two share. Comparing
     * the reference dumps' two zones region by region, the low-density image
     * carries neither Sega logo nor the TOC, while the high-density one
     * carries those plus the licence screen and the trademark; the 256-byte
     * header is byte-identical between the zones of a given disc.
     *
     * That is also why a custom -p/--bootstrap contributes its header and
     * nothing else: taking its bytes wholesale would put the licence screen in
     * the low-density area, which no pressed disc has, and which is what makes
     * GD-ROM Explorer label that area IP.BIN instead of IP0000.BIN, since it
     * keys the name off the presence of that logo.
     *
     * Pressed discs do put their own second-stage bootstrap in the rest of the
     * sector. We leave it zeroed: the console never runs it -- it boots from
     * the high-density zone -- and the low-density track exists here to give
     * the disc the structure a GD-ROM is expected to have. */
    IPBin lda_ip_bin{};
    std::memcpy(&lda_ip_bin.meta, &ip_bin.meta, sizeof(lda_ip_bin.meta));
    replicate_lda_header(&lda_ip_bin);

    const size_t MIN_AUDIO_SECTORS = 300; /* Red Book: 4 seconds */

    /* The GD-DA tracks, loaded up front because the whole high-density layout
     * is sized around how much room they need. */
    std::vector<std::vector<uint8_t>> gdda_tracks;
    size_t gdda_sectors = 0;

    if(OPTS.count("cdda-track")) {
        for(const auto& filename: OPTS["cdda-track"]) {
            auto data_maybe = load_audio_track(filename);
            if(!data_maybe) {
                return false;
            }

            size_t sectors = (data_maybe->size() + 2351) / 2352;
            if(sectors < MIN_AUDIO_SECTORS) {
                std::cerr << term::error << "audio file " << filename << " is too short - it must be at least "
                          << (MIN_AUDIO_SECTORS * 2352) << " bytes (4 seconds)" << std::endl;
                return false;
            }

            gdda_sectors += sectors;
            gdda_tracks.push_back(std::move(*data_maybe));

            logging::note() << "Added GD-DA track " << gdda_tracks.size()
                            << " (" << sectors << " sectors) from " << filename;
        }
    }

    /* The low-density CDDA track (i.e., track 2). Loaded here with the GD-DA tracks
     * so an unusable file is reported before any building starts, even though
     * (unlike them) it sits below the high-density zone and so has no say
     * in how that zone is laid out. */
    std::vector<uint8_t> lda_cdda;
    if(OPTS.count("gdi-lda-cdda-track")) {
        const std::string& filename = OPTS["gdi-lda-cdda-track"][0];
        auto data_maybe = load_audio_track(filename);
        if(!data_maybe) {
            return false;
        }

        if((data_maybe->size() + 2351) / 2352 < MIN_AUDIO_SECTORS) {
            std::cerr << term::error << "audio file " << filename << " is too short - it must be at least "
                      << (MIN_AUDIO_SECTORS * 2352) << " bytes (4 seconds)" << std::endl;
            return false;
        }

        lda_cdda = std::move(*data_maybe);
    }

    /* The address range the audio occupies, which no track file covers: the
     * closing run-out of the data track before it, a 150-sector pregap either
     * side of the audio, and the audio itself. The spacer file reserves it
     * inside the filesystem, since libisofs cannot leave a hole. */
    const size_t gdda_spacer_sectors = gdda_tracks.empty()
        ? 0
        : TRACK_END_RESERVED_SECTORS + 150 + gdda_sectors + 150;

    iso_init();

    /* Pass 1: measure the unpadded content's size. The system area is a
     * fixed 32K regardless of what's in ip_bin, so the still-blank TOC here
     * doesn't affect the measurement.
     *
     * Only the size is wanted, and libisofs knows it as soon as the layout
     * exists, so the image this pass describes is never actually built: no
     * disc content is read and no gigabyte-scale buffer is allocated for a
     * figure that then gets divided by 2048 and thrown away. Pass 2 below
     * builds the only image that is kept. */
    ProgressLine measuring("Measuring high-density area...");
    uint64_t content_bytes = measure_burn_source(create_hda_burn_source(input_dir, ip_bin));

    if(interrupted()) {
        return false;
    }

    measuring.finish();

    size_t content_sectors = (size_t) ((content_bytes + 2047) / 2048);

    const bool truncate = OPTS.count("gdi-truncate");
    gd_track::Plan plan = gd_track::plan_track(content_sectors, gdda_spacer_sectors, truncate);

    if(!plan.fits) {
        std::cerr << term::error << "data track (" << content_sectors << " sectors"
                  << (gdda_spacer_sectors ? " plus " + std::to_string(gdda_spacer_sectors)
                                            + " reserved for the GD-DA tracks" : "")
                  << ") doesn't fit in the GD-ROM high-density zone ("
                  << gd_track::MAX_CONTENT_SECTORS << " usable sectors, the last "
                  << TRACK_END_RESERVED_SECTORS << " being reserved). "
                     "Reduce the disc contents, or pass --gdi-truncate to build a smaller, "
                     "non-standard image." << std::endl;
        return false;
    }

    /* A truncated build is sized to whatever it is given and always "fits", so
     * the overflow the branch above rejects goes out silently here instead: a
     * large enough disc root produces a track running past the end of the
     * physical zone, and a TOC whose lead-out is beyond the last sector a
     * GD-ROM has. Nothing in mkdcdisc can say whether the reader will mind --
     * that is the flag's whole nature -- but it can say by how much. What the
     * overflow means is left to docs/gd-image.md, which has the room for it.
     *
     * Not covered by a test on purpose: the smallest image that trips it is a
     * gigabyte, which is exactly the cost gd_track_plan.hpp was split out to
     * avoid paying in the suite. */
    if(truncate && plan.track_sectors > gd_track::FULL_TRACK_SECTORS
        && logging::enabled(logging::Level::Normal)) {
        std::cerr << term::warning << "high-density track ends "
                  << (plan.track_sectors - gd_track::FULL_TRACK_SECTORS)
                  << " sectors past LBA " << gd_track::GD_END_LBA
                  << ", where a GD-ROM ends." << std::endl;
    }

    const size_t hda_track_sectors = plan.track_sectors;
    const size_t content_target_sectors = plan.content_target_sectors;

    /* On a GD-DA disc the zone is cut in three and each data track has to clear
     * the minimum track length by itself; see MIN_SPLIT_SIDE_SECTORS. One
     * filler goes ahead of the spacer and one behind it, so track 3 and the
     * closing track each start with enough filesystem to be long enough. Zero
     * without audio, where the zone is a single track that plan_track() has
     * already floored. */
    const size_t split_side_sectors =
        gdda_spacer_sectors > 0 ? gd_track::MIN_SPLIT_SIDE_SECTORS : 0;

    const size_t occupied_sectors =
        content_sectors + gdda_spacer_sectors + 2 * split_side_sectors;

    auto write_filler = [&](const char* name, size_t sectors, const char* label) {
        ProgressLine filling(label);
        write_zero_file(std::filesystem::path(input_dir) / name,
            (std::uintmax_t) sectors * 2048, filling.progress());

        if(interrupted()) {
            return false;
        }

        filling.finish();
        return true;
    };

    /* Pad the filesystem out to content_target_sectors. build_hda_iso() weights
     * the padding to the front and the boot binary to the back, so the binary
     * lands at the end of the content area, which the reserved sectors then
     * separate from the end of the track itself. */
    size_t pad_sectors = content_target_sectors - occupied_sectors + split_side_sectors;
    if(pad_sectors > 0
        && !write_filler(GDI_PADDING_FILE, pad_sectors, "Padding high-density area...")) {
        return false;
    }

    /* The spacer, ranked between the padding and the real content, so the
     * range it reserves lands just before the closing data track. */
    if(gdda_spacer_sectors > 0
        && !write_filler(GDI_SPACER_FILE, gdda_spacer_sectors, "Reserving GD-DA area...")) {
        return false;
    }

    /* And the closing track's own head start, on the far side of the spacer. */
    if(split_side_sectors > 0
        && !write_filler(GDI_BACK_PADDING_FILE, split_side_sectors,
                         "Reserving the closing track...")) {
        return false;
    }

    /* Low-density zone tracks: a data track carrying the IP0000.BIN boot
     * image and the low-density filesystem (so a real Dreamcast can
     * identify the disc and hand off to the high-density area), and a short
     * blank CDDA track. A complete, standard 3-track .gdi (what
     * Flycast/Redream/GDEmu expect) needs both present. */
    ProgressLine building_lda("Generating low-density area...");
    std::vector<uint8_t> lda_iso = build_lda_iso(lda_ip_bin, building_lda.progress());

    if(interrupted()) {
        return false;
    }

    building_lda.finish();

    size_t lda_content_sectors = (lda_iso.size() + 2047) / 2048;

    /* Track 1 is sized the way real discs size it: the content plus the same
     * 150-sector run-out every data track ends with, floored at 450 sectors.
     * Both regimes show up in the dumps; the discs whose LDA holds real
     * content sit exactly 150 sectors above it, while those carrying only the 
     * 3 text files are padded out to a fixed floor, 450 being the most common. */
    const size_t LDA_MIN_SECTORS = 450;
    const uint32_t track1_sectors = (uint32_t) std::max(LDA_MIN_SECTORS,
        lda_content_sectors + TRACK_END_RESERVED_SECTORS);

    /* ~7s of silence. Red Book's minimum track length is 4 seconds (300
     * sectors), but no reference dump goes below 526, it is both the
     * smallest and the most common value across the 17 checked, so it makes a
     * better floor than the bare standard minimum. Real audio supplied via
     * --gdi-lda-cdda-track sizes the track instead, having already been held
     * to that same Red Book minimum above. */
    const uint32_t LDA_CDDA_SILENCE_SECTORS = 526;
    const uint32_t track2_sectors = lda_cdda.empty()
        ? LDA_CDDA_SILENCE_SECTORS
        : (uint32_t) ((lda_cdda.size() + 2351) / 2352);
    const uint32_t track3_lba = CD_GD_START_LBA;

    /* Both low-density tracks and the pregap between them have to fit below
     * the high-density zone's fixed start. Only a supplied track 2 can push
     * past it, the generated silence being ~7 seconds. */
    if(track1_sectors + 150 + track2_sectors > track3_lba) {
        std::cerr << term::error << "the low-density area does not fit below LBA " << track3_lba
                  << ": track 1 is " << track1_sectors << " sectors and track 2 is "
                  << track2_sectors << ". Use a shorter --gdi-lda-cdda-track file (at most "
                  << ((track3_lba - track1_sectors - 150) * 2352) << " bytes here)." << std::endl;
        return false;
    }

    if(!lda_cdda.empty()) {
        logging::note() << "Added low-density CDDA track (" << track2_sectors << " sectors) from "
                        << OPTS["gdi-lda-cdda-track"][0];
    }

    /* The low-density volume declares the area through to the end of the
     * audio track, not just its own filesystem. */
    patch_pvd_volume_space(lda_iso, track1_sectors + 150 + track2_sectors);

    /* Pass 2: rebuild with the filler files present. */
    ProgressLine generating("Generating high-density area...");
    std::vector<uint8_t> iso_data = build_hda_iso(input_dir, ip_bin, generating.progress(),
        (uint64_t) hda_track_sectors * 2048);

    if(interrupted()) {
        return false;
    }

    generating.finish();

    logging::debug() << "Data track is " << iso_data.size() << " bytes";

    size_t final_content_sectors = (iso_data.size() + 2047) / 2048;
    if(!truncate && final_content_sectors != content_target_sectors
        && logging::enabled(logging::Level::Normal)) {
        std::cerr << term::warning << "high-density filesystem ended up " << final_content_sectors
                  << " sectors instead of the targeted " << content_target_sectors
                  << " (libisofs structural overhead shifted by "
                  << ((int64_t) final_content_sectors - (int64_t) content_target_sectors)
                  << " sector(s)); 1ST_READ.BIN may not end exactly "
                  << TRACK_END_RESERVED_SECTORS << " sectors before the end of the track."
                  << std::endl;
    }

    /* Where each high-density track begins. Without GD-DA the zone is one data
     * track, exactly as before. With it, the spacer's extent marks out the
     * region no track file covers, and everything is measured from there:
     *
     *   track 3      [45000, spacer + 150)
     *   pregap       [spacer + 150, spacer + 300)
     *   GD-DA        [spacer + 300, spacer + 300 + audio)   flush, no gaps
     *   pregap       [spacer + 300 + audio, spacer + spacer_len)
     *   last track   [spacer + spacer_len, end of zone)
     *
     * The spacer's leading 150 sectors are track 3's own run-out, which is why
     * they are part of it: the file before them must still stop 150 short of
     * the track's end, like on every reference dump. */
    const uint32_t hda_end_lba = track3_lba + (uint32_t) hda_track_sectors;
    uint32_t track3_sectors = (uint32_t) hda_track_sectors;
    uint32_t gdda_start_lba = 0;
    uint32_t last_track_lba = 0;

    if(gdda_spacer_sectors > 0) {
        /* Where the closing data track begins, which is where the spacer ends.
         *
         * The spacer cannot be looked up directly -- it is hidden, so it has no
         * directory record -- so it is found from the first file that does have
         * one, counting back over what sits between them. That is the spacer
         * itself and, behind it, the filler holding the closing track open:
         * both hidden, both of known length, and both there precisely so that
         * no real file lands in the first sectors of that track. */
        uint32_t first_file_lba = find_first_hda_file_lba(iso_data);
        uint32_t hidden_before_first_file =
            (uint32_t) (gdda_spacer_sectors + split_side_sectors);

        uint32_t spacer_lba = first_file_lba > hidden_before_first_file
            ? first_file_lba - hidden_before_first_file
            : 0;

        /* The closing track starts at the filler, not at the first file. */
        first_file_lba = spacer_lba + (uint32_t) gdda_spacer_sectors;

        if(spacer_lba <= track3_lba || first_file_lba >= hda_end_lba) {
            std::cerr << term::error << "could not place the GD-DA tracks in the high-density zone."
                      << std::endl;
            return false;
        }

        track3_sectors = spacer_lba + TRACK_END_RESERVED_SECTORS - track3_lba;
        gdda_start_lba = spacer_lba + TRACK_END_RESERVED_SECTORS + 150;
        last_track_lba = first_file_lba;
    }

    /* The TOC lists every high-density track, so it can only be built now that
     * they are placed. It lives in the 32K system area, which is the head of
     * the image just produced; rewriting it there costs nothing, where
     * folding it in earlier would need a third pass over the filesystem. */
    std::vector<GdiTocTrack> hda_tracks;
    hda_tracks.push_back({track3_lba, /* is_data */ true});

    for(size_t i = 0, lba = gdda_start_lba; i < gdda_tracks.size(); ++i) {
        hda_tracks.push_back({(uint32_t) lba, /* is_data */ false});
        lba += (gdda_tracks[i].size() + 2351) / 2352;
    }

    if(last_track_lba) {
        hda_tracks.push_back({last_track_lba, /* is_data */ true});
    }

    write_gdi_hda_toc(&ip_bin, /* first_hda_track */ 3, hda_tracks, hda_end_lba);
    std::memcpy(&iso_data[0], &ip_bin, sizeof(IPBin));

    /* The boot prerequisite checks, run over what mkdcdisc just built. Here
     * rather than anywhere earlier because the line above is what puts the
     * final IP.BIN -- TOC and all -- into the system area the checks read.
     *
     * The high-density volume only. The low-density one is deliberately not a
     * bootable volume and would fail two of these checks by design: its system
     * area is a 256-byte copy of this header repeated sixteen times, so it
     * declares a boot file that lives up here rather than down there, and it
     * carries no licence screen code at all because nothing ever boots from
     * it. Checking it would mean reporting mkdcdisc's own intended layout as
     * an error on every GD-ROM build.
     *
     * Exact: CD_GD_START_LBA is the ms_block libisofs was handed for this
     * volume, not a figure another tool worked out for itself. */
    if(!boot_checks::check_disc({
        {"the high-density data track", boot_checks::Role::Bootable, &iso_data,
         boot_checks::MsinfoExpectation{CD_GD_START_LBA, /* exact */ true}},
    })) {
        return false;
    }

    cd_image_t* img = cd_new_image();
    cd_image_set_volume_name(img, PATH_TO_CSTR(std::filesystem::path(output_path).filename().stem()));

    cd_session_t* session1 = cd_new_session(img);

    /* Track 1: the low-density filesystem, whose 32K system area is the
     * IP0000.BIN boot image, zero-padded to the full track length. */
    std::vector<uint8_t> track1_data = std::move(lda_iso);
    track1_data.resize(track1_sectors * (size_t) 2048, 0);
    cd_track_t* track1 = cd_new_track(session1, TRACK_TYPE_DATA, &track1_data[0], track1_data.size());
    cd_track_set_mode(track1, TRACK_MODE_MODE1);

    /* Track 2: silence unless --gdi-lda-cdda-track supplied audio, in which
     * case it is zero-padded up to the sector boundary track2_sectors already
     * rounded it to. */
    if(lda_cdda.empty()) {
        cd_new_track_blank(session1, TRACK_TYPE_AUDIO, track2_sectors * 2352);
    } else {
        lda_cdda.resize((size_t) track2_sectors * 2352, 0);
        cd_new_track(session1, TRACK_TYPE_AUDIO, &lda_cdda[0], (uint32_t) lda_cdda.size());
    }

    /* Grow the filesystem image so every track below can be sliced out of it.
     * The tail is the reserved run-out, written as empty sectors; it also
     * absorbs any shortfall left by libisofs overhead. Never shrink: an ISO
     * that overshot the target is written whole (the warning above already
     * flagged it) rather than silently truncated. */
    if(iso_data.size() < hda_track_sectors * 2048) {
        iso_data.resize(hda_track_sectors * 2048, 0);
    }

    cd_session_t* session2 = cd_new_session(img);

    /* Borrowed, not copied: these slices are the high-density filesystem, most
     * of a gigabyte of it on a padded disc, and iso_data outlives the
     * write_disc_image() below that frees the image. Nothing resizes it
     * between here and there -- which is why read_burn_source() was asked for
     * the full track length up front, so the resize above could not move the
     * buffer these point into. The GD-DA and low-density tracks below still
     * copy: a few megabytes each, not worth the lifetime obligation. */
    auto add_hda_data_track = [&](uint32_t lba, const uint8_t* data, size_t bytes) {
        cd_track_t* track = cd_new_track_borrowed(session2, TRACK_TYPE_DATA, data, (uint32_t) bytes);
        cd_track_set_mode(track, TRACK_MODE_MODE1);
        cd_track_set_placement_lba(track, lba);
    };

    add_hda_data_track(track3_lba, &iso_data[0], track3_sectors * (size_t) 2048);

    /* The GD-DA tracks sit flush against one another, so each one's start is
     * simply the previous one's end; no pregap between them, which is what
     * all 78 audio-to-audio boundaries in the reference dumps show. */
    uint32_t lba = gdda_start_lba;
    for(auto& audio: gdda_tracks) {
        cd_track_t* track = cd_new_track(session2, TRACK_TYPE_AUDIO, &audio[0], (uint32_t) audio.size());
        cd_track_set_placement_lba(track, lba);
        lba += (uint32_t) ((audio.size() + 2351) / 2352);
    }

    /* The closing data track: the rest of the filesystem, from where the
     * spacer ends to the end of the zone. Every reference dump carrying GD-DA
     * ends on a data track like this one. */
    if(last_track_lba) {
        size_t offset = (size_t) (last_track_lba - track3_lba) * 2048;
        add_hda_data_track(last_track_lba, &iso_data[offset], iso_data.size() - offset);
    }

    if(!write_disc_image(img, output_path)) {
        return false;
    }

    /* Said at the very end, under the "Wrote ..." line, because it is about the
     * file that line just named: what was produced, not what is about to be.
     *
     * Only when the zone really did stop short. A --gdi-truncate build given
     * enough content runs past the end of the physical zone instead, which the
     * warning further up reports and which is not this problem.
     *
     * Deliberately cautious, and deliberately short. "May not boot" is the
     * whole of what can be claimed: nullDC refuses such an image at three sizes
     * short of the full zone (see gd_track_plan.hpp) and Demul is recorded as
     * refusing one too, but an ODE is said to accept them, and no real hardware
     * has been tried. Which readers those are, and the four builds that
     * measured it, live in docs/gd-image.md -- a closing line is not the place
     * to argue the case, only to say the image is not the standard one. */
    if(truncate && plan.track_sectors < gd_track::FULL_TRACK_SECTORS
        && logging::enabled(logging::Level::Normal)) {
        std::cerr << term::warning << "--gdi-truncate ended the high-density area at LBA "
                  << (CD_GD_START_LBA + plan.track_sectors) << ", where a GD-ROM ends at "
                  << gd_track::GD_END_LBA << ". This is not a standard image and may not boot."
                  << std::endl;
    }

    return true;
}
