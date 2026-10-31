/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

#pragma once

/* Whether the disc about to be written meets the minimum a Dreamcast needs in
 * order to boot from it.
 *
 * mkdcdisc had no such step: a disc it could assemble was a disc it wrote, and
 * whether the console would do anything with it was found out on the console.
 * That is mostly fine while mkdcdisc builds the filesystem itself, and not at
 * all fine for -t/--external-data-track, where the ISO and the boot header
 * inside it were built by mkisofs from whatever the user handed it. The checks
 * exist for that case first, and are run over mkdcdisc's own output too --
 * where they should never fire, and where a regression that made them fire is
 * exactly the thing worth hearing about.
 *
 * What separates a blocking error from a warning here is the strength of what
 * is known, not how bad the consequence would be. A documented hardware fact
 * -- the boot ROM compares the licence screen code byte for byte and refuses
 * the disc on any difference -- is an error, because a disc that fails it does
 * not boot, full stop. A consistency check whose failure merely makes a broken
 * disc likely is a warning. --disable-bootstrap-checks turns the errors into
 * warnings and lets the build finish, for the user who knows better than this
 * file does.
 *
 * Nothing here decides what a disc's layout should be; it is told what each
 * volume is for and reports on what it finds. */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace boot_checks {

/* What a volume is for on the finished disc, which is what decides what is
 * required of it.
 *
 * Every disc mkdcdisc builds has exactly one volume the console boots from.
 * -L/--disc-layout merged-data is the only layout with a volume that is read
 * without ever being booted: its session 1 supplies files that session 2's
 * merged filesystem points at, and the console never looks at its boot header
 * even when it has one. */
enum class Role {
    Bootable,
    NotBooted,
};

/* Where on the disc a volume is going to sit, which its own recorded addresses
 * have to have been written for. A volume built for the wrong place is not
 * broken in any way a reader can see: its filesystem is perfectly well formed
 * and every address in it points somewhere else than where the file is.
 *
 * `exact` is what separates the two ways of knowing the figure. A volume that
 * starts the disc starts at sector 0, and there is nothing to compute or to
 * disagree about, so anything else is wrong. A volume that follows another
 * session starts wherever that session ended, which is a figure mkdcdisc works
 * out from lead-in, lead-out and pregap lengths -- so a volume a little either
 * side of it was plausibly built by a tool that accounts for those slightly
 * differently, rather than built for the wrong disc. */
struct MsinfoExpectation {
    uint32_t sectors = 0;
    bool exact = true;
};

/* One volume of the disc, as the bytes of its ISO9660 filesystem plus what
 * those bytes are for. `name` is how the diagnostics will refer to it, so it
 * reads as the thing the user handed over ("the external ISO", "the session 2
 * ISO") rather than as an index into something they cannot see.
 *
 * `msinfo` empty means the question does not arise for this volume, and is not
 * the same as expecting zero. */
struct Volume {
    std::string name;
    Role role = Role::Bootable;
    const std::vector<uint8_t>* data = nullptr;
    std::optional<MsinfoExpectation> msinfo;
};

/* Runs every check over the volumes making up one disc, reports what it finds,
 * and answers whether the build may go ahead.
 *
 * False only when a blocking check failed and --disable-bootstrap-checks was
 * not given; warnings and notes never stop a build. */
bool check_disc(const std::vector<Volume>& volumes);

}
