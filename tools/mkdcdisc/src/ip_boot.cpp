/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Dreamcast boot header handling: see ip_boot.hpp. */

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>

#include "cli.hpp"
#include "ip_boot.hpp"
#include "logging.hpp"
#include "mr_image.hpp"
#include "progress.hpp"
#include "terminal.hpp"
#include "third_party/makeip/crc.hpp"
#include "version.h"

/* The default IP.BIN file for MIL-CD (CD-ROM) images */
#include "assets/ip_cd.include"

/* GD-ROM bootstrap program: the high-density boot structure, which carries the
 * TOC and sits at the start of the high-density data track. Its second-stage
 * bootstrap differs from the MIL-CD one above.
 *
 * Two of them, one per FPU rounding mode -- see gd_hda_template(). They are
 * identical but for three short runs inside bootstrap2, where the rounding
 * mode is set; the 256-byte header is the same in both.
 *
 * The low-density counterpart (IP0000.BIN) is not bundled: it shares only that
 * header, which build_gd.cpp copies from whichever of these is in use. */
#include "assets/ip_gd.hda.nearest.include"
#include "assets/ip_gd.hda.zero.include"

/* The default MR license image, which reads: "... is what official games
 * display, but this is 100% unofficial and unlicensed." */
#include "assets/iplogo.default.include"

std::string gd_fpu_rounding() {
    return OPTS.count("gdi-fpu-rounding") && OPTS["gdi-fpu-rounding"][0] == "zero"
        ? "zero" : "nearest";
}

const unsigned char* gd_hda_template() {
    return gd_fpu_rounding() == "zero" ? IP_BIN_GDROM_HDA_ZERO : IP_BIN_GDROM_HDA_NEAREST;
}

size_t gd_hda_template_len() {
    return gd_fpu_rounding() == "zero" ? IP_BIN_GDROM_HDA_ZERO_len : IP_BIN_GDROM_HDA_NEAREST_len;
}

const char* const IPBIN_HARDWARE_ID = "SEGA SEGAKATANA ";
const char* const IPBIN_MAKER_ID = "SEGA ENTERPRISES";

/* Taken from the struct rather than written out again, so that the zone the
 * checks compare is the same one the templates are laid out with. Both are
 * the values in the layout table: 0300-36FF. */
const size_t IPBIN_LICENSE_CODE_OFFSET = offsetof(IPBin, license_screen);
const size_t IPBIN_LICENSE_CODE_LENGTH = sizeof(IPBin::license_screen);

const unsigned char* ipbin_license_code() {
    static_assert(offsetof(IPBin, license_screen) == 0x300,
        "the licence screen code starts at IP.BIN offset 0300");
    static_assert(sizeof(IPBin::license_screen) == 0x3700 - 0x300,
        "the licence screen code runs to IP.BIN offset 36FF");

    /* Any of the three templates would do -- the zone is byte-identical in all
     * of them -- so the one that does not depend on an option is the one to
     * read it from.
     *
     * Nothing asserts that here, and nothing needs to: once the checks run
     * over mkdcdisc's own output, a template whose licence code drifted from
     * this one would fail its own build's check, naming the offset it drifted
     * at. The invariant is held up by the thing that depends on it. */
    return IP_BIN_MILCD + IPBIN_LICENSE_CODE_OFFSET;
}

std::string trim_ipbin_field(const char* data, size_t max_len) {
    size_t len = 0;
    while(len < max_len && data[len] != '\0') {
        ++len;
    }

    std::string s(data, len);
    while(!s.empty() && s.back() == ' ') {
        s.pop_back();
    }

    return s;
}

/* Warns (without rejecting or truncating here -- callers still pad/truncate
 * on their own) when a user-supplied IP.BIN field is longer than the fixed-
 * size field it will be copied into. */
static void warn_if_ipbin_field_truncated(const std::string& option, const std::string& value, std::size_t max_len) {
    if(value.size() > max_len) {
        std::cerr << term::warning << option << " is " << value.size() << " characters, exceeds the "
            << max_len << "-character IP.BIN limit; it will be truncated." << std::endl;
    }
}

/* A name as an ISO9660 primary tree will hold it. ECMA-119's d-characters are
 * A-Z, 0-9 and _, so libisofs upper-cases every name on its way into that tree
 * -- the tree the console reads at boot. This is therefore not a preference but
 * the only form of the name that exists there to be looked up. */
static std::string as_iso9660_case(const std::string& name) {
    std::string upper = name;

    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char c) { return (char) std::toupper(c); });

    return upper;
}

/* Writes `name` into the header's Boot Filename field, upper-cased and
 * space-padded like every other field. No CRC to refresh: the device_info
 * checksum spans the product number and version only (see update_ipbin_crc()).
 *
 * Upper-cased here rather than at the name's source, so that this field is the
 * only thing that changes. The file itself keeps the case it was given, which
 * is what Joliet and Rock Ridge then carry -- and those are the names
 * KallistiOS resolves at runtime, so a program that opens its own boot binary,
 * or anything beside it, still finds the name its author wrote. Only the
 * primary tree upper-cases, and this field is the one that has to match the
 * primary tree, because the primary tree is what the console reads at boot.
 *
 * So a --boot-filename game.bin gives a disc holding game.bin under Joliet and
 * Rock Ridge, GAME.BIN in the primary tree, and GAME.BIN declared here. All
 * three name the same file; each says it the way its own reader spells it. */
static void set_ipbin_boot_filename(IPBin* ip_bin, const std::string& name) {
    const std::string declared = as_iso9660_case(name);

    warn_if_ipbin_field_truncated("--boot-filename", declared, sizeof(ip_bin->meta.boot_filename));

    if(declared != name) {
        logging::verbose() << "IP.BIN declares the boot file as " << declared << ", the name the "
            "ISO9660 primary tree holds it under; the file itself keeps its case (" << name << ")";
    }

    std::memset(ip_bin->meta.boot_filename, ' ', sizeof(ip_bin->meta.boot_filename));
    std::memcpy(ip_bin->meta.boot_filename, declared.c_str(),
        std::min(declared.size(), sizeof(ip_bin->meta.boot_filename)));
}

/* Checks for a plausible YYYYMMDD calendar date: 8 digits, month 01-12, and
 * a day valid for that month (including leap years). */
static bool is_valid_release_date(const std::string& date) {
    if(date.size() != 8 || !std::all_of(date.begin(), date.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return false;
    }

    int year = std::stoi(date.substr(0, 4));
    int month = std::stoi(date.substr(4, 2));
    int day = std::stoi(date.substr(6, 2));

    if(month < 1 || month > 12) {
        return false;
    }

    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    int max_day = (month == 2 && leap_year) ? 29 : days_in_month[month - 1];

    return day >= 1 && day <= max_day;
}

/* Checks the Peripherals field's 7-character format, rewritten after makeip's
 * _check_peripherals(): 5 hex digits packing the controller/peripheral
 * requirement bits, then two strict '0'/'1' flags for VGA support and
 * Windows CE use (in that order -- e.g. the common default "E000F10" is VGA
 * on, Windows CE off). */
static bool is_valid_peripherals(const std::string& value) {
    if(value.size() != 7) {
        return false;
    }

    bool hex_part_valid = std::all_of(value.begin(), value.begin() + 5,
        [](unsigned char c) { return std::isxdigit(c); });
    bool vga_flag_valid = value[5] == '0' || value[5] == '1';
    bool wince_flag_valid = value[6] == '0' || value[6] == '1';

    return hex_part_valid && vga_flag_valid && wince_flag_valid;
}

/* What the two free-text metadata fields say when nothing was asked for.
 * Named here because the build plan has to report them before generate_ip_bin()
 * writes them. */
static const char* const DEFAULT_GAME_NAME = "Untitled Game";
static const char* const DEFAULT_COMPANY_NAME = "Unknown Author";

/* Today, in the YYYYMMDD form the Release Date field takes -- or the day
 * SOURCE_DATE_EPOCH names, read in UTC, so that a reproducible build does not
 * put the wall clock into the header. */
static std::string today_as_release_date() {
    char generated[9];

    if(auto epoch = source_date_epoch()) {
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &*epoch);
#else
        gmtime_r(&*epoch, &utc);
#endif
        std::strftime(generated, sizeof(generated), "%Y%m%d", &utc);
        return generated;
    }

    std::time_t t = std::time(NULL);
    std::strftime(generated, sizeof(generated), "%Y%m%d", std::localtime(&t));

    return generated;
}

static std::string generate_product_number(const std::vector<uint8_t>& data) {
    const std::string prefix = "IND-";
    const std::string raw(data.begin(), data.end());

    auto hash = std::hash<std::string>()(raw);
    return prefix + std::to_string(hash).substr(0, 6);
}

bool load_ip_bin(const std::filesystem::path& ipbin_path, IPBin* out) {
    const std::streamsize MAX_IPBIN_SIZE = 32768;

    if(!std::filesystem::exists(ipbin_path)) {
        std::cerr << term::error << "IP.BIN file does not exist: " << ipbin_path << std::endl;
        return false;
    }

    std::ifstream file;
    std::streamsize file_size = 0;

    file.open(ipbin_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << term::error << "unable to open IP.BIN file: " << ipbin_path << std::endl;
        return false;
    }

    file_size = file.tellg();
    if (file_size > MAX_IPBIN_SIZE) {
        std::cerr << term::error << "IP.BIN file size exceeds the maximum allowed size of " 
            << MAX_IPBIN_SIZE << " bytes. File size is " << file_size << " bytes." 
            << std::endl;
        file.close();
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(out), file_size)) {
        std::cerr << term::error << "unable to read IP.BIN file: " << ipbin_path << std::endl;
        file.close();
        return false;
    }
    file.close();

    return true;
}

/* Recomputes the CRC over meta's 16-byte product_number+product_version span
 * and writes it as 4 uppercase ASCII hex digits into the first 4 bytes of
 * meta.device_info -- the "0000" placeholder the templates ship with (e.g.
 * "0000 GD-ROM1/1", see makeip's field.c). Must run after those two fields
 * are at their final, options-driven values.
 *
 * The checksum itself is makeip's calc_crc(); which bytes it covers, and where
 * the four digits land, is this project's own reading of the header layout --
 * upstream's update_crc() works on raw offsets into a 32K buffer instead. */
static void update_ipbin_crc(IPBin* ip_bin) {
    static_assert(offsetof(decltype(ip_bin->meta), product_version)
        == offsetof(decltype(ip_bin->meta), product_number) + sizeof(ip_bin->meta.product_number),
        "product_number/product_version must be adjacent for the CRC span");

    const size_t crc_span = sizeof(ip_bin->meta.product_number) + sizeof(ip_bin->meta.product_version);
    uint16_t crc = makeip::calc_crc((const uint8_t*) ip_bin->meta.product_number, crc_span);

    char hex[5];
    std::snprintf(hex, sizeof(hex), "%04X", crc);
    std::memcpy(ip_bin->meta.device_info, hex, 4);
}

/* template_data/template_len select which 32K boot image to start from: the
 * default IP_BIN_MILCD for CDI/MDS/NRG, or the GD-ROM high-density boot image
 * gd_hda_template() picks for -F gdi. Everything layered on top -- MR license
 * image, metadata fields -- is identical regardless. */
bool generate_ip_bin(const std::filesystem::path& output_dir, const std::filesystem::path& bin_path, IPBin* out,
                     const unsigned char* template_data, size_t template_len) {
    std::ifstream bin(bin_path.string());
    bin.seekg(0, std::ios::end);
    auto size = bin.tellg();
    std::vector<uint8_t> bin_data;
    bin_data.resize(size);
    bin.seekg(0, std::ios::beg);
    bin.read((char*) &bin_data[0], size);
    bin.close();

    IPBin ip_bin;
    uint8_t* mr_image = ((uint8_t*) &ip_bin) + 0x3820;

    std::copy(template_data, template_data + template_len, (char*) &ip_bin);

    if(OPTS.count("disable-bootstrap-mr") && OPTS.count("bootstrap-mr-image")) {
        std::cerr << term::warning << "-i/--bootstrap-mr-image is ignored because "
            "-m/--disable-bootstrap-mr was given." << std::endl;
    }

    if(!OPTS.count("disable-bootstrap-mr")) {
        if(OPTS.count("bootstrap-mr-image")) {
            std::string image_path = OPTS["bootstrap-mr-image"][0];
            const size_t MAX_IMAGE_SIZE = 8192;

            bool valid_image = true;
            std::vector<uint8_t> image_data;

            if(!std::filesystem::exists(image_path)) {
                std::cerr << term::error << "image file does not exist: " << image_path << std::endl;
                valid_image = false;
            }

            ImageFormat format = valid_image ? detect_image_format(image_path) : ImageFormat::Unknown;

            if(valid_image && format == ImageFormat::Mr) {
                std::ifstream file(image_path, std::ios::binary | std::ios::ate);
                if(!file) {
                    std::cerr << term::error << "unable to open MR file: " << image_path << std::endl;
                    valid_image = false;
                } else {
                    std::streamsize file_size = file.tellg();
                    if((size_t) file_size > MAX_IMAGE_SIZE) {
                        std::cerr << term::error << "MR file size exceeds the maximum allowed size of "
                                << MAX_IMAGE_SIZE << " bytes. File size is " << file_size << " bytes."
                                << std::endl;
                        valid_image = false;
                    } else {
                        image_data.resize(file_size);
                        file.seekg(0, std::ios::beg);
                        if(!file.read((char*) image_data.data(), file_size)) {
                            std::cerr << "Unable to read MR file: " << image_path << std::endl;
                            valid_image = false;
                        }
                    }
                }
            } else if(valid_image && format == ImageFormat::Png) {
#ifdef MKDCDISC_WITH_PNG
                auto mr = convert_png_to_mr(image_path);
                if(!mr) {
                    valid_image = false;
                } else if(mr->size() > MAX_IMAGE_SIZE) {
                    std::cerr << term::error << "converted MR data from " << image_path << " exceeds the maximum "
                        "allowed size of " << MAX_IMAGE_SIZE << " bytes (" << mr->size()
                        << " bytes)." << std::endl;
                    valid_image = false;
                } else {
                    image_data = std::move(*mr);
                }
#else
                std::cerr << term::error << image_path << " is a PNG file, but this build of "
                    "mkdcdisc was compiled without PNG support. Convert it to MR format first, "
                    "or use a build with PNG support." << std::endl;
                valid_image = false;
#endif
            } else if(valid_image) {
                std::cerr << term::error << "unsupported or unreadable image file: " << image_path << std::endl;
                valid_image = false;
            }

            if(valid_image) {
                std::copy(image_data.begin(), image_data.end(), mr_image);
            } else {
                logging::note() << "Using the default MR image instead.";
                std::copy(IP_BIN_DEFAULT_MR, IP_BIN_DEFAULT_MR + IP_BIN_DEFAULT_MR_len, mr_image);
            }
        } else {
            std::copy(IP_BIN_DEFAULT_MR, IP_BIN_DEFAULT_MR + IP_BIN_DEFAULT_MR_len, mr_image);
        }
    }

    auto pad = [](std::string s, std::size_t x) -> std::string {
        while(s.size() < x) {
            s += " ";
        }
        return s;
    };

    /* The same answers the -v 3 build plan reported before any of this ran. */
    IPBinMetadata plan = planned_ip_bin_metadata(template_data, template_len);

    std::string name = plan.title;
    warn_if_ipbin_field_truncated("-n/--title", name, 128);
    name = pad(name, 128);
    std::strncpy(ip_bin.meta.game_name, name.c_str(), 128);

    std::string company = plan.author;
    warn_if_ipbin_field_truncated("-a/--author", company, 16);
    company = pad(company, 16);
    std::strncpy(ip_bin.meta.company_name, company.c_str(), 16);

    /* Must match the actual name gather_files() gives the executable on the
     * disc -- this is the name the console looks up at boot time, and, for
     * -F gdi, the name build_hda_iso() looks for to pin the boot binary to
     * the end of the high-density area. */
    set_ipbin_boot_filename(&ip_bin, boot_filename());

    std::string release = plan.release_date;
    if(OPTS.count("release") && !is_valid_release_date(release)) {
        std::cerr << term::warning << "-r/--release value '" << release << "' is not a valid YYYYMMDD "
            "date; using it as-is." << std::endl;
    }
    warn_if_ipbin_field_truncated("-r/--release", release, 16);
    release = pad(release, 16);
    std::strncpy(ip_bin.meta.release_date, release.c_str(), 16);

    /* An unset -s/--serial is the one thing the plan cannot answer: its
     * default is a hash of the very bytes read at the top of this function. */
    std::string product_number = plan.serial.empty() ? generate_product_number(bin_data) : plan.serial;
    warn_if_ipbin_field_truncated("-s/--serial", product_number, 10);
    product_number = pad(product_number, 10);
    std::strncpy(ip_bin.meta.product_number, product_number.c_str(), 10);

    /* Leave the template IP.BIN's own bytes untouched unless overridden --
     * unlike product_number/release, there's no sensible generated default
     * for a version string, and the plan reports the template's value here
     * precisely because nothing rewrites it. */
    if(OPTS.count("product-version")) {
        std::string product_version = plan.product_version;
        warn_if_ipbin_field_truncated("-P/--product-version", product_version, 6);
        product_version = pad(product_version, 6);
        std::strncpy(ip_bin.meta.product_version, product_version.c_str(), 6);
    }

    /* Leave the template's own value untouched unless overridden, same as
     * product-version: there's no sensible generated default for a
     * peripherals bitfield either. */
    if(OPTS.count("peripherals")) {
        std::string peripherals = plan.peripherals;
        if(!is_valid_peripherals(peripherals)) {
            std::cerr << term::warning << "--peripherals value '" << peripherals << "' is not a valid "
                "peripherals bitfield (expected 5 hex digits followed by two '0'/'1' flags, e.g. "
                "'E000F10'); using it as-is." << std::endl;
        }
        peripherals = pad(peripherals, 8);
        std::strncpy(ip_bin.meta.peripherals, peripherals.c_str(), 8);
    }

    update_ipbin_crc(&ip_bin);

    *out = ip_bin;
    return true;
}

/* What the executable is called when nothing says otherwise: the name every
 * reference dump, and every mkdcdisc release so far, has used. */
static const char* const DEFAULT_BOOT_FILENAME = "1ST_READ.BIN";

bool is_usable_boot_filename(const std::string& name) {
    /* Anything other than a bare file name would send gather_files() writing
     * the executable outside the staging directory -- and, on a disc that
     * already exists, names something the console cannot look up at the root
     * of the filesystem, which is the only place it looks. */
    return !name.empty() && name != "." && name != ".."
        && std::filesystem::path(name).filename().string() == name;
}

/* The -p/--bootstrap file, read once however many callers ask after it. NULL
 * when there is none, or when it could not be read -- load_ip_bin() reports
 * that at the first attempt, and every caller after it simply sees "no custom
 * IP.BIN", so the fallback is the same one everywhere. */
static const IPBin* custom_ip_bin() {
    static bool tried = false;
    static bool loaded = false;
    static IPBin ip_bin;

    if(!tried) {
        tried = true;
        loaded = OPTS.count("bootstrap") && load_ip_bin(OPTS["bootstrap"][0], &ip_bin);
    }

    return loaded ? &ip_bin : nullptr;
}

std::string boot_filename() {
    static const std::string resolved = [] {
        /* An explicit --boot-filename is the user's own word on the subject
         * and outranks everything, a -p/--bootstrap header included:
         * prepare_ip_bin() rewrites that header's field to match, so the two
         * still name the same file. */
        if(OPTS.count("boot-filename")) {
            return OPTS["boot-filename"][0];
        }

        const IPBin* custom = custom_ip_bin();
        if(!custom) {
            return std::string(DEFAULT_BOOT_FILENAME);
        }

        std::string declared = trim_ipbin_field(custom->meta.boot_filename,
            sizeof(custom->meta.boot_filename));

        if(!is_usable_boot_filename(declared)) {
            std::cerr << term::warning << "the -p/--bootstrap IP.BIN declares "
                      << (declared.empty() ? std::string("no boot file name")
                                           : "an unusable boot file name (\"" + declared + "\")")
                      << ", so the executable is written as " << DEFAULT_BOOT_FILENAME
                      << " instead. Use --boot-filename to name it yourself. The disc is "
                         "unlikely to boot." << std::endl;
            return std::string(DEFAULT_BOOT_FILENAME);
        }

        return declared;
    }();

    return resolved;
}

std::string ipbin_title() {
    if(const IPBin* custom = custom_ip_bin()) {
        return trim_ipbin_field(custom->meta.game_name, sizeof(custom->meta.game_name));
    }

    return OPTS.count("title") ? OPTS["title"][0] : DEFAULT_GAME_NAME;
}

/* See ip_boot.hpp. Deliberately silent: it is asked the same questions
 * generate_ip_bin() then asks, so a warning raised here would be printed twice
 * -- once for the plan, once for the header it describes. The warnings stay
 * where the fields are actually written. */
IPBinMetadata planned_ip_bin_metadata(const unsigned char* template_data, size_t template_len) {
    IPBinMetadata plan;

    /* The one field a -p/--bootstrap file does not have the last word on:
     * --boot-filename overrides what it declares, and prepare_ip_bin() then
     * rewrites the header to match. boot_filename() already knows all that. */
    plan.boot_filename = boot_filename();

    plan.title = ipbin_title();

    if(const IPBin* custom = custom_ip_bin()) {
        plan.from_custom_bootstrap = true;
        plan.author = trim_ipbin_field(custom->meta.company_name, sizeof(custom->meta.company_name));
        plan.serial = trim_ipbin_field(custom->meta.product_number, sizeof(custom->meta.product_number));
        plan.product_version = trim_ipbin_field(custom->meta.product_version, sizeof(custom->meta.product_version));
        plan.release_date = trim_ipbin_field(custom->meta.release_date, sizeof(custom->meta.release_date));
        plan.peripherals = trim_ipbin_field(custom->meta.peripherals, sizeof(custom->meta.peripherals));

        return plan;
    }

    /* The template is where the two fields with no generated default keep
     * their value, so reading them back out of it is not a guess at what
     * generate_ip_bin() will write -- it is what generate_ip_bin() leaves
     * exactly as it found it. */
    IPBin from_template{};
    std::copy(template_data, template_data + std::min(template_len, sizeof(IPBin)),
        (char*) &from_template);

    plan.author = OPTS.count("author") ? OPTS["author"][0] : DEFAULT_COMPANY_NAME;
    plan.serial = OPTS.count("serial") ? OPTS["serial"][0] : "";
    plan.release_date = OPTS.count("release") ? OPTS["release"][0] : today_as_release_date();

    plan.product_version = OPTS.count("product-version")
        ? OPTS["product-version"][0]
        : trim_ipbin_field(from_template.meta.product_version, sizeof(from_template.meta.product_version));

    plan.peripherals = OPTS.count("peripherals")
        ? OPTS["peripherals"][0]
        : trim_ipbin_field(from_template.meta.peripherals, sizeof(from_template.meta.peripherals));

    return plan;
}

/* Shared by the MIL-CD (build_cd) and GD-ROM (build_gd) paths so that a
 * -p/--bootstrap file behaves identically in every format. What the callers
 * layer on top is what differs: the GD-ROM path still has its device_info
 * patch and its TOC to apply, and has to decide how a custom file maps onto
 * its own layout. */
bool prepare_ip_bin(IPBin* out, const std::filesystem::path& input_dir,
                           const unsigned char* template_data, size_t template_len) {
    ProgressLine building("Building bootstrap...");

    if(const IPBin* custom = custom_ip_bin()) {
        *out = *custom;
        logging::note() << "Using the custom IP.BIN as given; the other IP.BIN options "
                           "are ignored.";

        /* The one exception to "used verbatim", because it is not a matter of
         * taste like the other fields: the name the header declares has to be
         * the name the file was actually written under, or the console looks
         * up something that is not on the disc. When --boot-filename decides
         * that name, the header follows it here. */
        if(OPTS.count("boot-filename")) {
            set_ipbin_boot_filename(out, boot_filename());

            if(logging::enabled(logging::Level::Normal)) {
                building.break_line();
                std::cerr << term::note << "--boot-filename overrides the boot file name declared by the "
                                           "-p/--bootstrap IP.BIN; the header now declares "
                          << boot_filename() << "." << std::endl;
            }
        }

        building.finish();
        return true;
    }

    if(OPTS.count("bootstrap")) {
        logging::note() << "Using the built-in IP.BIN instead.";
    }

    generate_ip_bin(input_dir, input_dir / boot_filename(), out, template_data, template_len);

    building.finish();
    return false;
}

/* Unlike the high-density IP.BIN, which the IPBin struct describes, a
 * low-density boot image is one 2048-byte sector repeated sixteen times: on
 * every reference dump checked its sixteen sectors are byte-identical, and
 * two discs differ only by their header, echoed sixteen times over (Namco and
 * 18 Wheeler differ in 41 header bytes and 41 * 16 = 656 bytes overall).
 *
 * The field names of IPBin are therefore meaningless past the first sector,
 * hence the byte-level view below. */
void replicate_lda_header(IPBin* ip_bin) {
    const size_t SECTOR = 2048;
    const size_t HEADER = sizeof(ip_bin->meta);

    uint8_t* bytes = (uint8_t*) ip_bin;
    for(size_t offset = SECTOR; offset + HEADER <= sizeof(IPBin); offset += SECTOR) {
        std::memcpy(bytes + offset, bytes, HEADER);
    }
}
