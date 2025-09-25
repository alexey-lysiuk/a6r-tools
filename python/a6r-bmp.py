#!/usr/bin/env python3

#
# Copyright (C) 2025 Alexey Lysiuk
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
import cProfile
import os
import pstats
import struct


# def _calculate_shift(mask: int):
#     shiftbits = 0

#     while mask & 1 == 0:
#         mask >>= 1
#         shiftbits += 1

#     colorbits = 0

#     while mask & 1:
#         mask >>= 1
#         colorbits += 1

#     return shiftbits - (8 - colorbits)


# def _shift(color: int, shift: int):
#     return color >> shift if shift > 0 else color << -shift



def _rgb565_to_rgb888(rgb565):
    # Shift the red value to the right by 11 bits.
    red5 = rgb565 >> 11
    # Shift the green value to the right by 5 bits and extract the lower 6 bits.
    green6 = (rgb565 >> 5) & 0b111111
    # Extract the lower 5 bits.
    blue5 = rgb565 & 0b11111
    # Convert 5-bit red to 8-bit red.
    red8 = round(red5 / 31 * 255)
    # Convert 6-bit green to 8-bit green.
    green8 = round(green6 / 63 * 255)
    # Convert 5-bit blue to 8-bit blue.
    blue8 = round(blue5 / 31 * 255)
    return red8, green8, blue8


# _COLOR_CONVERSION = None


# def _calculate_bits(mask: int):
#     shiftbits = 0

#     while mask & 1 == 0:
#         mask >>= 1
#         shiftbits += 1

#     colorbits = 0

#     while mask & 1:
#         mask >>= 1
#         colorbits += 1

#     return shiftbits, colorbits


# def _init_color_conversion(redmask: int, greenmask: int, bluemask: int):
#     global _COLOR_CONVERSION
#     assert not _COLOR_CONVERSION

#     # redshift, redcolor = _calculate_bits(redmask)
#     # greenshift, greencolor = _calculate_bits(greenmask)
#     # blueshift, bluecolor = _calculate_bits(bluemask)

#     def make_color(rgb565: int) -> int:
#         # red = rgb565 & redmask >> 8
#         # green = rgb565 & greenmask >> 3
#         # blue = rgb565 & bluemask << 3
#         # return (red << 16) | (green << 8) + blue

#         red = rgb565 & redmask
#         green = rgb565 & greenmask
#         blue = rgb565 & bluemask
#         # return red >> 8 | red >> 12 | green >> 3 | green >> 9
#         return (red << 8 | red << 4) & 0xFF0000 | (green << 5 | green << 2) & 0xFF00 | (blue << 3 | blue >> 2)

#     # make_color(0xffff)
#     _COLOR_CONVERSION = [make_color(c) for c in range(2**16)]


class BMPFile:
    # https://en.wikipedia.org/wiki/BMP_file_format#Bitmap_file_header
    HEADER_FORMAT = '<2sI4xI'
    HEADER_SIZE = 14

    MAGIC = b'BM'

    # https://en.wikipedia.org/wiki/BMP_file_format#DIB_header_(bitmap_information_header)
    BITMAPINFOHEADER_FORMAT = '3I2H5I4x'
    BITMAPINFOHEADER_SIZE = 40

    BI_RGB = 0
    BI_BITFIELDS = 3

    # https://en.wikipedia.org/wiki/BMP_file_format#Example_2
    BITMAPV4HEADER_FORMAT = '<3I2H2I2I8x4I52x'
    BITMAPV4HEADER_SIZE = 108

    _COLOR_CONVERSION = []

    def __init__(self, filename: str):
        with open(filename, 'rb') as f:
            bmpheader = f.read(BMPFile.HEADER_SIZE)
            magic, _, _ = struct.unpack(BMPFile.HEADER_FORMAT, bmpheader)
            assert magic == BMPFile.MAGIC

            dibheader = f.read(BMPFile.BITMAPV4HEADER_SIZE)
            (
                didheadersize, self.width, self.height, planes, bpp, compression, datasize,
                self.xres, self.yres, self.redmask, self.greenmask, self.bluemask, self.alphamask
            ) = struct.unpack(BMPFile.BITMAPV4HEADER_FORMAT, dibheader)

            assert self.width % 2 == 0
            assert self.height % 2 == 0
            assert didheadersize == BMPFile.BITMAPV4HEADER_SIZE
            assert planes == 1
            assert bpp == 16
            assert compression == BMPFile.BI_BITFIELDS

            data = f.read(datasize)
            pixels = [pixel[0] for pixel in struct.iter_unpack('<H', data)]

            palette = {}
            colorscount = 0

            for pixel in pixels:
                if pixel not in palette:
                    palette[pixel] = colorscount
                    colorscount += 1

            self.pixels = pixels
            self.palette = palette
            self.colorscount = colorscount

            # self.redshift = _calculate_shift(self.redmask)
            # self.greenshift = _calculate_shift(self.greenmask)
            # self.blueshift = _calculate_shift(self.bluemask)

            self._init_color_conversion()

    def save(self, filename: str):
        if self.colorscount > 256:
            self._save_rgb(filename)
        else:
            self._save_paletted(filename)

    def _save_paletted(self, filename: str):
        assert self.colorscount <= 256

        fourbitpalette = self.colorscount <= 16
        datasize = len(self.pixels)
        bpp = 8

        if fourbitpalette:
            datasize //= 2
            bpp //= 2

        dataoffset = BMPFile.HEADER_SIZE + BMPFile.BITMAPINFOHEADER_SIZE + self.colorscount * 4
        filesize = dataoffset + datasize

        with open(filename, 'wb') as f:
            bmpheader = struct.pack(BMPFile.HEADER_FORMAT, BMPFile.MAGIC, filesize, dataoffset)
            f.write(bmpheader)

            dibheader = struct.pack(BMPFile.BITMAPINFOHEADER_FORMAT, BMPFile.BITMAPINFOHEADER_SIZE,
                self.width, self.height, 1, bpp, BMPFile.BI_RGB, datasize, self.xres, self.yres, self.colorscount)
            f.write(dibheader)

            for color in self.palette:
                # if color == 0:
                #     entry = b'\0' * 4
                # else:
                #     # red = _shift(color & self.redmask, self.redshift)
                #     # green = _shift(color & self.greenmask, self.greenshift)
                #     # blue = _shift(color & self.bluemask, self.blueshift)
                #     # red, green, blue = _rgb565_to_rgb888(color)
                #     # entry = struct.pack('4B', blue, green, red, 0)
                #     entry = struct.pack('<I', _COLOR_CONVERSION[color])
                #     print(f'{color:x} -> {_COLOR_CONVERSION[color]:x}')

                #     # if color != 0:
                #     #     print(f'{color:x} -> {red:x} {green:x} {blue:x}')

                red, green, blue = BMPFile._COLOR_CONVERSION[color]
                entry = struct.pack('4B', blue, green, red, 0)

                # print(f'{color:x} -> {red:x} {green:x} {blue:x}')
                red2, green2, blue2 = _rgb565_to_rgb888(color)
                match = '' if red == red2 and green == green2 and blue == blue2 else '<- !!!'
                print(f'{color:x} -> {red:x} {green:x} {blue:x} || {red2:x} {green2:x} {blue2:x}  {match}')

                # entry = struct.pack('<I', self._COLOR_CONVERSION[color])
                # print(f'{color:x} -> {self._COLOR_CONVERSION[color]:x}')

                f.write(entry)

            if fourbitpalette:
                for pixel1, pixel2 in zip(*[iter(self.pixels)] * 2):
                    pixel = struct.pack('B', (self.palette[pixel1] << 4) + self.palette[pixel2])
                    f.write(pixel)
            else:
                for pixel in self.pixels:
                    pixel = struct.pack('B', self.palette[pixel])
                    f.write(pixel)

    def _save_rgb(self, filename: str):
        datasize = len(self.pixels) * 3  # 24bit per pixel
        dataoffset = BMPFile.HEADER_SIZE + BMPFile.BITMAPINFOHEADER_SIZE
        filesize = dataoffset + datasize

        with open(filename, 'wb') as f:
            bmpheader = struct.pack(BMPFile.HEADER_FORMAT, BMPFile.MAGIC, filesize, dataoffset)
            f.write(bmpheader)

            dibheader = struct.pack(BMPFile.BITMAPINFOHEADER_FORMAT, BMPFile.BITMAPINFOHEADER_SIZE,
                self.width, self.height, 1, 24, BMPFile.BI_RGB, datasize, self.xres, self.yres, 0)
            f.write(dibheader)

            for color in self.pixels:
                # red = _shift(color & self.redmask, self.redshift)
                # green = _shift(color & self.greenmask, self.greenshift)
                # blue = _shift(color & self.bluemask, self.blueshift)
                # red, green, blue = _rgb565_to_rgb888(color)
                red, green, blue = BMPFile._COLOR_CONVERSION[color]
                pixel = struct.pack('3B', blue, green, red)
                f.write(pixel)

    @staticmethod
    def _init_color_conversion():
        def make_color(rgb565: int) -> int:
            red = rgb565 & 0b11111000_00000000
            green = rgb565 & 0b00000111_11100000
            blue = rgb565 & 0b00000000_00011111
            # return (red << 8 | red << 4) & 0xFF0000 | (green << 5 | green << 2) & 0xFF00 | (blue << 3 | blue >> 2)
            return red >> 8 | red >> 12, green >> 3 | green >> 9, blue << 3 | blue >> 2

        BMPFile._COLOR_CONVERSION = [make_color(c) for c in range(2**16)]


def convert(filename: str, inplace: bool = False):
    bmpfile = BMPFile(filename)

    if not inplace:
        path, extension = os.path.splitext(filename)
        filename = path + '_converted' + extension

    bmpfile.save(filename)


def create_test_pattern():
    # f = open('/Volumes/ramdisk/a6r-tools/data/capture.bmp', 'rb')
    # header = f.read(14+108)
    # print(header)

    header = b'BMz\xb0\x04\0\0\x00\x00\x00z\x00\x00\x00l\x00\x00\x00\xe0\x01\x00\x00@\x01\x00\x00' \
        b'\x01\x00\x10\x00\x03\x00\x00\x00\x00\xb0\x04\x00\xc4\x0e\x00\x00\xc4\x0e\x00\x00\x00\x00\x00' \
        b'\x00\x00\x00\x00\x00\x00\xf8\x00\x00\xe0\x07\x00\x00\x1f\x00\x00\x00\x00\x00\x00\x00BGRs\x00' \
        b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
        b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'

    with open('test-pattern.bmp', 'wb') as f:
        f.write(header)

        for i in range(2 ** 16):
            color = struct.pack('>H', i)
            f.write(color)

        f.write(b'\0\0' * (480 * 320 - 2 ** 16))


def main():
    create_test_pattern()

    parser = argparse.ArgumentParser()
    parser.add_argument('files', metavar='bmp-file', type=str, nargs='+')
    parser.add_argument('--profile', action='store_true', help='enable profiling')
    parser.add_argument('--inplace', action='store_true', help='replace source files with converted')
    args = parser.parse_args()

    profiler = None

    if args.profile:
        profiler = cProfile.Profile()
        profiler.enable()

    for filename in args.files:
        convert(filename, args.inplace)

    if profiler:
        profiler.disable()
        stats = pstats.Stats(profiler)
        stats.sort_stats(pstats.SortKey.TIME)
        stats.print_stats()


if '__main__' == __name__:
    main()
