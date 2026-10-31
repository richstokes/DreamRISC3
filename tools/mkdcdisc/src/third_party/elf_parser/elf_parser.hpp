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

#ifndef H_ELF_PARSER
#define H_ELF_PARSER

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h> /* O_RDONLY */
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string.h>
#include <string>
#include <vector>
#include "elf.h"

namespace elfparser {

class Segment {
protected:
    uint32_t m_Type, m_Offset, m_VirtAddr, m_PhysAddr, 
        m_FileSize, m_MemSize, m_Flags, m_Align;
    std::vector<char> m_Data;
public:
    Segment(const Elf32_Phdr *phdr, const std::vector<char>& elf_data) 
        : m_Type(phdr->p_type)
        , m_Offset(phdr->p_offset)
        , m_VirtAddr(phdr->p_vaddr)
        , m_PhysAddr(phdr->p_paddr)
        , m_FileSize(phdr->p_filesz)
        , m_MemSize(phdr->p_memsz)
        , m_Flags(phdr->p_flags)
        , m_Align(phdr->p_align)
        , m_Data()
        {
            // p_offset and p_filesz are read straight out of the file, so they
            // can point anywhere. Parser::Load() rejects an ELF whose segments
            // do not fit; clamping here as well means no path can build a
            // Segment that reads past the end of elf_data. m_FileSize is
            // corrected to what was actually read so it never over-reports.
            const size_t offset = std::min(size_t(m_Offset), elf_data.size());
            const size_t length = std::min(size_t(m_FileSize), elf_data.size() - offset);
            m_FileSize = static_cast<uint32_t>(length);
            m_Data.assign(elf_data.begin() + offset, elf_data.begin() + offset + length);
    }

    uint32_t get_offset() const { return m_Offset; }
    uint32_t get_filesize() const { return m_FileSize; }
    uint32_t get_virtaddr() const { return m_VirtAddr; }
    uint32_t get_physaddr() const { return m_PhysAddr; }
    uint32_t get_memsize() const { return m_MemSize; }
    uint32_t get_align() const { return m_Align; }
    // Writes exactly get_filesize() bytes; the caller sizes dest from that.
    void write_data_to(char * dest) const {
        if ( !m_Data.empty())
            // m_Data is clamped in the constructor and m_FileSize corrected to
            // match, so dest -- sized from get_filesize() -- holds this exactly.
            // nosemgrep: insecure-use-memcpy-fn
            memcpy(dest, m_Data.data(), m_Data.size());
    }
    uint32_t get_type() const { return m_Type; }

};

class Parser {
private:
    static const uint32_t DC_RAM_START_ADDR = 0x8c010000;

    std::vector<Segment> m_Segments;

public:
    // Load ELF from std::vector<char>
    static std::optional<std::shared_ptr<Parser>> Load(const std::vector<char>& elfData);

    // Load ELF from Path
    static std::optional<std::shared_ptr<Parser>> Load(const std::filesystem::path& elfData);

    bool fill_bin(std::vector<char>& bin_data);

protected:
    inline Parser(std::vector<Segment> segments)
        : m_Segments(std::move(segments)) {};
};

} // namespace elfparser
#endif