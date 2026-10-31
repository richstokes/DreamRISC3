/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Content-based duplicate elimination: see duplicates.hpp. */

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "cli.hpp"
#include "duplicates.hpp"
#include "logging.hpp"
#include "terminal.hpp"

namespace {

/* Read buffer size for streaming operations. Free to be modified: unlike
 * FAST_DIGEST_BYTES below, it changes no result. */
constexpr size_t READ_CHUNK = 64 * 1024;

/* How much of a file the first digest covers, the same figure mkisofs uses
 * for its digest_fast (DIGEST_FAST_SIZE in its `hash.c`). */
constexpr off_t FAST_DIGEST_BYTES = 65536;

struct Candidate {
    IsoFile* file = nullptr;
    IsoDir* parent = nullptr;
    std::string path;
    off_t size = 0;
};

void collect_candidates(IsoDir* dir, const std::string& prefix,
                        const std::set<IsoNode*>& excluded,
                        std::vector<Candidate>& out) {
    IsoDirIter* iter = nullptr;
    if(iso_dir_get_children(dir, &iter) != 1) {
        return;
    }

    /* The tree must not be modified while an iterator is open on it, so this
     * only ever reads. */
    IsoNode* node = nullptr;
    while(iso_dir_iter_next(iter, &node) == 1) {
        const char* name = iso_node_get_name(node);
        if(!name) {
            continue;
        }

        std::string path = prefix + "/" + name;

        if(iso_node_get_type(node) == LIBISO_DIR) {
            collect_candidates(reinterpret_cast<IsoDir*>(node), path, excluded, out);
            continue;
        }

        if(iso_node_get_type(node) != LIBISO_FILE || excluded.count(node)) {
            continue;
        }

        IsoFile* file = reinterpret_cast<IsoFile*>(node);
        off_t size = iso_stream_get_size(iso_file_get_stream(file));

        if(size > 0) {
            out.push_back(Candidate{file, dir, path, size});
        }
    }

    iso_dir_iter_free(iter);
}

bool digest_stream(IsoStream* stream, std::array<char, 16>& out, off_t limit,
                   std::vector<char>& workspace_buf) {
    if(iso_stream_open(stream) < 0) {
        return false;
    }

    void* md5 = nullptr;
    if(iso_md5_start(&md5) < 0) {
        iso_stream_close(stream);
        return false;
    }

    bool ok = true;
    off_t done = 0;

    while(done < limit) {
        const size_t want = static_cast<size_t>(
            std::min<off_t>(static_cast<off_t>(workspace_buf.size()), limit - done)
        );
        int n = iso_stream_read(stream, workspace_buf.data(), want);
        if(n == 0) {
            break;
        }
        if(n < 0) {
            ok = false;
            break;
        }
        iso_md5_compute(md5, workspace_buf.data(), n);
        done += n;
    }

    iso_md5_end(&md5, out.data());
    iso_stream_close(stream);
    return ok;
}

bool streams_identical(IsoStream* a, IsoStream* b,
                       std::vector<char>& buf_a, std::vector<char>& buf_b) {
    if(iso_stream_get_size(a) != iso_stream_get_size(b)) {
        return false;
    }
    if(iso_stream_open(a) < 0) {
        return false;
    }
    if(iso_stream_open(b) < 0) {
        iso_stream_close(a);
        return false;
    }

    bool identical = true;
    for(;;) {
        int na = iso_stream_read(a, buf_a.data(), buf_a.size());
        int nb = iso_stream_read(b, buf_b.data(), buf_b.size());

        if(na < 0 || nb < 0 || na != nb) {
            identical = false;
            break;
        }
        if(na == 0) {
            break;
        }
        if(std::memcmp(buf_a.data(), buf_b.data(), static_cast<size_t>(na)) != 0) {
            identical = false;
            break;
        }
    }

    iso_stream_close(a);
    iso_stream_close(b);
    return identical;
}

bool rebuild_sharing_stream(IsoImage* image, const Candidate& alias, IsoStream* shared,
                            int sort_weight) {
    IsoNode* node = reinterpret_cast<IsoNode*>(alias.file);

    const char* name_cstr = iso_node_get_name(node);
    if(!name_cstr) {
        return false;
    }

    const std::string name = name_cstr;
    const mode_t mode = iso_node_get_permissions(node);
    const uid_t uid = iso_node_get_uid(node);
    const gid_t gid = iso_node_get_gid(node);
    const time_t mtime = iso_node_get_mtime(node);
    const time_t atime = iso_node_get_atime(node);
    const time_t ctime = iso_node_get_ctime(node);
    const int hidden = iso_node_get_hidden(node);

    /* iso_node_new_file() (via iso_image_add_new_file) stores the stream pointer
     * without taking a reference of its own, so the new node consumes one of ours. */
    iso_stream_ref(shared);

    /* Unlinks without freeing: this call hands us the node's own reference,
     * which has to go back once the replacement is in place. */
    if(iso_node_take(node) < 0) {
        iso_stream_unref(shared);
        return false;
    }

    IsoFile* replacement = nullptr;
    if(iso_image_add_new_file(image, alias.parent, name.c_str(), shared, &replacement) < 0) {
        if(iso_dir_add_node(alias.parent, node, ISO_REPLACE_NEVER) < 0) {
            iso_node_unref(node);
        }
        iso_stream_unref(shared);
        return false;
    }

    iso_node_unref(node);

    IsoNode* rebuilt = reinterpret_cast<IsoNode*>(replacement);
    iso_node_set_permissions(rebuilt, mode);
    iso_node_set_uid(rebuilt, uid);
    iso_node_set_gid(rebuilt, gid);
    iso_node_set_mtime(rebuilt, mtime);
    iso_node_set_atime(rebuilt, atime);
    iso_node_set_ctime(rebuilt, ctime);
    iso_node_set_hidden(rebuilt, hidden);

    /* The weight is assigned to all nodes sharing the extent, not just the primary.
     * filesrc.c takes sort_weight from the first node to create the source and
     * discards the rest. Setting it everywhere avoids a lottery. */
    iso_node_set_sort_weight(rebuilt, sort_weight);

    return true;
}

}  /* namespace */

DuplicateStats collapse_duplicate_files(IsoImage* image, IsoDir* root,
                                        const std::set<IsoNode*>& excluded,
                                        const std::map<IsoNode*, int>& weights) {
    DuplicateStats stats;
    if(!image || !root) {
        return stats;
    }

    std::vector<Candidate> candidates;
    collect_candidates(root, "", excluded, candidates);

    std::vector<char> io_buffer_a(READ_CHUNK);
    std::vector<char> io_buffer_b(READ_CHUNK);

    std::map<off_t, std::vector<const Candidate*>> by_size;
    for(const auto& candidate: candidates) {
        by_size[candidate.size].push_back(&candidate);
    }

    for(const auto& [size, sized]: by_size) {
        if(sized.size() < 2) {
            continue;
        }

        std::map<std::array<char, 16>, std::vector<const Candidate*>> by_prefix;
        for(const Candidate* candidate: sized) {
            std::array<char, 16> digest{};
            if(digest_stream(iso_file_get_stream(candidate->file), digest,
                             std::min<off_t>(size, FAST_DIGEST_BYTES), io_buffer_a)) {
                by_prefix[digest].push_back(candidate);
            }
        }

        for(const auto& [prefix, prefixed]: by_prefix) {
            if(prefixed.size() < 2) {
                continue;
            }

            std::map<std::array<char, 16>, std::vector<const Candidate*>> by_content;
            if(size <= FAST_DIGEST_BYTES) {
                by_content[prefix] = prefixed;
            } else {
                for(const Candidate* candidate: prefixed) {
                    std::array<char, 16> digest{};
                    if(digest_stream(iso_file_get_stream(candidate->file), digest, size, io_buffer_a)) {
                        by_content[digest].push_back(candidate);
                    }
                }
            }

            for(auto& [digest, matches]: by_content) {
                (void) digest;

                if(matches.size() < 2) {
                    continue;
                }

                std::sort(matches.begin(), matches.end(),
                    [](const Candidate* a, const Candidate* b) { return a->path < b->path; });

                int group_weight = 0;
                for(const Candidate* candidate: matches) {
                    auto at = weights.find(reinterpret_cast<IsoNode*>(candidate->file));
                    if(at != weights.end() && at->second > group_weight) {
                        group_weight = at->second;
                    }
                }

                const Candidate* primary = matches.front();
                IsoStream* shared = iso_file_get_stream(primary->file);
                size_t collapsed = 0;

                for(size_t i = 1; i < matches.size(); ++i) {
                    const Candidate* alias = matches[i];

                    if(!streams_identical(shared, iso_file_get_stream(alias->file), io_buffer_a, io_buffer_b)) {
                        logging::debug() << "Kept " << alias->path << " and "
                                         << primary->path << " apart: the byte comparison "
                                         << "did not confirm the digest match";
                        continue;
                    }

                    if(!rebuild_sharing_stream(image, *alias, shared, group_weight)) {
                        logging::debug() << "Failed to share extent of " << primary->path
                                         << " with " << alias->path;
                        continue;
                    }

                    logging::debug() << "Shared extent of " << primary->path << " with "
                                     << alias->path << " (" << static_cast<long long>(size) << " bytes)";
                    ++collapsed;
                    stats.bytes_saved += static_cast<uint64_t>(size);
                }

                if(!collapsed) {
                    continue;
                }

                ++stats.groups;
                stats.aliases += collapsed;

                iso_node_set_sort_weight(reinterpret_cast<IsoNode*>(primary->file), group_weight);

                for(const Candidate* candidate: matches) {
                    auto at = weights.find(reinterpret_cast<IsoNode*>(candidate->file));
                    if(at == weights.end() || at->second >= group_weight) {
                        continue;
                    }

                    stats.weight_conflicts.push_back(
                        "--duplicates-once placed " + candidate->path + " at weight "
                        + std::to_string(group_weight) + " instead of "
                        + std::to_string(at->second) + " (shares extent with higher weighted file)");
                }
            }
        }
    }

    return stats;
}

void apply_duplicates_once(const char* area, IsoImage* image, IsoDir* root,
                           const std::set<IsoNode*>& excluded,
                           const std::map<IsoNode*, int>& weights) {
    if(!OPTS.count("duplicates-once")) {
        return;
    }

    DuplicateStats stats = collapse_duplicate_files(image, root, excluded, weights);

    static std::set<std::string> REPORTED_AREAS;
    if(!REPORTED_AREAS.insert(area).second) {
        return;
    }

    if(stats.aliases) {
        logging::note() << "Deduplicated " << stats.aliases << " file"
                        << (stats.aliases == 1 ? "" : "s") << " in " << stats.groups << " set"
                        << (stats.groups == 1 ? "" : "s") << ", saving " << stats.bytes_saved
                        << " bytes in the " << area;
    }

    if(!stats.weight_conflicts.empty() && logging::enabled(logging::Level::Normal)) {
        for(const std::string& conflict: stats.weight_conflicts) {
            std::cerr << term::warning << conflict << "." << std::endl;
        }
    }
}
