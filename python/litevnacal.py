#!/usr/bin/env python3

#
# Copyright (C) 2026 Alexey Lysiuk
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

"""Read and write LiteVNA 64 and LiteVNA 67 calibration files (*.cal).

File layout (little endian), reverse engineered from files saved by firmware 260713:

    offset  size  contents
    0x000   4     magic, 0x80081248
    0x008   8     sweep start frequency, Hz
    0x010   8     sweep stop frequency, Hz
    0x018   8     calibration start frequency, Hz
    0x020   8     calibration stop frequency, Hz
    0x038   2     number of calibration points (1024)
    0x03a   2     calibration status bit mask
    0x03c   48    4 traces, 12 bytes each (flags, scale, reference position)
    0x070   128   8 markers, 16 bytes each (enabled, index, frequency)
    0x0f0   ...   other settings, preserved as is
    0x18c   49152 calibration data, 6 terms * 1024 points * (float32 real, float32 imag)
    0xc18c  4     checksum

Checksum is the same rotate-and-add sum as in tinySA/NanoVNA settings, but it covers
all bytes before the last 8 bytes of the file. LiteVNA 67 files have zero checksum.
"""

import argparse
import struct
import typing

MAGIC = 0x80081248
POINTS = 1024
TERMS = 6
HEADER_SIZE = 0x18c
DATA_SIZE = TERMS * POINTS * 8
FILE_SIZE = HEADER_SIZE + DATA_SIZE + 4
CHECKSUM_BYTES = FILE_SIZE - 8

TRACES_OFFSET = 0x3c
TRACE_COUNT = 4
TRACE_SIZE = 12
MARKERS_OFFSET = 0x70
MARKER_COUNT = 8
MARKER_SIZE = 16


def calculate_checksum(data: typing.Union[bytes, bytearray]) -> int:
    values = struct.unpack(f'<{len(data) // 4}I', data)
    mask = (1 << 32) - 1
    checksum = 0

    for value in values:
        checksum = ((checksum >> 31) | (checksum << 1)) & mask
        checksum = (checksum + value) & mask

    return checksum


class Trace(typing.NamedTuple):
    enabled: int
    type: int
    channel: int
    reserved: int
    scale: float
    reference: float


class Marker(typing.NamedTuple):
    enabled: int
    reserved: int
    index: int
    frequency: int


class CalFile:
    def __init__(self):
        self.header = bytearray(HEADER_SIZE)
        struct.pack_into('<I', self.header, 0, MAGIC)
        self.points = POINTS
        # terms[term][point] is a complex number
        self.terms = [[0j] * POINTS for _ in range(TERMS)]
        self.checksum = 0

    def _get(self, fmt: str, offset: int):
        return struct.unpack_from(fmt, self.header, offset)

    def _set(self, fmt: str, offset: int, *values):
        struct.pack_into(fmt, self.header, offset, *values)

    @property
    def start(self) -> int:
        return self._get('<Q', 0x08)[0]

    @start.setter
    def start(self, value: int):
        self._set('<Q', 0x08, value)

    @property
    def stop(self) -> int:
        return self._get('<Q', 0x10)[0]

    @stop.setter
    def stop(self, value: int):
        self._set('<Q', 0x10, value)

    @property
    def cal_start(self) -> int:
        return self._get('<Q', 0x18)[0]

    @cal_start.setter
    def cal_start(self, value: int):
        self._set('<Q', 0x18, value)

    @property
    def cal_stop(self) -> int:
        return self._get('<Q', 0x20)[0]

    @cal_stop.setter
    def cal_stop(self, value: int):
        self._set('<Q', 0x20, value)

    @property
    def cal_points(self) -> int:
        return self._get('<H', 0x38)[0]

    @property
    def cal_status(self) -> int:
        return self._get('<H', 0x3a)[0]

    @cal_status.setter
    def cal_status(self, value: int):
        self._set('<H', 0x3a, value)

    @property
    def traces(self) -> typing.List[Trace]:
        return [Trace(*self._get('<4Bff', TRACES_OFFSET + i * TRACE_SIZE)) for i in range(TRACE_COUNT)]

    @property
    def markers(self) -> typing.List[Marker]:
        return [Marker(*self._get('<BBHxxxxQ', MARKERS_OFFSET + i * MARKER_SIZE)) for i in range(MARKER_COUNT)]

    def frequencies(self) -> typing.List[float]:
        """Frequency of each calibration point, linearly spaced between calibration start and stop."""
        step = (self.cal_stop - self.cal_start) / (self.points - 1)
        return [self.cal_start + step * i for i in range(self.points)]

    @classmethod
    def from_bytes(cls, raw: typing.Union[bytes, bytearray]) -> 'CalFile':
        if len(raw) != FILE_SIZE:
            raise ValueError(f'Unexpected calibration file size {len(raw)}, expected {FILE_SIZE}')

        magic, = struct.unpack_from('<I', raw)
        if magic != MAGIC:
            raise ValueError(f'Bad magic 0x{magic:08X}, expected 0x{MAGIC:08X}')

        cal = cls()
        cal.header = bytearray(raw[:HEADER_SIZE])

        if cal.cal_points != POINTS:
            raise ValueError(f'Unsupported number of calibration points {cal.cal_points}')

        floats = struct.unpack_from(f'<{TERMS * POINTS * 2}f', raw, HEADER_SIZE)
        for term in range(TERMS):
            base = term * POINTS * 2
            cal.terms[term] = [complex(floats[base + i * 2], floats[base + i * 2 + 1]) for i in range(POINTS)]

        cal.checksum, = struct.unpack_from('<I', raw, FILE_SIZE - 4)
        return cal

    def to_bytes(self) -> bytes:
        floats = []
        for term in self.terms:
            if len(term) != POINTS:
                raise ValueError(f'Each term must have {POINTS} points')
            for value in term:
                floats += (value.real, value.imag)

        body = bytes(self.header) + struct.pack(f'<{len(floats)}f', *floats)
        # Checksum covers everything but the last 8 bytes, see module docstring
        checksum = calculate_checksum(body[:CHECKSUM_BYTES])
        return body + struct.pack('<I', checksum)

    @property
    def checksum_valid(self) -> bool:
        return self.checksum == calculate_checksum(self.to_bytes()[:CHECKSUM_BYTES])


def read(path: str) -> CalFile:
    with open(path, 'rb') as f:
        return CalFile.from_bytes(f.read())


def write(path: str, cal: CalFile):
    with open(path, 'wb') as f:
        f.write(cal.to_bytes())


def main():
    parser = argparse.ArgumentParser(description='Show or convert LiteVNA 64/67 calibration files')
    parser.add_argument('input', help='calibration file to read')
    parser.add_argument('-o', '--output', help='write calibration file copy (checksum is recalculated)')
    parser.add_argument('-c', '--csv', help='write calibration terms to CSV file')
    args = parser.parse_args()

    cal = read(args.input)
    print(f'Sweep: {cal.start} - {cal.stop} Hz')
    print(f'Calibration: {cal.cal_start} - {cal.cal_stop} Hz, {cal.cal_points} points')
    print(f'Calibration status: 0x{cal.cal_status:04X}')
    print(f'Checksum: 0x{cal.checksum:08X}, {"valid" if cal.checksum_valid else "not valid"}')

    if args.output:
        write(args.output, cal)

    if args.csv:
        with open(args.csv, 'w', newline='') as f:
            f.write('frequency,' + ','.join(f'term{i}_re,term{i}_im' for i in range(TERMS)) + '\n')
            for i, freq in enumerate(cal.frequencies()):
                values = ','.join(f'{t[i].real!r},{t[i].imag!r}' for t in cal.terms)
                f.write(f'{freq:.0f},{values}\n')


if __name__ == '__main__':
    main()
