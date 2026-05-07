#!/usr/bin/env python3

#
# Copyright (C) 2025-2026 Alexey Lysiuk
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
import datetime
import struct
import sys

import serial
from serial.tools import list_ports


_VID = 0x0483  # STMicroelectronics
_PID = 0x5740  # Virtual COM Port

_WIDTH = 320
_HEIGHT = 240
_PIXELS_SIZE = _WIDTH * _HEIGHT * 2  # RGB565: 2 bytes per pixel

_DATA_OFFSET = 122  # 14-byte BMP file header + 108-byte BITMAPV4HEADER
_FILE_SIZE = _DATA_OFFSET + _PIXELS_SIZE

# BMP file header (14 bytes) + BITMAPV4HEADER size field (4 bytes)
# Layout: magic(2) + filesize(4) + reserved(4) + dataoffset(4) + headersize(4)
_BMP_HEADER1 = struct.pack('<2sI4xII', b'BM', _FILE_SIZE, _DATA_OFFSET, 108)

# BITMAPV4HEADER fields that follow width and height (96 bytes):
# planes(2) + bpp(2) + compression(4) + imagesize(4) + xpels(4) + ypels(4) +
# clrused(4) + clrimportant(4) + redmask(4) + greenmask(4) + bluemask(4) +
# alphamask(4) + cstype(4) + CIEXYZTRIPLE(36) + gammaRed(4) + gammaGreen(4) +
# gammaBlue(4) = 96 bytes
_BMP_HEADER2 = struct.pack(
    '<HHIIiiII4I',
    1,            # planes
    16,           # bpp
    3,            # BI_BITFIELDS
    _PIXELS_SIZE, # image data size
    3780,         # x pixels per meter (~96 dpi)
    3780,         # y pixels per meter (~96 dpi)
    0,            # colors used
    0,            # colors important
    0xF800,       # red mask   (RGB565: bits 15–11)
    0x07E0,       # green mask (RGB565: bits 10–5)
    0x001F,       # blue mask  (RGB565: bits 4–0)
    0,            # alpha mask
) + b'BGRs' + b'\x00' * 48  # color space type + CIEXYZTRIPLE + gamma values


class LiteVNA64:
    def __init__(self, device_name: str = None, verbose: bool = False):
        self.verbose = verbose

        if not device_name:
            for port in list_ports.comports():
                if port.vid == _VID and port.pid == _PID:
                    device_name = port.device
                    break

        if not device_name:
            raise OSError('LiteVNA 64 not found')

        if verbose:
            print(f'Connecting to {device_name}...')

        self._device = serial.Serial(device_name, timeout=10)
        self._device.reset_input_buffer()

    def capture(self, path: str) -> bool:
        if self.verbose:
            print(f'Capturing {_WIDTH}x{_HEIGHT} bitmap...')

        self._send('capture')

        pixels = self._device.read(_PIXELS_SIZE)

        if len(pixels) != _PIXELS_SIZE:
            print(f'Error: expected {_PIXELS_SIZE} bytes, received {len(pixels)}',
                  file=sys.stderr)
            return False

        # Device sends big-endian RGB565; swap to little-endian for BMP
        pixels = bytes(pixels[x ^ 1] for x in range(_PIXELS_SIZE))

        if path == '*':
            time = datetime.datetime.now().strftime('%y%m%d_%H%M%S')
            path = f'litevna64_{time}.bmp'

        if self.verbose:
            print(f'Saving capture to {path}...')

        with open(path, 'wb') as f:
            # Negative height stores rows top-to-bottom in the BMP file
            resolution = struct.pack('<2i', _WIDTH, -_HEIGHT)
            f.write(_BMP_HEADER1)
            f.write(resolution)
            f.write(_BMP_HEADER2)
            f.write(pixels)

        return True

    def info(self):
        self._send('info')
        print(self._receive_text())

    def version(self):
        self._send('version')
        print(self._receive_text())

    def _send(self, command: str):
        if not command.endswith('\r'):
            command += '\r'
        self._device.write(command.encode())
        self._device.readline()  # discard echo

    def _receive_text(self) -> str:
        result = bytearray()
        line = bytearray()

        while True:
            c = self._device.read()

            if c == b'\r':
                continue  # ignore CR

            line += c

            if c == b'\n':
                result += line
                line = bytearray()
                continue

            if line.endswith(b'ch>'):
                break

        return result.decode()


def main():
    parser = argparse.ArgumentParser(description='LiteVNA 64 screen capture utility')
    parser.add_argument('-C', '--capture', const='*', metavar='bmp-file', nargs='?',
                        help='capture screen to BMP file (auto-named when path is omitted)')
    parser.add_argument('--device', metavar='device-name',
                        help='specify serial port device (auto-detected when omitted)')
    parser.add_argument('--info', action='store_true',
                        help='print device info')
    parser.add_argument('--verbose', action='store_true',
                        help='enable verbose output')
    parser.add_argument('--version', action='store_true',
                        help='print device version')
    args = parser.parse_args()

    if len(sys.argv) == 1:
        parser.print_help()
        return

    device = LiteVNA64(args.device, args.verbose)

    if args.capture:
        device.capture(args.capture)

    if args.info:
        device.info()

    if args.version:
        device.version()


if '__main__' == __name__:
    main()
