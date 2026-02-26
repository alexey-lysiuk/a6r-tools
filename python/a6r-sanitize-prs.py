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

_prs = importlib.import_module('a6r-prs')
Enums = _prs.Enums
Marker = _prs.Marker
Preset = _prs.Preset


def sanitize(path: str, markers: int, waterfall: int):
    with open(path, 'rb') as f:
        preset = Preset()
        preset.from_binary(f)

    start_frequency = preset.frequency0

    for band in preset.bands:
        if band.enabled:
            start_frequency = band.start
            break

    if markers < 0:
        markers = 0
    elif markers > Preset.MARKERS_MAX:
        markers = Preset.MARKERS_MAX

    for i in range(Preset.MARKERS_MAX):
        if i < markers:
            marker = preset.markers[i]
            marker.mtype = Enums.M_TRACKING
            marker.enabled = True
            marker.ref = 0
            marker.trace = 3 if i > 0 else 0
            marker.index = 0
            marker.frequency = start_frequency
        else:
            preset.markers[i] = Marker()

    preset.scan_after_dirty = [0 for _ in range(Preset.TRACES_MAX)]
    preset.waterfall = max(Enums.W_OFF, min(waterfall, Enums.W_SUPER))
    preset.actual_sweep_time_us = 0
    preset.preset_name = ''

    with open(path, 'wb') as f:
        stream = io.BytesIO()
        preset.to_binary(stream)
        f.write(stream.getbuffer())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('presets', metavar='preset', type=str, nargs='*', help='preset to sanitize')
    parser.add_argument('-M', '--markers', metavar='count', type=int, default=4,
                        help=f'desired number of markers, 0..{Preset.MARKERS_MAX}')
    parser.add_argument('-W', '--waterfall', metavar='number', type=int, default=1,
                        help=f'desired waterfall height, {Enums.W_OFF}..{Enums.W_SUPER}')
    args = parser.parse_args()

    if len(args.presets) == 0:
        parser.print_help()
        return

    for preset in args.presets:
        sanitize(preset, args.markers, args.waterfall)

if '__main__' == __name__:
    main()
