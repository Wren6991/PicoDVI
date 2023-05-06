#!/usr/bin/env python3

# A script to repack a PCF font file to one that can be used by the
# colortext program.
#
# A good source of suitable PCF files is the X11 distribution.
#
# This script has quite a number of limitations. It hardcodes some of
# the encoding, and there is limited to ASCII. It assumes a number of
# settings (word size etc) that are true for the fonts tested, but not
# of PCF fonts in general. It clips glyphs with negative side bearings
# (typically the descender of the lowercase j).
#
# Input PCF filename is given as argument, C code on stdout.

import sys
import struct

class Pcf:
    def __init__(self, data):
        self.data = data
        n_tables = geti32(data, 4)
        tables = {}
        for i in range(n_tables):
            table_type = geti32(data, 16 * i + 8)
            table_len = geti32(data, 16 * i + 16)
            table_offset = geti32(data, 16 * i + 20)
            table_data = data[table_offset : table_offset + table_len]
            tables[table_type] = table_data
        self.tables = tables
        self.metrics = self.dometrics()
        self.bitmaps = self.dobitmap()

    def dometrics(self):
        data = self.tables[4]
        format = geti32(data, 0)
        metrics_count = geti16(data, 4)
        metrics = []
        for i in range(metrics_count):
            lsb = getu8(data, 5 * i + 6) - 128
            rsb = getu8(data, 5 * i + 7) - 128
            width = getu8(data, 5 * i + 8) - 128
            asc = getu8(data, 5 * i + 9) - 128
            desc = getu8(data, 5 * i + 10) - 128
            metrics.append((lsb, rsb, width, asc, desc))
        return metrics

    def dobitmap(self):
        data = self.tables[8]
        format = geti32(data, 0)
        glyph_count = geti32(data, 4)
        glyphdata = data[24 + 4 * glyph_count:]
        bitmaps = []
        for i in range(glyph_count):
            offset = geti32(data, i * 4 + 8)
            metrics = self.metrics[i]
            #print(i, hex(i + 32), offset, metrics)
            height = metrics[3] + metrics[4]
            # most likely stride is width rounded up to 1 << format
            stride = 4
            bitmap = [getu32(glyphdata, offset + stride * j) for j in range(height)]
            if False:
                for word in bitmap:
                    s = ''
                    while word:
                        if word % 2:
                            s += '##'
                        else:
                            s += '  '
                        word = word >> 1
                    print(s)
            bitmaps.append(bitmap)
        return bitmaps

    def render(self):
        # Only worry about ASCII printable for now
        n = 95
        asc = max(m[3] for m in self.metrics[:n])
        desc = max(m[4] for m in self.metrics[:n])
        height = asc + desc
        x = 0
        pos = []
        for m in self.metrics[:n]:
            pos.append(x)
            x += m[2]
        width = x + (31 & -x)
        bits = [[0] * width for i in range(height)]
        for j in range(n):
            m = self.metrics[j]
            y = asc - m[3]
            w = m[2]
            for word in self.bitmaps[j]:
                x = m[0]
                while word:
                    if word % 2 and x >= 0 and x < w and y >= 0 and y < height:
                        bits[y][x + pos[j]] = 1
                    word = word >> 1
                    x += 1
                y += 1
        stride = width // 8
        print(f'#define FONT_STRIDE {stride}')
        print(f'#define FONT_HEIGHT {height}')
        print_c_array('font_x_offsets', pos, 'uint16_t')
        widths = [m[2] for m in self.metrics[:n]]
        print_c_array('font_widths', widths, 'uint8_t')
        words = []
        for row in bits:
            for x in range(0, width, 32):
                word = 0
                for j in range(32):
                    word |= row[x + j] << j
                words.append(word)
        print_c_array('font_bits', words, 'uint32_t', True)

def print_c_array(name, data, ty, is_hex = False):
    print(f'static const {ty} {name}[] = {{')
    buf = ''
    for x in data:
        s = hex(x) if is_hex else str(x)
        if len(buf) + len(s) <= 72:
            if buf == '':
                buf = '    ' + s
            else:
                buf += ', ' + s
        else:
            print(buf + ',')
            buf = '    ' + s
    print(buf)
    print('};')


def geti32(data, offset):
    return struct.unpack('<i', data[offset:offset + 4])[0]

def getu32(data, offset):
    return struct.unpack('<I', data[offset:offset + 4])[0]

def geti16(data, offset):
    return struct.unpack('<h', data[offset:offset + 2])[0]

def getu8(data, offset):
    return struct.unpack('<B', data[offset:offset + 1])[0]


def dofont(filename):
    data = open(filename, mode="rb").read()
    return Pcf(data)

font = dofont(sys.argv[1])
font.render()
