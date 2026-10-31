/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Boot prerequisite checks: see boot_checks.hpp. */

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "boot_checks.hpp"
#include "cli.hpp"
#include "ip_boot.hpp"
#include "iso_reader.hpp"
#include "logging.hpp"
#include "progress.hpp"
#include "terminal.hpp"

namespace boot_checks {

namespace {

/* Where a finding goes and whether it stops the build.
 *
 * Collected through one object rather than printed at each call site so that
 * --disable-bootstrap-checks is honoured in exactly one place: a check states
 * what it found and how strongly it knows it, and never has to consult the
 * option itself. A check that tested the flag on its own would be a check that
 * could forget to.
 *
 * Which severity a finding carries decides whether -q/-v 0 can silence it. An
 * error always prints: a build that refuses to produce a disc has to say why,
 * whatever was asked of the output, which is what logging.hpp's level 0 makes
 * its one exception. Everything softer is silenceable, the way every other
 * structural warning here already is -- an ISO whose size is not a whole number
 * of sectors (disc_layout.cpp), a boot binary that could not be pinned at the
 * end of a GD-ROM's high-density area (build_gd.cpp). A run asked for silence
 * and still given a disc should be silent. */
class Report {
public:
    /* A disc that will not boot. Blocking, unless the user has said they know
     * better -- in which case the same text is printed as a warning and the
     * build carries on. */
    void fail(const std::string& message) {
        if(OPTS.count("disable-bootstrap-checks")) {
            /* Now a warning in every sense, silence included: the build is
             * going ahead, so this is advice about the disc rather than the
             * reason there isn't one. */
            if(logging::enabled(logging::Level::Normal)) {
                std::cerr << term::warning << message << std::endl;
            }

            overridden_ += 1;
            return;
        }

        std::cerr << term::error << message << std::endl;
        failed_ += 1;
    }

    /* A disc that is probably wrong, on evidence that stops short of certain.
     * Never blocking, whatever the options say. */
    void warn(const std::string& message) {
        if(logging::enabled(logging::Level::Normal)) {
            std::cerr << term::warning << message << std::endl;
        }
    }

    /* Worth knowing, wrong in no way at all. */
    void note(const std::string& message) {
        logging::note() << message;
    }

    bool ok() const { return failed_ == 0; }
    size_t overridden() const { return overridden_; }

private:
    size_t failed_ = 0;
    size_t overridden_ = 0;
};

/* The boot header a volume carries, or nullptr when it is too short to carry
 * one at all.
 *
 * The header is the first 32K of the volume -- the system area, which ECMA-119
 * reserves and says nothing about, so ISO9660 and the Dreamcast are not in any
 * disagreement about who owns those bytes. IPBin maps them exactly, and is all
 * char arrays, so there is no alignment to satisfy beyond what any allocation
 * already gives. */
const IPBin* boot_header(const std::vector<uint8_t>& iso) {
    if(iso.size() < sizeof(IPBin)) {
        return nullptr;
    }

    return reinterpret_cast<const IPBin*>(iso.data());
}

/* Whether those 32K are a boot header rather than 32K of something else. The
 * Hardware ID is what answers it: every Dreamcast disc carries exactly
 * "SEGA SEGAKATANA " there, and nothing that is not a Dreamcast disc does. */
bool has_hardware_id(const IPBin* header) {
    return std::memcmp(header->meta.hardware_id, IPBIN_HARDWARE_ID,
        sizeof(header->meta.hardware_id)) == 0;
}

/* An IP.BIN offset written the way the layout tables write it, so a diagnostic
 * can be read straight against the documentation. */
std::string as_ipbin_offset(size_t offset) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << offset;
    return out.str();
}

/* Check 1: the volume carries a boot header, and the parts of it that are not
 * anybody's to choose say what they must.
 *
 * Only the fixed fields are checked. Everything else in the header -- the
 * product number, the area symbols, the dates, the peripheral bits -- is the
 * disc's own business, and a disc whose metadata differs from what mkdcdisc
 * would have generated is not a disc with a problem. The device information
 * checksum is left alone for the same reason and one better: writing 0000
 * there is documented to work.
 *
 * Returns whether a header was found, so the caller can skip the checks that
 * would only restate its absence. */
bool check_boot_header(Report& report, const Volume& volume, const IPBin* header) {
    const std::vector<uint8_t>& iso = *volume.data;
    const bool present = header != nullptr && has_hardware_id(header);

    /* merged-data's session 1: the console never boots it, so it needs no
     * header -- and one that is there anyway is not an error, just something
     * that will be ignored, which is worth saying before the user spends time
     * wondering why editing it changes nothing. */
    if(volume.role == Role::NotBooted) {
        if(present) {
            report.note("The boot header in " + volume.name + " is unused: the console boots "
                "from session 2, so session 2's header is the one that decides how this disc "
                "starts. Editing this one changes nothing.");
        }

        return present;
    }

    if(header == nullptr) {
        report.fail(volume.name + " is " + std::to_string(iso.size()) + " bytes, too short to "
            "hold the 32768-byte boot header the console reads before anything else. The disc "
            "will not boot.");
        return false;
    }

    if(!present) {
        report.fail(volume.name + " carries no boot header: its first 16 sectors do not begin "
            "with the Hardware ID \"" + std::string(IPBIN_HARDWARE_ID) + "\" that every "
            "Dreamcast disc starts with. Build the ISO with mkisofs' -G switch, pointing it at "
            "an IP.BIN. The disc will not boot.");
        return false;
    }

    if(std::memcmp(header->meta.maker_id, IPBIN_MAKER_ID, sizeof(header->meta.maker_id)) != 0) {
        report.fail(volume.name + " declares the Maker ID \""
            + trim_ipbin_field(header->meta.maker_id, sizeof(header->meta.maker_id))
            + "\" at IP.BIN offset " + as_ipbin_offset(0x010) + ", where every Dreamcast disc "
            "declares \"" + std::string(IPBIN_MAKER_ID) + "\". The disc will not boot.");
    }

    /* The name the console will look up at boot time. Whether a file of that
     * name is actually on the disc is a separate question, asked further down;
     * this one is only whether the field names anything lookupable at all. */
    std::string declared = trim_ipbin_field(header->meta.boot_filename,
        sizeof(header->meta.boot_filename));

    if(!is_usable_boot_filename(declared)) {
        report.fail(volume.name + " declares "
            + (declared.empty() ? std::string("no boot file name")
                                : "the boot file name \"" + declared + "\"")
            + " at IP.BIN offset " + as_ipbin_offset(0x060) + ". That is the name the console "
            "looks up at the root of the filesystem to start the disc, so it has to be a plain "
            "file name. The disc will not boot.");
    }

    return true;
}

/* Whether two file names are the same name but for their case.
 *
 * Written out because the standard library has no case-insensitive string
 * comparison at all -- every std::string comparison is case-sensitive, and
 * there is no icompare. strcasecmp() would do it, but it is POSIX rather than
 * C++, is spelled _stricmp() on MSVC, and is locale-sensitive in some
 * implementations; nothing in this project reaches for it.
 *
 * ASCII throughout, which is all an ISO9660 primary tree holds. std::toupper
 * is locale-dependent in principle, and safe here in fact: nothing calls
 * setlocale(), so the program stays in the "C" locale where it folds A-Z and
 * nothing else. The parameters are unsigned char because std::toupper is
 * undefined for the negative values a plain char can hold. */
bool same_name_ignoring_case(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char x, unsigned char y) { return std::toupper(x) == std::toupper(y); });
}

/* A few of the names actually at the root, for a message that would otherwise
 * only be able to say what is not there. Capped, because the answer is meant
 * to orient the reader rather than to be the whole listing -- and a disc with
 * two hundred files at its root would bury the sentence it belongs to. */
std::string root_contents_summary(const std::vector<iso9660::Entry>& entries) {
    const size_t MAX_NAMED = 8;

    if(entries.empty()) {
        return "nothing at all";
    }

    std::string listed;
    for(size_t i = 0; i < entries.size() && i < MAX_NAMED; ++i) {
        if(i > 0) {
            listed += ", ";
        }
        listed += entries[i].name + (entries[i].is_dir ? "/" : "");
    }

    if(entries.size() > MAX_NAMED) {
        listed += ", and " + std::to_string(entries.size() - MAX_NAMED) + " more";
    }

    return listed;
}

/* Check 2: the file the header names is at the root of the filesystem.
 *
 * The name is read from the header rather than assumed to be 1ST_READ.BIN:
 * that is only the convention, --boot-filename and a -p/--bootstrap header can
 * both say otherwise, and a check that assumed the convention would pass discs
 * that do not boot and fail discs that do.
 *
 * The root and nowhere else. The console looks the name up in the root
 * directory, so a boot binary one directory down is not a boot binary. */
void check_boot_file(Report& report, const Volume& volume, const IPBin* header) {
    const std::vector<uint8_t>& iso = *volume.data;

    std::string declared = trim_ipbin_field(header->meta.boot_filename,
        sizeof(header->meta.boot_filename));

    /* Already reported as unusable by check 1, and there is no name here to
     * look for. */
    if(!is_usable_boot_filename(declared)) {
        return;
    }

    /* No answer rather than a negative one. The reader is a deliberately plain
     * ECMA-119 one, so a volume it cannot follow is not necessarily a broken
     * volume -- and refusing to build over "I could not read it" would reject
     * discs that boot. Said as a warning, which is what the strength of the
     * evidence supports. */
    if(!iso9660::locate_root_directory(iso).found) {
        report.warn(volume.name + " has an ISO9660 volume this cannot read the root directory "
            "of, so whether it carries \"" + declared + "\" at its root -- the file the console "
            "looks up to start the disc -- has not been checked.");
        return;
    }

    std::vector<iso9660::Entry> entries = iso9660::read_root_directory(iso);

    for(const auto& entry: entries) {
        if(entry.name != declared) {
            continue;
        }

        if(entry.is_dir) {
            report.fail(volume.name + " has a directory called \"" + declared + "\" at its root, "
                "where the header says the boot file is. The console cannot start a directory. "
                "The disc will not boot.");
        }

        return;
    }

    /* Found, but not under the name the header gave. An ISO9660 primary tree
     * holds upper-case names -- it is the tree the console reads at boot, and
     * every writer uppercases into it -- so a header naming the file in any
     * other case names something that tree does not contain.
     *
     * A warning and not a refusal, on two grounds: whether the console's own
     * lookup is case-sensitive is not documented anywhere consulted here, and
     * mkdcdisc reaches this state through an option it supports -- pass
     * --boot-filename a lower-case name and the header keeps the case while
     * the filesystem uppercases it. Refusing would break that option; saying
     * nothing would hide the one difference that explains a disc which builds
     * cleanly and then does not boot. */
    for(const auto& entry: entries) {
        if(entry.is_dir || !same_name_ignoring_case(entry.name, declared)) {
            continue;
        }

        report.warn(volume.name + " declares the boot file as \"" + declared + "\" at IP.BIN "
            "offset " + as_ipbin_offset(0x060) + ", but its filesystem calls it \"" + entry.name
            + "\". ISO9660 upper-cases the names in the tree the console reads at boot, so the "
            "two differ only in case. If the disc does not boot, this is the first thing to "
            "change.");
        return;
    }

    report.fail(volume.name + " does not carry \"" + declared + "\" at the root of its "
        "filesystem, which is the file the header names at IP.BIN offset " + as_ipbin_offset(0x060)
        + " and the only thing the console will try to start. Its root holds "
        + root_contents_summary(entries) + ". The disc will not boot.");
}

/* How far a volume may sit from a computed MSINFO before it is called wrong.
 *
 * One CD pregap, which is the one quantity two mastering models legitimately
 * disagree about when they say where a session starts: some count the 150
 * sectors between sessions as the tail of the one before, some as the head of
 * the one after. Within that, a difference says the ISO was built by a tool
 * that accounts for the gap differently from mkdcdisc; beyond it, the ISO was
 * built for a different disc than the one being made, and every address in it
 * is that far out.
 *
 * It buys latitude in one direction only, and deliberately: a strict-zero
 * expectation gets none, because a volume that starts the disc starts at
 * sector 0 and there is nothing to model. */
const long long MSINFO_TOLERANCE_SECTORS = 150;

/* Check 3: the volume was built for the place on the disc it is going to sit.
 *
 * An ISO carries absolute addresses, so `mkisofs -C 0,N` has to be told where
 * the session will start before it writes any of them -- which means the value
 * is chosen by hand, from a guide or from `mkdcdisc -M`, and nothing has ever
 * checked that the choice matched the disc being built. It is the one check
 * here that reads the filesystem rather than the boot header.
 *
 * A warning, never a refusal, whichever way it fails: mkdcdisc's own figure is
 * a model of where the session lands and not a measurement of it, and a build
 * refused on the strength of a model is a build refused on this file's opinion.
 * The user is told what was expected, what the ISO says, and how to get the
 * right number. */
void check_msinfo(Report& report, const Volume& volume) {
    if(!volume.msinfo) {
        return;
    }

    const uint32_t expected = volume.msinfo->sectors;
    auto detected = iso9660::detect_msinfo(*volume.data);

    if(!detected) {
        report.warn(volume.name + " has an ISO9660 volume whose root directory this cannot "
            "follow, so the sector it was built to start at -- which should be "
            + std::to_string(expected) + " -- has not been checked.");
        return;
    }

    if(*detected == expected) {
        return;
    }

    const long long drift = (long long) *detected - (long long) expected;
    const long long distance = drift < 0 ? -drift : drift;

    if(!volume.msinfo->exact && distance <= MSINFO_TOLERANCE_SECTORS) {
        logging::debug() << volume.name << " was built for sector " << *detected << " against an "
            "expected " << expected << ", within the " << MSINFO_TOLERANCE_SECTORS
            << "-sector pregap allowance";
        return;
    }

    std::string said = volume.name + " was built to start at sector " + std::to_string(*detected)
        + ", but it will be written at sector " + std::to_string(expected) + " ("
        + (drift > 0 ? "+" : "") + std::to_string(drift) + "). Every address recorded inside it "
        "is that far out, so the console will read each file from the wrong place.";

    if(expected == 0) {
        said += " A first session starts at sector 0: build the ISO with mkisofs -C 0,0, or "
            "without -C at all.";
    } else {
        said += " Pass mkisofs -C 0," + std::to_string(expected)
            + "; mkdcdisc -M prints that number for the options this disc is being built with.";
    }

    report.warn(said);
}

/* Check 4: the SEGA licence screen code is the licence screen code.
 *
 * The strongest check here, and the only one resting on a documented hardware
 * fact rather than on a convention: the boot ROM holds its own copy of these
 * 13312 bytes and compares the disc's against it byte for byte, refusing to
 * boot on any difference at all. So this is a comparison rather than a
 * checksum -- the reference bytes are right here, a checksum would only be a
 * weaker way of asking the same question, and comparing directly is what makes
 * it possible to say *where* the difference is.
 *
 * The zone either side of it is deliberately left alone: the table of contents
 * below it is free-form, and the two bootstraps above it are modifiable by
 * design -- which is exactly what mkdcdisc does to them when it places the MR
 * licence image and picks an FPU rounding mode. */
void check_license_code(Report& report, const Volume& volume) {
    const std::vector<uint8_t>& iso = *volume.data;

    const uint8_t* found = iso.data() + IPBIN_LICENSE_CODE_OFFSET;
    const unsigned char* expected = ipbin_license_code();

    if(std::memcmp(found, expected, IPBIN_LICENSE_CODE_LENGTH) == 0) {
        return;
    }

    size_t at = 0;
    while(at < IPBIN_LICENSE_CODE_LENGTH && found[at] == expected[at]) {
        at += 1;
    }

    size_t differing = 0;
    for(size_t i = 0; i < IPBIN_LICENSE_CODE_LENGTH; ++i) {
        if(found[i] != expected[i]) {
            differing += 1;
        }
    }

    report.fail(volume.name + " carries a modified SEGA licence screen code: "
        + std::to_string(differing) + " of the " + std::to_string(IPBIN_LICENSE_CODE_LENGTH)
        + " bytes at IP.BIN offsets " + as_ipbin_offset(IPBIN_LICENSE_CODE_OFFSET) + "-"
        + as_ipbin_offset(IPBIN_LICENSE_CODE_OFFSET + IPBIN_LICENSE_CODE_LENGTH - 1)
        + " differ, the first at offset " + as_ipbin_offset(IPBIN_LICENSE_CODE_OFFSET + at)
        + ". The boot ROM compares that whole zone against a copy of its own and refuses any "
        "disc that does not match it exactly, so this one will not boot on any console. It is "
        "not a zone an IP.BIN may customize.");
}

}

bool check_disc(const std::vector<Volume>& volumes) {
    ProgressLine checking("Checking boot prerequisites...");
    Report report;

    for(const auto& volume: volumes) {
        if(volume.data == nullptr) {
            continue;
        }

        const IPBin* header = boot_header(*volume.data);
        bool has_header = check_boot_header(report, volume, header);

        /* Asked of every volume, the never-booted one included: where a volume
         * sits is a property of the filesystem rather than of the boot header,
         * and session 1's files are read even though session 1 is not booted.
         * A volume built for the wrong sector fails to deliver them either
         * way. */
        check_msinfo(report, volume);

        /* Every check below reads the header, so there is nothing to say about
         * a volume that has none beyond what has already been said. Restating
         * it once per check would bury the one line that matters. */
        if(!has_header || volume.role != Role::Bootable) {
            continue;
        }

        check_boot_file(report, volume, header);
        check_license_code(report, volume);
    }

    if(report.overridden() > 0) {
        report.note(std::to_string(report.overridden()) + " boot prerequisite check(s) failed. "
            "Building anyway because --disable-bootstrap-checks was given; the disc is "
            "unlikely to boot.");
    }

    if(!report.ok()) {
        /* A note rather than a third Error line: what is wrong has been said
         * already, and the caller closes with a refusal of its own. All that
         * is left to add is the way out, which is not itself a failure.
         *
         * Printed whatever -v says, unlike the warnings above, and for the same
         * reason print_help_hint() is: it is an aside attached to an error, so
         * it only ever appears where output was going to be produced anyway.
         * Silencing it would leave a -q run told that its disc will not boot
         * and not told that it can say otherwise. */
        std::cerr << term::note << "pass --disable-bootstrap-checks to build this disc "
            "anyway." << std::endl;
        return false;
    }

    checking.finish();
    return true;
}

}
