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
import datetime
import enum
import struct
import sys

import serial
from serial.tools import list_ports


_DeviceType = enum.Enum('DeviceType', 'TINYSA4 NANOVNA_FVX')

_BMP_HEADER1 = b'BMz\xb0\x04\x00\x00\x00\x00\x00z\x00\x00\x00l\x00\x00\x00'
_BMP_HEADER2 = b'\x01'\
    b'\x00\x10\x00\x03\x00\x00\x00\x00\xb0\x04\x00\xc4\x0e\x00\x00\xc4\x0e\x00\x00\x00\x00\x00\x00'\
    b'\x00\x00\x00\x00\x00\xf8\x00\x00\xe0\x07\x00\x00\x1f\x00\x00\x00\x00\x00\x00\x00BGRs\x00\x00'\
    b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'\
    b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'


class SMTVirtualCOMPort:
    VID = 0x0483  # 1155
    PID = 0x5740  # 22336

    def __init__(self, device_name: str, verbose: bool = False):
        self.verbose = verbose

        if not device_name:
            ports = list_ports.comports()

            for port in ports:
                if port.vid == self.VID and port.pid == self.PID:
                    device_name = port.device
                    break

        if not device_name:
            raise OSError('No devices found')

        self._device = serial.Serial(device_name)
        self.send('info')

        device_info = self.receive()

        if verbose:
            print(device_info)

        if device_info.find('tinySA ULTRA') != -1:
            self._device_type = _DeviceType.TINYSA4
        elif device_info.find('NanoVNA-F_V') != -1:
            self._device_type = _DeviceType.NANOVNA_FVX
        else:
            raise RuntimeError('No supported devices found')

    def send(self, command: str):
        device = self._device
        assert device

        if not command.endswith('\r'):
            command += '\r'

        device.write(command.encode())
        device.readline()  # discard empty line

    def receive(self):
        device = self._device
        assert device

        result = bytearray()
        line = bytearray()

        while True:
            c = device.read()

            if c == b'\r':
                continue  # ignore CR

            line += c

            if c == b'\n':
                result += line
                line = bytearray()
                continue

            if line.endswith(b'ch>'):
                # stop on prompt
                break

        return result.decode()

    def capture(self, path: str) -> bool:
        verbose = self.verbose
        device_type = self._device_type

        if device_type == _DeviceType.TINYSA4:
            width, height = 480, 320
        elif device_type == _DeviceType.NANOVNA_FVX:
            width, height = 800, 480
        else:
            return False

        if verbose:
            print(f'Capturing {width}x{height} bitmap...')

        self.send('capture')

        pixels_length = width * height * 2
        pixels = self._device.read(pixels_length)

        if device_type == _DeviceType.TINYSA4:
            # Swap bytes in pixels
            pixels = bytes(pixels[x ^ 1] for x in range(pixels_length))

        path = self._prepare_filename(path, 'bmp')

        if verbose:
            print(f'Saving capture to {path}...')

        with open(path, 'wb') as f:
            # Store bitmap from top to bottom by using a negative value for image height
            resolution = struct.pack('<2i', width, -height)

            f.write(_BMP_HEADER1)
            f.write(resolution)
            f.write(_BMP_HEADER2)
            f.write(pixels)

        return True

    def copy(self, pattern: str):
        verbose = self.verbose

        if verbose:
            print(f'Copying files {pattern}...')

        entries = self._list(pattern).splitlines()

        for entry in entries:
            name, size = entry.split(' ')

            if verbose:
                print(f'Reading file {name} of {size} bytes...')

            content = self._read(name)
            content_size = len(content)

            if content_size != int(size):
                raise RuntimeError(f'Inconsistent size of file {name}, {size} vs. {content_size}')

            if verbose:
                print(f'Saving file {name}...')

            with open(name, 'wb') as f:
                f.write(content)

    def delete(self, pattern: str):
        if self.verbose:
            print(f'Deleting files {pattern}...')

        self.send(f'sd_delete {pattern}')

    def list(self, pattern: str):
        if self.verbose:
            print(f'Listing files {pattern}...')

        print(self._list(pattern))

    def _list(self, pattern: str) -> str:
        self.send(f'sd_list {pattern}')
        return self.receive()

    def _read(self, filename: str):
        self.send(f'sd_read {filename}')

        device = self._device
        assert device

        size_binary = device.read(4)

        if size_binary == b'err:':
            message = self.receive()
            raise RuntimeError(f"Cannot read {filename} from SD card, error{message}")

        size = struct.unpack('<1I', size_binary)[0]
        return device.read(size)

    def _prepare_filename(self, path: str, extension: str) -> str:
        if path == '*':
            time = datetime.datetime.now().strftime('%y%m%d_%H%M%S')
            prefix = self._filename_prefix()
            return f'{prefix}_{time}.{extension}'
        else:
            return path

    def _filename_prefix(self):
        if self._device_type == _DeviceType.TINYSA4:
            return 'SA'
        elif self._device_type == _DeviceType.NANOVNA_FVX:
            return 'VNA'
        else:
            raise RuntimeError('Invalid device type')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-C', '--capture', const='*', help='save screen to file', metavar='bmp-file', nargs='?')
    parser.add_argument('-D', '--delete', help='delete files from SD card', metavar='pattern')
    parser.add_argument('-X', '--copy', help='copy files from SD card', metavar='pattern')
    parser.add_argument('-L', '--list', const='*', help='list files on SD card', metavar='pattern', nargs='?')
    parser.add_argument('--device', help='specify device explicitly', metavar='device-name')
    parser.add_argument('--verbose', action='store_true', help='enable verbose output')
    args = parser.parse_args()

    if len(sys.argv) == 1:
        parser.print_help()
        return

    device = SMTVirtualCOMPort(args.device, args.verbose)

    if args.capture:
        device.capture(args.capture)

    if args.copy:
        device.copy(args.copy)

    if args.delete:
        device.delete(args.delete)

    if args.list:
        device.list(args.list)


if '__main__' == __name__:
    main()
