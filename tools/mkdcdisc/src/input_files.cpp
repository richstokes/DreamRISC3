/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* User-supplied input files: see input_files.hpp. */

#include <fstream>
#include <iostream>
#include <sstream>

#include "cli.hpp"
#include "input_files.hpp"
#include "interrupt.hpp"
#include "ip_boot.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "progress.hpp"
#include "terminal.hpp"
#include "third_party/elf_parser/elf_parser.hpp"
#include "third_party/scramble/scramble.h"




bool load_elf_file(const std::filesystem::path& elf_name, std::vector<char>& bin_data) {
    if(!std::filesystem::exists(elf_name)) {
        std::cerr << term::error << "ELF file does not exist: " << elf_name << std::endl;
        return false;
    }

    logging::verbose() << "Loading " << elf_name;

    auto elf_parser_ret = elfparser::Parser::Load(elf_name);
    if (!elf_parser_ret.has_value()) {
        std::cerr << term::error << "failed to load ELF " << elf_name << std::endl;
        return false;
    }
    auto elf_parser = elf_parser_ret.value();
    
    // Create BIN from  ELF
    if ( !elf_parser->fill_bin(bin_data)) {
        std::cerr << term::error << "failed to create BIN" << std::endl;
        return false;
    }

    return true;
}

bool load_bin_file(const std::filesystem::path& bin_path, std::vector<char>& bin_data) {
    if(!std::filesystem::exists(bin_path)) {
        std::cerr << term::error << "BIN file does not exist: " << bin_path << std::endl;
        return false;
    }

    logging::verbose() << "Loading " << bin_path;

    std::ifstream file;
    std::streamsize file_size = 0;

    file.open(bin_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << term::error << "unable to open BIN file: " << bin_path << std::endl;
        return false;
    }

    file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    bin_data.resize(file_size);
    if (!file.read(&bin_data[0], file_size)) {
        std::cerr << term::error << "unable to read BIN file: " << bin_path << std::endl;
        file.close();
        return false;
    }
    file.close();

    return true;
}

/* Drops any trailing separators from a directory path.
 *
 * Shell completion adds one by itself, and it makes the path awkward twice
 * over: filename() is then empty, so the directory's own name has to be dug
 * out of the parent; and stat() rejects such a path outright on some C
 * runtimes, so is_directory() answers false for a directory that plainly
 * exists. Normalising once, up front, removes both problems rather than
 * working around each. */
static std::filesystem::path strip_trailing_separators(std::filesystem::path path) {
    while(path.has_relative_path() && path.filename().empty()) {
        path = path.parent_path();
    }

    return path;
}

/* The file -e/-b/-B was given, and how a diagnostic names the option that
 * supplied it. Exactly one of the three is present by the time either is
 * called: main() refuses an invocation carrying none of them, or several. */
static std::filesystem::path main_executable_source() {
    for(const char* key: {"main-elf", "main-binary-unscrambled", "main-binary-scrambled"}) {
        if(OPTS.count(key)) {
            return OPTS[key][0];
        }
    }

    return {};
}

static const char* main_executable_option() {
    if(OPTS.count("main-elf")) {
        return "-e/--main-elf";
    }
    if(OPTS.count("main-binary-unscrambled")) {
        return "-b/--main-binary-unscrambled";
    }

    return "-B/--main-binary-scrambled";
}

/* Whether two paths lead to the same file. equivalent() rather than comparing
 * the paths themselves: "-e ./build/game.elf -D build" reaches one file two
 * ways, and so do a symlink, a "..", and a difference of case on a Windows
 * filesystem. It sets the error code rather than throwing when a path does not
 * exist, which answers false, as it should. */
static bool is_same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    if(b.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) && !ec;
}

/* Said at normal verbosity rather than under -v 2: leaving out a file that was
 * asked for is not a detail of how the copying went, and the size is the point
 * of saying it at all. An ELF is typically the boot binary's size over again,
 * so a -D across a build directory quietly doubles what the data track carries
 * to no purpose -- the disc already holds this file, converted, under the boot
 * name. */
static void warn_executable_not_added(const std::filesystem::path& path, const char* option) {
    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(path, ec);

    std::cerr << term::warning << "not adding " << path << " from " << option << ": it is the "
              << main_executable_option() << " file, already on the disc as " << boot_filename();

    if(!ec) {
        std::cerr << " (saves " << size << " bytes)";
    }

    std::cerr << "." << std::endl;
}

/* The other reason an entry is left out: it is not the executable, but it
 * carries the name the executable takes on the disc, so copying it would
 * replace the one that was just generated with one that does not boot. */
static void warn_boot_filename_not_added(const std::filesystem::path& path, const char* option) {
    std::cerr << term::warning << "not adding " << path << " from " << option << ": "
              << boot_filename() << " is generated from " << main_executable_option()
              << ", and this copy would replace it." << std::endl;
}

/* Where the main executable sits inside `dir`, relative to it, or nullopt when
 * that tree does not hold it. Walked rather than worked out with relative(),
 * which compares paths where this has to compare files. */
static std::optional<std::filesystem::path> executable_within(
        const std::filesystem::path& dir, const std::filesystem::path& exe) {
    if(exe.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    auto walk = std::filesystem::recursive_directory_iterator(dir, ec);
    if(ec) {
        return std::nullopt;
    }

    for(const auto& entry: walk) {
        if(entry.is_directory(ec)) {
            continue;
        }

        if(is_same_file(entry.path(), exe)) {
            return std::filesystem::relative(entry.path(), dir);
        }
    }

    return std::nullopt;
}

/* Copies the tree at `src` to `dst`, then drops the staged copy of the main
 * executable when the tree carried one.
 *
 * Removed afterwards rather than filtered on the way in because
 * std::filesystem::copy() takes no predicate, and excluding one file otherwise
 * means replacing a single library call with a recursive copy loop of our own.
 * The cost is one file written and deleted inside the staging directory, and
 * nothing at all on the disc. */
static void copy_tree(const std::filesystem::path& src, const std::filesystem::path& dst,
                      const std::filesystem::path& exe, const char* option) {
    std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive);

    if(auto relative_path = executable_within(src, exe)) {
        std::error_code ec;
        std::filesystem::remove(dst / *relative_path, ec);
        warn_executable_not_added(src / *relative_path, option);
    }
}

static bool copy_disc_contents(const std::filesystem::path& out,
                               const std::filesystem::path& exe);

bool gather_files(const std::filesystem::path& out, bool scramble_output) {
    std::vector<char> bin_data;
    bool source_is_scrambled = false;
    const std::filesystem::path exe = main_executable_source();

    /* Each of the three things this function does is announced as a phase of
     * its own, in the order it happens, rather than the lot of them hiding
     * behind one "gathering files" line: reading the executable, converting
     * it, and copying in the disc contents fail for entirely different
     * reasons and take entirely different amounts of time. */
    {
        ProgressLine loading("Loading main executable...");

        if(OPTS.count("main-elf")) {
            load_elf_file(exe, bin_data);
        } else {
            load_bin_file(exe, bin_data);
            source_is_scrambled = OPTS.count("main-binary-scrambled") > 0;
        }

        if (!bin_data.size()) {
            std::cerr << term::error << "no bin data available, aborting..." << std::endl;
            return false;
        }

        logging::debug() << "Executable is " << bin_data.size() << " bytes"
                         << (source_is_scrambled ? ", already scrambled" : ", unscrambled");

        loading.finish();
    }

    /* Bring the binary to whatever scrambling state the output format needs,
     * regardless of which of -e/-b/-B supplied it -- a -B binary destined for
     * -F gdi is unscrambled here rather than rejected at the CLI level.
     *
     * The phase is named after what it actually does to this particular
     * binary, so the log answers whether the scrambler ran at all: the one
     * thing that differs between a MIL-CD and a GD-ROM build here. */
    {
        const bool converting = source_is_scrambled != scramble_output;
        std::string verb = !converting ? "Writing "
                         : scramble_output ? "Scrambling " : "Unscrambling ";

        ProgressLine converting_line(verb + boot_filename() + "...");

        std::vector<char> output_data;
        if(!converting) {
            output_data = bin_data;
        } else if (scramble_output) {
            output_data = scramble(bin_data);
        } else {
            output_data = unscramble(bin_data);
        }

        std::filesystem::path scrambled_path = out / boot_filename();

        std::ofstream output_stream;
        output_stream.open(scrambled_path, std::ios::binary);
        if (!output_stream.is_open())
        {
            std::cerr << term::error << "failed to open " << scrambled_path << std::endl;
            return false;
        }
        output_stream.write(&output_data[0], output_data.size());
        output_stream.close();

        logging::debug() << "Wrote " << output_data.size() << " bytes to " << scrambled_path;

        converting_line.finish();
    }

    return copy_disc_contents(out, exe);
}

/* The -d/-D/-f trees and files, copied into the staging directory that
 * becomes the data track. Split out of gather_files() so the copying is one
 * phase with one name, rather than the tail of a function that has already
 * done two other things. */
static bool copy_disc_contents(const std::filesystem::path& out,
                               const std::filesystem::path& exe) {
    /* Nothing asked for is not a phase: an image built from -e alone would
     * otherwise report a step that copied nothing. */
    if(!OPTS.count("directory") && !OPTS.count("directory-contents") && !OPTS.count("file")) {
        logging::verbose() << "No -d/-D/-f contents given; the data track holds only "
                           << boot_filename();
        return true;
    }

    ProgressLine gathering("Gathering input files...");

    /* Checked between copies rather than inside them: a copy holds both its
     * source and its destination open, and the staging directory cannot be
     * deleted until they are closed. One tree still has to finish copying
     * before the request is noticed (std::filesystem::copy() is a single
     * call with nothing to poll) but it does finish, rather than being
     * abandoned mid-file with the handle still open. */
    if(OPTS.count("directory")) {
        for(const std::filesystem::path given: OPTS["directory"]) {
            if(interrupted()) {
                return false;
            }

            std::filesystem::path dir = strip_trailing_separators(given);

            if(!std::filesystem::is_directory(dir)) {
                std::cerr << term::error << "no such directory: " << given.string() << std::endl;
                return false;
            }

            /* The name the directory takes on the disc. filename(), not
             * stem(): stem() drops everything from the last dot on, so
             * "-d data.v2" would land as "data". */
            std::filesystem::path name = dir.filename();
            if(name.empty()) {
                std::cerr << term::error << "cannot work out a directory name from: " << given.string() << std::endl;
                return false;
            }

            logging::verbose() << "Adding directory " << dir << " as " << name;
            copy_tree(dir, out / name, exe, "-d/--directory");
        }
    }

    if(OPTS.count("directory-contents")) {
        for(const std::filesystem::path given: OPTS["directory-contents"]) {
            std::filesystem::path dir = strip_trailing_separators(given);

            if(std::filesystem::is_directory(dir)) {
                for (const auto & entry : std::filesystem::directory_iterator(dir)) {
                    if(interrupted()) {
                        return false;
                    }

                    /* Identity before name: an entry that is both the
                     * executable and named after it is better explained as the
                     * first than as the second. */
                    if(is_same_file(entry.path(), exe)) {
                        warn_executable_not_added(entry.path(), "-D/--directory-contents");
                        continue;
                    }

                    if(entry.path().filename() == boot_filename()) {
                        warn_boot_filename_not_added(entry.path(), "-D/--directory-contents");
                        continue;
                    }

                    logging::verbose() << "Adding " << entry.path().filename();

                    /* A subdirectory goes through copy_tree(), which leaves the
                     * executable behind wherever in it the tree keeps it: -D
                     * only iterates the top level, so a build directory nested
                     * one deep would otherwise carry it across untouched. */
                    if(entry.is_directory()) {
                        copy_tree(entry.path(), out / entry.path().filename(), exe,
                            "-D/--directory-contents");
                    } else {
                        std::filesystem::copy(entry, out / entry.path().filename());
                    }
                }
            } else {
                std::cerr << term::error << "no such directory: " << dir.string() << std::endl;
                return false;
            }
        }
    }

    if(OPTS.count("file")) {
        for(std::filesystem::path file: OPTS["file"]) {
            if(interrupted()) {
                return false;
            }

            if(std::filesystem::exists(file)) {
                /* Left out on the same two grounds as a -d/-D entry, rather
                 * than let through because this one was named explicitly. The
                 * disc already carries the executable, and the copy at the
                 * boot name would be the one that replaced it -- which used to
                 * end the run on a raw filesystem error from the copy itself. */
                if(is_same_file(file, exe)) {
                    warn_executable_not_added(file, "-f/--file");
                    continue;
                }

                if(file.filename() == boot_filename()) {
                    warn_boot_filename_not_added(file, "-f/--file");
                    continue;
                }

                logging::verbose() << "Adding " << file << " -> " << out / file.filename();

                std::filesystem::copy(file, out / file.filename());
            } else {
                std::cerr << term::error << "no such file: " << file.string() << std::endl;
                return false;
            }
        }
    }

    gathering.finish();
    return true;
}

/* Reads a whole file into memory. Returns nullopt if it can't be opened. */
std::optional<std::string> read_file_contents(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if(!f) {
        return std::nullopt;
    }

    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
