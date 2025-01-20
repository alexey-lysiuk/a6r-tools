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


def _calculate_shift(mask: int):
    shiftbits = 0

    while mask & 1 == 0:
        mask >>= 1
        shiftbits += 1

    colorbits = 0

    while mask & 1:
        mask >>= 1
        colorbits += 1

    return shiftbits - (8 - colorbits)


def _shift(color: int, shift: int):
    return color >> shift if shift > 0 else color << -shift


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

    def __init__(self, filename: str):
        with open(filename, 'rb') as f:
            bmpheader = f.read(14)
            magic, filesize, dataoffset = struct.unpack(BMPFile.HEADER_FORMAT, bmpheader)
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
            pixels = [pixel[0] for pixel in struct.iter_unpack('<h', data)]

            palette = {}
            colorscount = 0

            for pixel in pixels:
                if pixel not in palette:
                    palette[pixel] = colorscount
                    colorscount += 1

            self.pixels = pixels
            self.palette = palette
            self.colorscount = colorscount

            self.redshift = _calculate_shift(self.redmask)
            self.greenshift = _calculate_shift(self.greenmask)
            self.blueshift = _calculate_shift(self.bluemask)

    def save(self, filename: str):
        if self.colorscount > 256:
            self._save_rgb(filename)
        else:
            self._save_paletted(filename)

    def _save_paletted(self, filename: str):
        assert self.colorscount <= 256

        with open(filename, 'wb') as f:
            datasize = len(self.pixels)
            dataoffset = BMPFile.HEADER_SIZE + BMPFile.BITMAPINFOHEADER_SIZE + self.colorscount * 4
            filesize = dataoffset + datasize

            bmpheader = struct.pack(BMPFile.HEADER_FORMAT, BMPFile.MAGIC, filesize, dataoffset)
            f.write(bmpheader)

            dibheader = struct.pack(BMPFile.BITMAPINFOHEADER_FORMAT, BMPFile.BITMAPINFOHEADER_SIZE,
                self.width, self.height, 1, 8, BMPFile.BI_RGB, datasize, self.xres, self.yres, self.colorscount)
            f.write(dibheader)

            for color in self.palette:
                red = _shift(color & self.redmask, self.redshift)
                green = _shift(color & self.greenmask, self.greenshift)
                blue = _shift(color & self.bluemask, self.blueshift)
                entry = struct.pack('4B', blue, green, red, 0)
                f.write(entry)

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
                red = _shift(color & self.redmask, self.redshift)
                green = _shift(color & self.greenmask, self.greenshift)
                blue = _shift(color & self.bluemask, self.blueshift)
                pixel = struct.pack('3B', blue, green, red)
                f.write(pixel)


def convert(filename: str):
    bmpfile = BMPFile(filename)

    path, extension = os.path.splitext(filename)
    filename = path + '_converted' + extension

    bmpfile.save(filename)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', metavar='bmp-file', type=str, nargs='+')
    parser.add_argument('--profile', action='store_true', help='enable profiling')
    args = parser.parse_args()

    profiler = None

    if args.profile:
        profiler = cProfile.Profile()
        profiler.enable()

    for filename in args.files:
        convert(filename)

    if profiler:
        profiler.disable()
        stats = pstats.Stats(profiler)
        stats.sort_stats(pstats.SortKey.TIME)
        stats.print_stats()


if '__main__' == __name__:
    main()
