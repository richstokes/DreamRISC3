/* mkdcdisc - Sega Dreamcast Disc Image Generator
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2022-2026 Simulant and contributors
 * (https://www.simulant.dev/) */

/* Audio file decoding: see audio.hpp. */

#include <cstring>
#include <fstream>
#include <iostream>

#include "audio.hpp"
#include "logging.hpp"
#include "terminal.hpp"

std::optional<std::vector<uint8_t>> wav_to_cdda(const std::string& filename) {
    std::vector<uint8_t> data;

    std::ifstream file(filename, std::ios::binary);
    if(!file.good()) {
        std::cerr << term::error << "couldn't load .wav file: " << filename << std::endl;
        return std::optional<std::vector<uint8_t>>();
    }

    char buffer[4];

    file.read(buffer, 4);
    if(std::strncmp(buffer, "RIFF", 4) != 0) {
        std::cerr << term::error << "not a valid .wav file: " << filename << std::endl;
        return std::optional<std::vector<uint8_t>>();
    }

    file.seekg(4, std::ios_base::cur); // file length
    file.read(buffer, 4);

    if(std::strncmp(buffer, "WAVE", 4) != 0) {
        std::cerr << term::error << "not a valid .wav file: " << filename << std::endl;
        return std::optional<std::vector<uint8_t>>();
    }

    while(!file.eof()) {
        file.read(buffer, 4);  // Chunk id

        logging::debug() << "RIFF chunk '" << buffer[0] << buffer[1] << buffer[2] << buffer[3] << "'";

        if(std::strncmp(buffer, "fmt ", 4) == 0) {

            /* Check the format */
            struct Format {
                uint32_t length;
                uint16_t type;
                uint16_t channels;
                uint32_t frequency;
                uint32_t rate;
                uint16_t bytes_per_sample;
                uint16_t bitrate;
            } format;

            file.read((char*) &format, sizeof(format));

            logging::debug() << "  " << format.channels << " channel(s), " << format.frequency
                             << " Hz, " << format.bitrate << "-bit, " << format.length
                             << "-byte format chunk";

            if(format.frequency != 44100 || format.channels != 2 || format.bitrate != 16) {
                std::cerr << term::error << "unsupported .wav format. Must be stereo, 44100hz, and 16 bit samples.: " << filename << std::endl;
                return std::optional<std::vector<uint8_t>>();
            }

        } else if(std::strncmp(buffer, "data", 4) == 0) {

            logging::debug() << "  Data chunk";

            /* Read the data */
            uint32_t length;
            file.read((char*) &length, sizeof(length));
            data.resize(length);
            file.read((char*) &data[0], length);
            break;

        } else {
            uint32_t skip;
            file.read((char*) &skip, sizeof(skip));
            file.seekg(skip, std::ios_base::cur);

            logging::debug() << "  Skipped " << skip << " bytes";
            continue;
        }
    }

    return data;
}

static bool is_wav_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if(!file.good()) {
        return false;
    }
    char buffer[4] = {0};
    file.read(buffer, 4);
    return std::strncmp(buffer, "RIFF", 4) == 0;
}

/* Load a raw CD-DA track (headerless 44100 Hz, 16-bit, stereo, little-endian PCM).
 * The bytes are already in CD-DA sector form (2352 bytes/sector), so we just read
 * them verbatim. A file that stops mid-sector is still usable -- every writer
 * fills the last sector out with silence -- so the size check below is a note,
 * not a warning, and never refuses the track. It stays on stderr all the same:
 * stdout belongs to -M/--print-msinfo, which a script captures. */
std::optional<std::vector<uint8_t>> raw_to_cdda(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if(!file.good()) {
        std::cerr << term::error << "couldn't load raw audio file: " << filename << std::endl;
        return std::optional<std::vector<uint8_t>>();
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size > 0 ? size : 0);
    if(size > 0 && !file.read(reinterpret_cast<char*>(&data[0]), size)) {
        std::cerr << term::error << "couldn't read raw audio file: " << filename << std::endl;
        return std::optional<std::vector<uint8_t>>();
    }

    const size_t remainder = (size > 0) ? (size_t) size % CDDA_SECTOR_SIZE : 0;
    if(remainder && logging::enabled(logging::Level::Normal)) {
        std::cerr << term::note << "raw audio file " << filename
                  << " does not end on a sector boundary; padding the last one with "
                  << (CDDA_SECTOR_SIZE - remainder) << " bytes of silence." << std::endl;
    }

    return data;
}

/* Accept either a .wav file or a headerless raw CD-DA file, detected by content
 * (presence of a RIFF header) rather than by extension. */
std::optional<std::vector<uint8_t>> load_audio_track(const std::string& filename) {
    if(is_wav_file(filename)) {
        return wav_to_cdda(filename);
    }

    logging::verbose() << "Treating " << filename << " as raw CD-DA (no RIFF header found)";
    return raw_to_cdda(filename);
}
