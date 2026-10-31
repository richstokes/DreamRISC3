// MIT License

// Copyright (c) 2018 finixbit
// Copyright (c) 2022-2023 Colton Pawielski

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Recommended reading material:
// Tool Interface Stands (TIS) Portable Formats Specificaion
// Version 1.1 can be found here: https://www.cs.cmu.edu/afs/cs/academic/class/15213-f00/docs/elf.pdf

#include "elf_parser.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "logging.hpp"
#include "terminal.hpp"

namespace elfparser {

/**
 *  Selects the segments from an elf which should
 *  be loaded into memory and place them into a
 *  vector of bytes starting at 0x8c010000.
 */
bool Parser::fill_bin(std::vector<char>& bin_data)
{
    // remove non-loadable segments and ones that are not in the loadable region
    auto end_valid = std::remove_if(m_Segments.begin(), m_Segments.end(), 
        [](const Segment& segment) 
        { 
            // Can't load a segment before RAM start, some ELFs 
            // (often dcload & libronin) have a segment ".stack" that 
            // is before RAM start address which is not what we want
            bool before_ram = segment.get_physaddr() < DC_RAM_START_ADDR;
            // type of PT_LOAD signifies this should be loaded into memory
            bool is_loadable = segment.get_type() == PT_LOAD;
            // Some toolchains, LTO builds especially, emit a `LOAD` segment
            // for `.bss` carrying no file data at all. It has nothing to
            // contribute, and it has to be dropped here rather than skipped
            // when copying: its address sits past the last real byte, so
            // leaving it in the list would stretch the image out to it in
            // zeros
            bool is_empty = segment.get_filesize() == 0;
            return before_ram || !is_loadable || is_empty;
        }
    );
    auto seg_count = m_Segments.size();
    m_Segments.erase(end_valid, m_Segments.end());
    logging::debug() << "Dropped " << seg_count - m_Segments.size()
                     << " non-loadable segment(s), kept " << m_Segments.size();


    // Make sure at least one segment carries data or binary will be empty
    if (m_Segments.size() == 0) {
        std::cerr << term::error << "no loadable segments found!" << std::endl;
        return false;
    }

    // Get the beginning and the end of the loadable region to determine bin
    // size. The end is taken from p_filesz and not p_memsz: what a segment
    // occupies in RAM beyond its file image is `.bss`, which KOS zeroes at
    // startup before main() runs, so reserving it here would only pad
    // 1ST_READ.BIN with zeros. This is also what `objcopy -O binary` does, as
    // it emits sections and `.bss` is SHT_NOBITS with nothing to emit.
    // Both bounds are taken over every kept segment rather than from the
    // lowest- and highest-addressed one, so a link script where a lower
    // segment ends above another one cannot silently truncate the binary.
    size_t start = m_Segments.front().get_physaddr();
    size_t end = 0;
    for (const Segment& segment : m_Segments) {
        start = std::min(start, size_t(segment.get_physaddr()));
        end = std::max(end, size_t(segment.get_physaddr()) + segment.get_filesize());
    }

    // Binary should always start at DC_RAM_START_ADDR on Dreamcast
    if (start != DC_RAM_START_ADDR) {
        std::cerr << term::error << "unexpected binary start at 0x" << std::hex << start
                  << ", expected 0x" << DC_RAM_START_ADDR << std::dec << std::endl;
        return false;
    }

    logging::debug() << "Binary start: 0x" << std::hex << start
                     << "\tBinary end:   0x" << end
                     << "\tBinary size:  0x" << (end - start);

    if ( !bin_data.empty()) {
        std::cerr << term::error << "failed to empty binary vector!" << std::endl;
        return false;
    }
    // Vector should have enough space to hold all segments
    bin_data.resize(end - start);

    for (const Segment& segment : m_Segments) {
        // Offset from begining of binary
        auto offset = segment.get_physaddr() - start;
        // Get data from segment and copy it into the binary
        segment.write_data_to(&bin_data[offset]);
    }
    return true;
}

std::optional<std::shared_ptr<Parser>> Parser::Load(const std::vector<char>& elf_data)
{
    // Everything below indexes into elf_data using offsets read out of the ELF
    // header, so the file has to be big enough to hold that header first
    if (elf_data.size() < sizeof(Elf32_Ehdr)) {
        std::cerr << term::error << "file is too small to be an ELF ("
                  << elf_data.size() << " bytes)" << std::endl;
        return {};
    }

    const char * elf_start = elf_data.data();

    // Really make sure this is an elf. These are real checks and not assert(),
    // which compiles out under NDEBUG and would hand a release build a non-ELF
    // file straight to the header dereference below
    if (elf_data[0] != 0x7F || elf_data[1] != 'E'
        || elf_data[2] != 'L' || elf_data[3] != 'F') {
        std::cerr << term::error << "file is not an ELF" << std::endl;
        return {};
    }

    // ELF header starts at the begining of the file
    const Elf32_Ehdr* elf_header = (const Elf32_Ehdr*)elf_start;

    // Check if ELF is for SuperH
    if( elf_header->e_machine != EM_SH ) {
        std::cerr << term::error << "unsupported Architecture (0x" << std::hex << elf_header->e_machine
                  << std::dec << ")" << std::endl;
        return {};
    }

    // Check if ELF is 32bit
    if( elf_header->e_ident[EI_CLASS] != ELFCLASS32) {
        std::cerr << term::error << "ELF is not 32bit" << std::endl;
        return {};
    }

    // Holds all the segments after parsing
    std::vector<Segment> segments;

    // Each entry in the program header table is e_phentsize bytes long
    auto segment_entry_size = elf_header->e_phentsize;
    // The number of entries in the program header table is e_phnum
    auto segment_entry_count = elf_header->e_phnum;

    // An entry has to be at least as big as the structure read out of it
    if (segment_entry_count > 0 && segment_entry_size < sizeof(Elf32_Phdr)) {
        std::cerr << term::error << "ELF program header entries are too small ("
                  << segment_entry_size << " bytes)" << std::endl;
        return {};
    }

    // e_phoff, e_phentsize and e_phnum all come from the file, so the table
    // they describe has to be shown to be inside it before it is walked
    if (size_t(elf_header->e_phoff)
            + size_t(segment_entry_size) * size_t(segment_entry_count) > elf_data.size()) {
        std::cerr << term::error << "ELF program header table extends past the end of the file"
                  << std::endl;
        return {};
    }

    // Program Header Table can be found using e_phoff from the begining of the elf
    const char * program_header_table = elf_start + elf_header->e_phoff;

    //
    // Parse Segments
    //      For every Program Header (one per segment) in the ELF
    for( int index = 0; index < segment_entry_count; index++ ) {
        // Pointer to Program Header at index
        auto program_header = (const Elf32_Phdr*)(program_header_table + (index * segment_entry_size));
        // p_offset and p_filesz are file-supplied too, and Segment copies that
        // range out of elf_data
        if (size_t(program_header->p_offset)
                + size_t(program_header->p_filesz) > elf_data.size()) {
            std::cerr << term::error << "ELF segment " << index
                      << " extends past the end of the file" << std::endl;
            return {};
        }
        // Add segment to list of segments
        segments.push_back(Segment(program_header, elf_data));
    }

    // Return a shared pointer to the parser
    return std::shared_ptr<Parser>(new Parser(segments));
}

std::optional<std::shared_ptr<Parser>> Parser::Load(const std::filesystem::path& elfPath)
{
    std::error_code ec;
    auto size = std::filesystem::file_size(elfPath, ec);
    if (ec) {
        std::cerr << term::error << "failed to size elf at " << elfPath << ": "
                  << ec.message() << std::endl;
        return {};
    }

    std::ifstream stream;
    stream.open(elfPath, std::ios::binary);
    if (!stream.is_open()) {
        std::cerr << term::error << "failed to open elf at " << elfPath << std::endl;
        return {};
    }

    std::vector<char> elf_data(static_cast<size_t>(size));
    stream.read(elf_data.data(), static_cast<std::streamsize>(elf_data.size()));
    if (static_cast<size_t>(stream.gcount()) != elf_data.size()) {
        std::cerr << term::error << "failed to read elf at " << elfPath << std::endl;
        return {};
    }

    logging::verbose() << "Read " << elf_data.size() << " bytes of ELF";
    return Load(elf_data);
}

}
