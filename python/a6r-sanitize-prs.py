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

import argparse
import importlib
import io


Preset = importlib.import_module('a6r-prs').Preset


def sanitize(path: str, markers: int):
    with open(path, 'rb') as f:
        preset = Preset()
        preset.from_binary(f)

    start_frequency = preset.frequency0

    for band in preset.bands:
        if band.enabled:
            start_frequency = band.start
            break

    for marker in preset.markers:
        marker.frequency = start_frequency if marker.enabled else 0

    preset.scan_after_dirty = [0 for _ in range(Preset.TRACES_MAX)]
    preset.waterfall = 1
    preset.preset_name = ''

    with open(path, 'wb') as f:
        stream = io.BytesIO()
        preset.to_binary(stream)
        f.write(stream.getbuffer())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('presets', metavar='preset', type=str, nargs='+', help='preset to sanitize')
    parser.add_argument('-M', '--markers', metavar='count', type=int, default=4, help='desired number of markers')
    args = parser.parse_args()

    for preset in args.presets:
        sanitize(preset, args.markers)


if '__main__' == __name__:
    main()
