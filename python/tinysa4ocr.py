#!/usr/bin/env python3

#
# tinySA Ultra screenshot text recognizer, Python port of cxx/tinysa-ocr
#
# Loads a capture (PNG or 16 bit BMP) and recognizes texts rendered with the
# small 5x7 font in the status column (left part of the screen) and in the
# status row (bottom of the screen) by exact glyph bitmap matching.
#
# Font data is derived from Font5x7.c of https://github.com/erikkaashoek/tinySA
# Copyright (c) 2019-2020, Dmitry (DiSlord) dislordlive@gmail.com
# Licensed under GPL v3 or later.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import argparse
import json
import struct
import sys
from collections import Counter

STATUS_COLUMN_WIDTH = 30  # OFFSETX in tinySA firmware
STATUS_ROW_HEIGHT = 8     # FREQUENCIES_YPOS = LCD_HEIGHT - 8
SEGMENT_GAP_SPACES = 3    # gap of this many spaces or more splits texts
INK_THRESHOLD = 100       # sum of per-channel differences from background
MIN_QUALITY = 0.85        # minimal share of ink explained by recognized glyphs
FONT_HEIGHT = 7

# Characters 0x20..0x7E: (width, 7 row bytes in hex, bit 7 is leftmost column)
FONT = {
    ' ': (4, '00000000000000'),
    '!': (3, '40404040400040'),
    '"': (8, '50505000000000'),
    '#': (6, '5050f850f85050'),
    '$': (6, '2078a07028f020'),
    '%': (6, 'c0c81020409818'),
    '&': (6, '00205060a89068'),
    "'": (4, '60408000000000'),
    '(': (4, '20408080804020'),
    ')': (4, '80402020204080'),
    '*': (6, '0020a870a82000'),
    '+': (6, '002020f8202000'),
    ',': (3, '00000000c04080'),
    '-': (5, '000000f0000000'),
    '.': (3, '0000000000c0c0'),
    '/': (4, '20204040408080'),
    '0': (5, '60909090909060'),
    '1': (5, '20602020202070'),
    '2': (5, '609010204080f0'),
    '3': (5, '60901060109060'),
    '4': (5, '90909090f01010'),
    '5': (5, 'f080e010109060'),
    '6': (5, '609080e0909060'),
    '7': (5, 'f0102020404040'),
    '8': (5, '60909060909060'),
    '9': (5, '60909070109060'),
    ':': (4, '00c0c000c0c000'),
    ';': (4, '00606000604080'),
    '<': (5, '00204080402000'),
    '=': (5, '0000f000f00000'),
    '>': (5, '00804020408000'),
    '?': (5, '60901020400040'),
    '@': (5, '6090b0b0808070'),
    'A': (5, '60909090f09090'),
    'B': (5, 'e09090e09090e0'),
    'C': (5, '60908080809060'),
    'D': (5, 'e09090909090e0'),
    'E': (5, 'f08080e08080f0'),
    'F': (5, 'f08080e0808080'),
    'G': (5, '609080b0909070'),
    'H': (5, '909090f0909090'),
    'I': (4, 'e04040404040e0'),
    'J': (5, '70101010109060'),
    'K': (5, '909090e0909090'),
    'L': (5, '808080808080f0'),
    'M': (6, '88d8a8a8a88888'),
    'N': (5, '9090d0b0909090'),
    'O': (5, '60909090909060'),
    'P': (5, 'e09090e0808080'),
    'Q': (5, '6090909090a050'),
    'R': (5, 'e09090e0909090'),
    'S': (5, '60908060109060'),
    'T': (6, 'f8202020202020'),
    'U': (5, '90909090909070'),
    'V': (5, '90909090906060'),
    'W': (6, '888888a8a8a850'),
    'X': (6, '88885020508888'),
    'Y': (6, '88888850202020'),
    'Z': (5, 'f01020408080f0'),
    '[': (4, 'e08080808080e0'),
    '\\': (4, '80804040402020'),
    ']': (4, 'e02020202020e0'),
    '^': (4, '40a00000000000'),
    '_': (5, '000000000000f8'),
    '`': (4, 'c0402000000000'),
    'a': (5, '0000709090b050'),
    'b': (5, '8080a0d09090e0'),
    'c': (5, '00007080808070'),
    'd': (5, '101050b0909070'),
    'e': (5, '00006090f08070'),
    'f': (5, '205040e0404040'),
    'g': (5, '00007090701060'),
    'h': (5, '8080a0d0909090'),
    'i': (4, '4000c0404040e0'),
    'j': (4, '2000202020a040'),
    'k': (5, '808090a0c0a090'),
    'l': (4, 'c04040404040e0'),
    'm': (6, '0000f0a8a8a8a8'),
    'n': (5, '0000a0d0909090'),
    'o': (5, '00006090909060'),
    'p': (5, '0000e09090e080'),
    'q': (5, '00007090907010'),
    'r': (5, '0000a0d0808080'),
    's': (5, '000070806010e0'),
    't': (5, '4040e040404030'),
    'u': (5, '0000909090b050'),
    'v': (5, '00009090906060'),
    'w': (6, '00008888a8a850'),
    'x': (5, '00009090609090'),
    'y': (5, '000090907010e0'),
    'z': (5, '0000f0204080f0'),
    '{': (4, '20404080404020'),
    '|': (4, '20202020202020'),
    '}': (4, '80404020404080'),
    '~': (5, '000050a0000000'),
}


class Glyph:
    __slots__ = ('ch', 'width', 'ink', 'cols')

    def __init__(self, ch, width, rows):
        self.ch = ch
        self.width = width
        # Bit r of each column mask is set if row r is inked
        self.cols = tuple(sum(((rows[r] >> (7 - c)) & 1) << r for r in range(FONT_HEIGHT)) for c in range(width))
        self.ink = sum(bin(col).count('1') for col in self.cols)


def load_glyphs():
    glyphs = []
    space_width = 4

    for ch, (width, rows) in FONT.items():
        if ch == ' ':
            space_width = width
            continue

        # 'O' is identical to '0' and is resolved by resolve_zero_o()
        if ch == 'O':
            continue

        glyph = Glyph(ch, width, bytes.fromhex(rows))

        if glyph.ink > 0:
            glyphs.append(glyph)

    return glyphs, space_width


GLYPHS, SPACE_WIDTH = load_glyphs()


class Image:
    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows  # top to bottom, each row is a list of (r << 16) | (g << 8) | b


def _scale_channel(value, bits):
    # Same bit replication as stb_image does
    value <<= 8 - bits
    result = value
    z = bits

    while z < 8:
        result += value >> z
        z += bits

    return result


def _mask_info(mask):
    shift = 0

    while mask and (mask >> shift) & 1 == 0:
        shift += 1

    bits = 0

    while (mask >> (shift + bits)) & 1:
        bits += 1

    return shift, bits


def load_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()

    if len(data) < 54 or data[:2] != b'BM':
        raise ValueError('not a BMP file')

    dataoffset = struct.unpack_from('<I', data, 10)[0]
    headersize, width, height, planes, bpp, compression = struct.unpack_from('<I2i2HI', data, 14)

    if headersize < 40:
        raise ValueError('unsupported BMP header')

    if bpp != 16 or planes != 1:
        raise ValueError('only 16 bit per pixel BMP is supported')

    if compression == 0:
        masks = (0x7C00, 0x03E0, 0x001F)  # 555
    elif compression == 3:
        if len(data) < 14 + 40 + 12:
            raise ValueError('truncated BMP header')

        masks = struct.unpack_from('<3I', data, 14 + 40)
    else:
        raise ValueError('unsupported BMP compression')

    if width <= 0 or height == 0:
        raise ValueError('invalid BMP size')

    topdown = height < 0
    height = abs(height)
    stride = (width * 2 + 3) & ~3

    if dataoffset + stride * height > len(data):
        raise ValueError('truncated BMP data')

    infos = [_mask_info(mask) for mask in masks]

    if any(bits == 0 or bits > 8 for _, bits in infos):
        raise ValueError('unsupported BMP color masks')

    cache = {}

    def convert(pixel):
        color = cache.get(pixel)

        if color is None:
            color = 0

            for mask, (shift, bits) in zip(masks, infos):
                color = (color << 8) | _scale_channel((pixel & mask) >> shift, bits)

            cache[pixel] = color

        return color

    rows = []

    for y in range(height):
        srcrow = y if topdown else height - 1 - y
        offset = dataoffset + srcrow * stride
        pixels = struct.unpack_from('<%dH' % width, data, offset)
        rows.append([convert(pixel) for pixel in pixels])

    return Image(width, height, rows)


def load_png(path):
    import png

    width, height, pixels, info = png.Reader(filename=path).asDirect()
    planes = info['planes']
    maxval = (1 << info['bitdepth']) - 1
    rows = []

    for row in pixels:
        if info['bitdepth'] != 8:
            row = [value * 255 // maxval for value in row]

        if planes >= 3:
            rows.append([(row[i] << 16) | (row[i + 1] << 8) | row[i + 2] for i in range(0, width * planes, planes)])
        else:
            rows.append([(row[i] * 0x010101) for i in range(0, width * planes, planes)])

    return Image(width, height, rows)


def load_image(path):
    with open(path, 'rb') as f:
        magic = f.read(8)

    if magic[:2] == b'BM':
        return load_bmp(path)

    if magic == b'\x89PNG\r\n\x1a\n':
        return load_png(path)

    raise ValueError('unknown image format, only PNG and BMP are supported')


def color_name(r, g, b):
    maxc = max(r, g, b)
    minc = min(r, g, b)
    delta = maxc - minc

    # Low saturation is white or gray, the tolerance covers pale colors such as pink
    if delta * 4 < maxc:
        return 'white' if maxc >= 192 else 'gray'

    if maxc == r:
        hue = 60.0 * ((g - b) / delta)
    elif maxc == g:
        hue = 60.0 * ((b - r) / delta + 2.0)
    else:
        hue = 60.0 * ((r - g) / delta + 4.0)

    if hue < 0.0:
        hue += 360.0

    if hue < 15.0 or hue >= 330.0:
        return 'red'
    if hue < 45.0:
        return 'orange'
    if hue < 75.0:
        return 'yellow'
    if hue < 165.0:
        return 'green'
    if hue < 200.0:
        return 'cyan'
    if hue < 260.0:
        return 'blue'
    return 'magenta'


def resolve_zero_o(value):
    # Glyphs of '0' and 'O' are identical, use uppercase neighbors as a hint
    chars = list(value)

    def numeric(c):
        return c.isdigit() or c == '.'

    for i, ch in enumerate(chars):
        if ch != '0':
            continue

        before = chars[i - 1] if i > 0 else ' '
        after = chars[i + 1] if i + 1 < len(chars) else ' '

        if not numeric(before) and not numeric(after) and (before.isupper() or after.isupper()):
            chars[i] = 'O'

    return ''.join(chars)


class Recognizer:
    def __init__(self, image, x0, y0, x1, y1):
        self.image = image
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1

        histogram = Counter()

        for y in range(y0, y1):
            histogram.update(image.rows[y][x0:x1])

        # Most frequent color, the smallest one wins a tie
        self.background = max(histogram.items(), key=lambda e: (e[1], -e[0]))[0]

        br, bg, bb = self.background >> 16, (self.background >> 8) & 255, self.background & 255
        ink_cache = {}
        self.colbits = []  # per column, bit y is set for ink at row y (absolute)
        self.rowink = [0] * y1

        for x in range(x0, x1):
            bits = 0

            for y in range(y0, y1):
                p = image.rows[y][x]
                ink = ink_cache.get(p)

                if ink is None:
                    ink = abs((p >> 16) - br) + abs(((p >> 8) & 255) - bg) + abs((p & 255) - bb) >= INK_THRESHOLD
                    ink_cache[p] = ink

                if ink:
                    bits |= 1 << y
                    self.rowink[y] += 1

            self.colbits.append(bits)

    def is_ink(self, x, y):
        return (self.colbits[x - self.x0] >> y) & 1

    def cell_color(self, x, width, top):
        r = g = b = count = 0

        for row in range(FONT_HEIGHT):
            for c in range(width):
                if self.is_ink(x + c, top + row):
                    p = self.image.rows[top + row][x + c]
                    r += p >> 16
                    g += (p >> 8) & 255
                    b += p & 255
                    count += 1

        return color_name(r // count, g // count, b // count) if count > 0 else 'white'

    def decode_line(self, top):
        """Returns (score, quality, tokens) for the best glyph sequence of a line starting at the given row."""
        n = self.x1 - self.x0
        mask = (1 << FONT_HEIGHT) - 1
        cols = [(bits >> top) & mask for bits in self.colbits]
        colink = [bin(c).count('1') for c in cols]
        totalink = sum(colink)

        if totalink == 0:
            return 0.0, 0.0, []

        # Decoding may start at any column
        score = [0.0] * (n + 1)
        prev = [-1] * (n + 1)
        glyph = [None] * (n + 1)
        unknown = [False] * (n + 1)
        glyphs = GLYPHS

        for i in range(n):
            base = score[i]
            ci = colink[i]
            step_score = base - 2.0 * ci

            if step_score > score[i + 1] or (ci == 0 and step_score >= score[i + 1]):
                score[i + 1] = step_score
                prev[i + 1] = i
                glyph[i + 1] = None
                unknown[i + 1] = ci > 0

            col = cols[i]

            for g in glyphs:
                w = g.width

                if i + w > n or g.cols[0] != col:
                    continue

                if cols[i:i + w] != list(g.cols):
                    continue

                # Glyph cell may start with blank columns, so only skip fully blank cells
                if not any(colink[i:i + w]):
                    continue

                s = base + g.ink

                if s > score[i + w]:
                    score[i + w] = s
                    prev[i + w] = i
                    glyph[i + w] = g
                    unknown[i + w] = False

        end = 0

        for i in range(n + 1):
            if score[i] >= score[end]:
                end = i

        total = score[end]
        matched = 0
        reversed_tokens = []
        x0 = self.x0
        i = end

        while i > 0 and prev[i] >= 0:
            frm = prev[i]
            g = glyph[i]

            if g is not None:
                matched += g.ink
                reversed_tokens.append((x0 + frm, g.width, g.ch, self.cell_color(x0 + frm, g.width, top)))
            elif unknown[i]:
                reversed_tokens.append((x0 + frm, 1, '?', self.cell_color(x0 + frm, 1, top)))

            i = frm

        reversed_tokens.reverse()
        return (total if matched > 0 else 0.0), matched / totalink, reversed_tokens

    def run(self):
        result = []
        y = self.y0

        while y < self.y1:
            if self.rowink[y] == 0:
                y += 1
                continue

            # Top ink row may belong to any glyph row, so try all line positions
            last_top = self.y1 - FONT_HEIGHT
            best_top = -1
            best_score = 0.0
            best_tokens = []
            best_quality = 0.0

            for top in range(max(self.y0, y - (FONT_HEIGHT - 1)), min(y, last_top) + 1):
                score, quality, tokens = self.decode_line(top)

                if score > best_score:
                    best_score = score
                    best_top = top
                    best_tokens = tokens
                    best_quality = quality

            if best_top < 0:
                y += 1
                continue

            if best_quality >= MIN_QUALITY:
                self.split_texts(best_top, best_tokens, result)

            y = best_top + FONT_HEIGHT

        return result

    @staticmethod
    def split_texts(top, tokens, out):
        text = None  # [x, y, color, value]
        prev_end = 0

        for x, width, ch, color in tokens:
            if text is not None:
                gap = x - prev_end

                if gap >= SEGMENT_GAP_SPACES * SPACE_WIDTH or color != text[2]:
                    text[3] = resolve_zero_o(text[3])
                    out.append(tuple(text))
                    text = None
                else:
                    text[3] += ' ' * ((gap + SPACE_WIDTH // 2) // SPACE_WIDTH)

            if text is None:
                text = [x, top, color, '']

            # Merge adjacent unrecognized columns
            if not (ch == '?' and text[3].endswith('?') and x == prev_end):
                text[3] += ch

            prev_end = x + width

        if text is not None:
            text[3] = resolve_zero_o(text[3])
            out.append(tuple(text))


def main():
    parser = argparse.ArgumentParser(
        description='Recognizes small font texts in the status column and the status row. '
        'Default output is tab-separated: region, x, y, color, text.')
    parser.add_argument('capture', metavar='capture.png|capture.bmp')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--plain', action='store_true', help='output recognized texts only')
    group.add_argument('--json', action='store_true', help='output a JSON array of recognized text objects')
    args = parser.parse_args()

    try:
        image = load_image(args.capture)
    except Exception as e:
        print("Failed to load '%s': %s" % (args.capture, e), file=sys.stderr)
        return 1

    if image.width <= STATUS_COLUMN_WIDTH or image.height <= STATUS_ROW_HEIGHT * 2:
        print('Image size %dx%d is too small for a tinySA capture' % (image.width, image.height), file=sys.stderr)
        return 1

    regions = (
        ('column', 0, 0, STATUS_COLUMN_WIDTH, image.height),
        ('row', STATUS_COLUMN_WIDTH, image.height - STATUS_ROW_HEIGHT, image.width, image.height),
    )

    lines = []
    items = []

    for name, x0, y0, x1, y1 in regions:
        for x, y, color, value in Recognizer(image, x0, y0, x1, y1).run():
            if args.plain:
                lines.append(value)
            elif args.json:
                items.append('  ' + json.dumps({'region': name, 'x': x, 'y': y, 'color': color, 'text': value},
                                               separators=(',', ':')))
            else:
                lines.append('%s\t%d\t%d\t%s\t%s' % (name, x, y, color, value))

    if args.json:
        print('[\n' + ',\n'.join(items) + '\n]')
    elif lines:
        print('\n'.join(lines))

    return 0


if __name__ == '__main__':
    sys.exit(main())
