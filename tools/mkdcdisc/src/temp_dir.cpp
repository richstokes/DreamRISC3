/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Temporary directory lifecycle: see temp_dir.hpp. */

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>

#include "cli.hpp"
#include "logging.hpp"
#include "temp_dir.hpp"
#include "terminal.hpp"

/* Where the staging directory is created, and the only directory anything
 * here will delete inside.
 *
 * The default is the system temp directory, which is what
 * --temp-dir exists to override: a build stages the whole disc contents
 * plus up to a 74-minute CD of padding, and /tmp is a RAM-backed tmpfs on
 * most current Linux distributions. A disc root of any size therefore fills
 * memory rather than a disk, which is not a failure this program can do
 * anything about from the inside -- only one the user can move elsewhere.
 *
 * Answered from the option every time rather than cached, so that
 * destroy_temp_directory() computes the same root from an atexit() handler
 * without depending on a static having survived. */
static std::filesystem::path staging_root(std::error_code& ec) {
    if(OPTS.count("temp-dir")) {
        ec.clear();
        return std::filesystem::path(OPTS["temp-dir"][0]);
    }
    return std::filesystem::temp_directory_path(ec);
}

std::optional<std::filesystem::path> generate_temp_directory() {
    std::error_code ec;
    auto temp_dir = staging_root(ec);

    if(ec) {
        std::cerr << term::error << "no usable temporary directory: " << ec.message()
                  << std::endl;
        return std::optional<std::filesystem::path>();
    }

    /* A --temp-dir that is missing or is not a directory is the user's
     * typo, and worth saying so about here: without this the only symptom is
     * create_directory() below failing, which reads as an unexplained
     * "failed to create a temporary directory". The default root is checked
     * too -- it costs nothing and a broken TMPDIR deserves the same answer. */
    if(!std::filesystem::is_directory(temp_dir, ec)) {
        if(OPTS.count("temp-dir")) {
            std::cerr << term::error << "--temp-dir does not name a directory: "
                      << temp_dir.string() << std::endl;
        } else {
            std::cerr << term::error << "the system temporary directory is unusable: "
                      << temp_dir.string() << " is not a directory. Pass --temp-dir to "
                         "stage the build somewhere else." << std::endl;
        }
        return std::optional<std::filesystem::path>();
    }

    std::random_device dev;
    std::mt19937 prng(dev());
    std::uniform_int_distribution<uint64_t> rand(0);
    std::filesystem::path path;

    std::stringstream ss;
    ss << std::hex << rand(prng);
    path = temp_dir / ss.str();
    if(std::filesystem::create_directory(path)) {
        logging::debug() << "Staging directory: " << path;
        return path;
    } else {
        std::cerr << term::error << "could not create a staging directory in "
                  << temp_dir.string() << ". Check that it is writable, or pass "
                     "--temp-dir to stage the build somewhere else." << std::endl;
        return std::optional<std::filesystem::path>();
    }
}

/* Every filesystem call here takes an error_code, deliberately: this runs from
 * an atexit() handler, and an exception escaping one of those is not a
 * diagnostic but a std::terminate() -- printed on top of whatever the program
 * was already doing, and with the rest of the cleanup abandoned. There is
 * nothing this function could usefully throw at, either: by the time it runs,
 * the only thing left to do about a failure is to say so. */
void destroy_temp_directory(const std::filesystem::path& path) {
    std::error_code ec;

    /* Resolve and normalize paths to handle symbolic links. Each step is
     * skipped once an earlier one has failed, so `ec` still holds the first
     * failure rather than being cleared by the next call's success. */
    auto temp_root = staging_root(ec);
    auto temp_dir_path = ec ? std::filesystem::path()
                            : std::filesystem::canonical(temp_root, ec);
    auto provided_path_parent = ec ? std::filesystem::path()
                                   : std::filesystem::canonical(path.parent_path(), ec);

    logging::debug() << "Staging directory is under " << provided_path_parent
                     << ", staging root is " << temp_dir_path;

    /* Ensure the provided path is the directory generate_temp_directory()
     * made directly inside the staging root -- the system temp directory, or
     * whatever --temp-dir named. Comparing against the root it was actually
     * created in and not against the system one unconditionally is what lets
     * --temp-dir clean up after itself; the check still holds, since what it
     * guards against is deleting anything other than our own subdirectory,
     * the root very much included. */
    if(ec || provided_path_parent != temp_dir_path) {
        std::cerr<< "Failed to remove temporary directory at : " << path << std::endl;
        return;
    }

    logging::debug() << "Removing staging directory " << path;

    std::filesystem::remove_all(path, ec);

    /* Whatever could be deleted has been; something is still holding the rest.
     * The usual way here is Ctrl+C during a build: on Windows a file cannot be
     * deleted while another thread still has it open, and the cleanup runs
     * while libisofs is still reading the disc contents. Name the directory,
     * since it is now the user's to remove -- it can be most of a gigabyte. */
    if(ec) {
        /* string(), not the path inserter: this one is meant to be read off
         * the screen and pasted into a delete command, not quoted with its
         * separators doubled. */
        std::cerr << term::warning << "could not fully remove the temporary directory "
                  << path.string() << " (" << ec.message() << "). You may want to delete it "
                     "by hand." << std::endl;
    }
}

/* Set once the temp dir is created; cleaned up by cleanup_temp_dir_on_exit(),
 * registered with atexit() below. Routing every exit path through one
 * atexit-registered function -- rather than a destroy_temp_directory() call
 * at each return site -- means normal returns and exit() calls all clean up
 * the same way, with nothing to forget when a new early-return path is added
 * later. atexit() does *not* fire on abort()/an uncaught exception/a killed
 * process, so this alone doesn't cover those; see the try/catch in main() for
 * the rest.
 *
 * Ctrl+C is not handled here, deliberately. Cleaning up after one is not a
 * matter of catching the signal -- the directory cannot be deleted at all
 * while the interrupted step still has its files open -- so what makes it
 * work is that the step stops first. See interrupt.hpp. */
static std::optional<std::filesystem::path> g_temp_dir_to_cleanup;

static void cleanup_temp_dir_on_exit() {
    if(g_temp_dir_to_cleanup) {
        destroy_temp_directory(*g_temp_dir_to_cleanup);
        g_temp_dir_to_cleanup.reset();
    }
}

void install_temp_dir_cleanup(const std::filesystem::path& path) {
    g_temp_dir_to_cleanup = path;
    std::atexit(cleanup_temp_dir_on_exit);
}
