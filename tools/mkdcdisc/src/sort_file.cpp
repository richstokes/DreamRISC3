/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Sort file parsing and application: see sort_file.hpp. */

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "logging.hpp"
#include "sort_file.hpp"
#include "terminal.hpp"

/* Weights by absolute path, as read from one sort file. */
using SortWeights = std::map<std::string, int>;

/* One line of the tree dump. Assembled in full before being handed to the
 * logging module, which owns where the line starts -- the depth indent below
 * is the tree's own, on top of whatever indent the current phase applies. */
static void print_node_info(IsoNode *node, int depth) {
    if(!node) {
        return;
    }

    const char *name = iso_node_get_name(node);
    if(!name)
        return;

    std::ostringstream line;
    line << std::string(depth * 2, ' ') << name;

    /* If it's a directory, add a trailing slash */
    if(iso_node_get_type(node) == LIBISO_DIR) {
        line << "/";
    } else if(iso_node_get_type(node) == LIBISO_FILE) {
        /* Retrieve and print sort weight for files */
        IsoFile *file = (IsoFile*)(node);
        if(file) {
            int weight = iso_file_get_sort_weight(file);
            line << " [Weight: " << weight << "]";
        }
    }

    logging::debug() << line.str();
}

static void traverse_directory(IsoDir *dir, int depth) {
    if(!dir) {
        return;
    }

    IsoNode *node;
    IsoDirIter *iter;

    /* Get directory iterator */
    int result = iso_dir_get_children(dir, &iter);
    if(result < 0) {
        return;
    }

    /* Iterate over directory entries */
    while(iso_dir_iter_next(iter, &node) == 1) {
        if(!node) {
            continue;
        }

        print_node_info(node, depth);

        if(iso_node_get_type(node) == LIBISO_DIR) {
            IsoDir *subdir = (IsoDir*)node;
            traverse_directory(subdir, depth + 1);
        }
    }

    iso_dir_iter_free(iter);
}

static std::string construct_path(const std::vector<std::string> &parents, const std::string &current) {
    std::ostringstream full_path;
    
    for(const auto &dir : parents) {
        full_path << "/" << dir;
    }

    if(!current.empty()) {
        full_path << "/" << current;
    }

    return full_path.str();
}

/* Walks the tree applying the weights, recording in `matched` the path of
 * every node one was found for and in `weighted` the node itself.
 *
 * The lookup runs this way round -- over the disc, asking the weight map --
 * so an entry naming something that is not on the disc is never consulted at
 * all, and would otherwise go by without a trace. Collecting what did match
 * is what lets apply_sort_file() work out what didn't. */
static void traverse_and_set_weights(
    IsoDir *dir,
    const SortWeights &weights,
    std::set<std::string> &matched,
    std::map<IsoNode*, int> &weighted,
    std::vector<std::string> parent_dirs = {}
) {
    if(!dir || weights.empty()) {
        return;
    }

    IsoNode *node;
    IsoDirIter *iter;

    /* Get directory iterator */
    int result = iso_dir_get_children(dir, &iter);
    if(result < 0) {
        return;
    }

    /* Iterate over directory entries */
    while(iso_dir_iter_next(iter, &node) == 1) {
        if(!node) {
            continue;
        }

        const char *name = iso_node_get_name(node);
        if(!name) {
            continue;
        }

        std::string current_name(name);
        std::string child_full_path = construct_path(parent_dirs, current_name);

        /* Check if the node has a specific weight */
        auto child_it = weights.find(child_full_path);
        if(child_it != weights.end()) {
            /* Override with specific weight */
            iso_node_set_sort_weight(node, child_it->second);
            matched.insert(child_full_path);
            weighted[node] = child_it->second;
        }

        /* Recurse into directories */
        if(iso_node_get_type(node) == LIBISO_DIR) {
            /* Add current directory to the parent list and recurse */
            auto updated_parent_dirs = parent_dirs;
            updated_parent_dirs.push_back(current_name);

            traverse_and_set_weights((IsoDir *)node, weights, matched, weighted,
                                     updated_parent_dirs);
        }
    }

    iso_dir_iter_free(iter);
}

/* Reads `file_path` into a fresh weight map, left empty when the file yields
 * nothing usable so the caller can skip the traversal entirely.
 *
 * A line this cannot make sense of is warned about rather than dropped in
 * silence: the build carries on either way, and what it produces is a disc
 * whose file order is not the one that was asked for -- which nothing about
 * the resulting image would ever reveal. Loading times are the whole point of
 * a sort file, so a typo that costs them has to say so.
 *
 * `report` is false on a pass whose diagnostics have already been printed;
 * see DIAGNOSED_OPTIONS. */
static SortWeights parse_sort_file(const std::string &file_path, bool report) {
    SortWeights weights;
    std::ifstream file(file_path);

    if(!file.is_open()) {
        if(report) {
            std::cerr << term::warning << "could not open the sort file " << file_path
                      << "; the disc contents keep their default order." << std::endl;
        }
        return weights;
    }

    std::string line;
    int line_number = 0;

    while(std::getline(file, line)) {
        ++line_number;

        /* Skip empty lines or lines starting with a comment (#) */
        if(line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string path;
        int weight;

        /* Extract path and weight from the line. A path containing a space
         * lands here too, since the first token is then only part of it and
         * the weight is read from the rest -- worth saying, because such a
         * line looks perfectly correct to whoever wrote it. */
        if(!(iss >> path >> weight)) {
            if(report) {
                std::cerr << term::warning << "ignoring line " << line_number << " of the sort file "
                          << file_path << ", which is not a path followed by a weight (a path "
                             "cannot contain spaces here): " << line << std::endl;
            }
            continue;
        }

        weights[path] = weight;
    }

    file.close();

    return weights;
}

/* Above this many orphaned entries, they are counted rather than listed. One
 * typo is worth naming; a whole file written against the wrong path prefix --
 * the usual way this goes wrong, and it orphans every line at once -- is worth
 * one line saying so, not forty saying it separately. */
static const size_t UNMATCHED_LIST_LIMIT = 5;

/* Reports sort file entries that named nothing on the disc. They parsed fine,
 * so nothing upstream objected; they simply gave a weight to a path the volume
 * does not have, and the disc comes out ordered as if the line had never been
 * written.
 *
 * Almost always a path written against the source tree rather than the volume,
 * which is why the closing note is worth its line: -d and -D differ in exactly
 * that, and the difference is invisible until something like this points at
 * it. */
static void report_unmatched_entries(const std::string &file_path,
                                     const std::vector<std::string> &unmatched) {
    /* -v 2 asks to be told about each thing handled, so it overrides the
     * summary and gets the list however long it is. */
    const bool list_them = unmatched.size() <= UNMATCHED_LIST_LIMIT
                        || logging::enabled(logging::Level::Verbose);

    if(list_them) {
        for(const auto &path : unmatched) {
            std::cerr << term::warning << "the sort file " << file_path << " gives a weight to "
                      << path << ", which is not on the disc, so it has no effect." << std::endl;
        }
    } else {
        std::cerr << term::warning << unmatched.size() << " entries of the sort file "
                  << file_path << " name nothing on the disc and have no effect. "
                     "Re-run with -v 2 to list them." << std::endl;
    }

    std::cerr << term::note << "sort file paths are those of the disc being built, and start "
                               "with \"/\": -d keeps the name of the directory it was given, "
                               "-D does not." << std::endl;
}

/* Options whose sort file has already been complained about. The GD-ROM path
 * builds its high-density filesystem twice -- once to measure how big it comes
 * out, once for real (see build_gd.cpp) -- so this runs twice over the same
 * file, and a warning left ungated would tell the user about their typo twice
 * for a GD-ROM and once for a MIL-CD. The -v 3 weight dumps below are left to
 * repeat: at that level, seeing both passes is the point. */
static std::set<std::string> DIAGNOSED_OPTIONS;

std::map<IsoNode*, int> apply_sort_file(const char *option, IsoDir *root) {
    /* Every node a weight was applied to, and the weight it got, so that
     * --duplicates-once can work out the highest of a set of identical
     * files. */
    std::map<IsoNode*, int> weighted;

    if(!root || !OPTS.count(option)) {
        return weighted;
    }

    const bool report = logging::enabled(logging::Level::Normal)
                     && DIAGNOSED_OPTIONS.insert(option).second;

    SortWeights weights = parse_sort_file(OPTS[option][0], report);
    if(weights.empty()) {
        return weighted;
    }

    if(logging::enabled(logging::Level::Debug)) {
        logging::debug() << "--" << option << " weights before:";
        traverse_directory(root, 0);
    }

    /* Every path a weight was actually applied to. */
    std::set<std::string> matched;

    /* Set root weight if one is assigned. Handled here rather than in the
     * traversal, which only ever sees the root's children, so it counts as
     * matched here too. */
    auto it = weights.find("/");
    if(it != weights.end()) {
        iso_node_set_sort_weight((IsoNode *)root, it->second);
        matched.insert("/");
        weighted[(IsoNode *)root] = it->second;
    }

    traverse_and_set_weights(root, weights, matched, weighted);

    if(report) {
        std::vector<std::string> unmatched;
        for(const auto &entry : weights) {
            if(!matched.count(entry.first)) {
                unmatched.push_back(entry.first);
            }
        }

        if(!unmatched.empty()) {
            report_unmatched_entries(OPTS[option][0], unmatched);
        }
    }

    if(logging::enabled(logging::Level::Debug)) {
        logging::debug() << "--" << option << " weights after:";
        traverse_directory(root, 0);
    }

    return weighted;
}
