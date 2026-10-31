#!/usr/bin/env python3
"""Unpack ScorPress files, as used by Tertis. Ported from the ARM decompressor.

The stream is 32-bit words read low bit first. After a 24-bit header come the
widths of a match's offset and length code, then a table turning length codes
into lengths, delta coded from both ends. Then each item is a 0 and a literal
byte, or a 1, an offset and a length code.
"""


class Bits:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, count):
        start, shift = self.pos >> 3, self.pos & 7
        self.pos += count
        field = int.from_bytes(self.data[start:start + 4], "little") >> shift
        return field & ((1 << count) - 1)


def length_table(bits, code_bits, total_bits):
    least = 2 if total_bits < 18 else 3
    last = (1 << code_bits) - 1
    if code_bits == 8:
        return bytes((least + i) & 0xFF for i in range(256))
    table = bytearray(last + 1)
    most = bits.take(8)
    table[last] = most
    table[0] = lowest = least + bits.take((most - least - last).bit_length())
    code = last - 1
    gap = most - 1 - (lowest + code)
    low = lowest + code
    while code > 0:
        gap = bits.take(gap.bit_length())
        table[code] = low + gap
        low -= 1
        code -= 1
    return table


def unpack(data):
    head = int.from_bytes(data[:4], "little")
    size = head & 0x7FFFFF
    if not head >> 23 & 1:
        return data[3:3 + size]
    bits = Bits(data[3:])  # the header's top byte starts the bit stream
    flags = bits.take(6)
    offset_bits = (flags & 7) + 8
    code_bits = (flags >> 3 & 7) + 1
    table = length_table(bits, code_bits, code_bits + offset_bits + 1)
    out = bytearray()
    while len(out) < size:
        if not bits.take(1):
            out.append(bits.take(8))
            continue
        offset = bits.take(offset_bits)
        start = len(out) - offset - 1
        for i in range(table[bits.take(code_bits)]):
            out.append(out[start + i])
    return bytes(out)
