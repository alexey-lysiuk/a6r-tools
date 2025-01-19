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

import struct
import sys

BI_RGB = 0
BI_BITFIELDS = 3

# https://en.wikipedia.org/wiki/BMP_file_format#Bitmap_file_header
BMPHEADER_FORMAT = '<2sI4xI'
BMPHEADER_SIZE = 14

# https://en.wikipedia.org/wiki/BMP_file_format#DIB_header_(bitmap_information_header)
BITMAPINFOHEADER_FORMAT = '3I2H4I8x'
BITMAPINFOHEADER_SIZE = 40

# https://en.wikipedia.org/wiki/BMP_file_format#Example_2
BITMAPV4HEADER_FORMAT = '<3I2H2I2I8x4I52x'
BITMAPV4HEADER_SIZE = 108


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


def convert(filename: str):
    with open(filename, 'rb') as f:
        bmpheader = f.read(14)
        magic, filesize, dataoffset = struct.unpack(BMPHEADER_FORMAT, bmpheader)
        assert magic == b'BM'

        dibheader = f.read(BITMAPV4HEADER_SIZE)
        (didheadersize, width, height, planes, bpp, compression, datasize, xres, yres,
            redmask, greenmask, bluemask, alphamask) = struct.unpack(BITMAPV4HEADER_FORMAT, dibheader)
        assert didheadersize == BITMAPV4HEADER_SIZE and planes == 1 and bpp == 16 and compression == BI_BITFIELDS

        data = f.read(datasize)

    redshift = _calculate_shift(redmask)
    greenshift = _calculate_shift(greenmask)
    blueshift = _calculate_shift(bluemask)

    with open(filename, 'wb') as f:
        datasize = len(data) // 2 * 3  # from 16bit to 24bit per pixel
        dataoffset = BMPHEADER_SIZE + BITMAPINFOHEADER_SIZE
        filesize = dataoffset + datasize
        bmpheader = struct.pack(BMPHEADER_FORMAT, magic, filesize, dataoffset)
        f.write(bmpheader)

        dibheader = struct.pack(BITMAPINFOHEADER_FORMAT,
        BITMAPINFOHEADER_SIZE, width, height, planes, 24, BI_RGB, datasize, xres, yres)
        f.write(dibheader)

        for pixel in struct.iter_unpack('<h', data):
            color = pixel[0]
            red = _shift(color & redmask, redshift)
            green = _shift(color & greenmask, greenshift)
            blue = _shift(color & bluemask, blueshift)
            pixel = struct.pack('3B', blue, green, red)
            f.write(pixel)


def main():
    if len(sys.argv) < 2:
        print('Usage: %s bmp-file ...' % sys.argv[0])
        return 0

    for filename in sys.argv[1:]:
        convert(filename)


if '__main__' == __name__:
    main()
