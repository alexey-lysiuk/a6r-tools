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
import re
import sys

from PIL import Image
import pytesseract


_UNIT_MULT = {
    'Hz': 1,
    'kHz': 1_000,
    'MHz': 1_000_000,
    'GHz': 1_000_000_000,
}

# Pattern for frequency labels shown on the tinySA Ultra display.
# The value group allows digit-like characters that Tesseract commonly misreads.
_VALUE_CHARS = r'[0-9OoIlBbSs|][0-9OoIlBbSs|.]*'
_FREQ_RE = re.compile(
    r'(START|STOP|CENTER|SPAN)\s+(' + _VALUE_CHARS + r')\s*(GHz|MHz|kHz|Hz)',
    re.IGNORECASE
)


def _normalize_unit(unit_raw: str) -> str:
    return {'hz': 'Hz', 'khz': 'kHz', 'mhz': 'MHz', 'ghz': 'GHz'}.get(unit_raw.lower(), unit_raw)


def _parse_freq(value_str: str, unit_str: str):
    mult = _UNIT_MULT.get(unit_str)
    if mult is None:
        return None

    # Correct common OCR misreadings of digits.
    s = value_str
    s = s.replace('O', '0').replace('o', '0')
    s = s.replace('I', '1').replace('l', '1').replace('|', '1')
    s = s.replace('B', '8').replace('b', '8')

    # 'S' is ambiguous: Tesseract misreads 8 as S most often, but 5 as S is also possible.
    for s_sub in ('8', '5'):
        val_str = s.replace('S', s_sub).replace('s', s_sub)
        val_str = val_str.strip('.,;:')
        try:
            val = float(val_str)
            if val >= 0:
                return round(val * mult)
        except ValueError:
            continue

    return None


def _format_freq(hz: int) -> str:
    if hz == 0:
        return '0 MHz'
    if hz >= 1_000_000_000 and hz % 1_000_000_000 == 0:
        return f'{hz // 1_000_000_000} GHz'
    if hz >= 1_000_000:
        v = hz / 1_000_000
        return f'{v:g} MHz'
    if hz >= 1_000:
        v = hz / 1_000
        return f'{v:g} kHz'
    return f'{hz} Hz'


def _extract_range(text: str):
    matches = {}

    for m in _FREQ_RE.finditer(text):
        keyword = m.group(1).upper()
        unit = _normalize_unit(m.group(3))
        hz = _parse_freq(m.group(2), unit)

        if hz is not None and keyword not in matches:
            matches[keyword] = hz

    if 'START' in matches and 'STOP' in matches:
        start = _format_freq(matches['START'])
        stop = _format_freq(matches['STOP'])
        return f'{start} to {stop}'

    if 'CENTER' in matches and 'SPAN' in matches:
        center = _format_freq(matches['CENTER'])
        span = _format_freq(matches['SPAN'])
        return f'{center} center, {span} span'

    return None


_FREQ_BAR_HEIGHT = 10


def _process_file(filename: str):
    try:
        img = Image.open(filename).convert('RGB')
    except Exception as e:
        print(f'Error: cannot open {filename}: {e}', file=sys.stderr)
        return None

    w, h = img.size
    freq_bar = img.crop((0, h - _FREQ_BAR_HEIGHT, w, h))
    big = freq_bar.resize((w * 3, _FREQ_BAR_HEIGHT * 3), Image.NEAREST)
    text = pytesseract.image_to_string(big, config='--psm 11')

    result = _extract_range(text)

    if result is None:
        print(f'Error: cannot extract frequency range from {filename}', file=sys.stderr)

    return result


def main():
    parser = argparse.ArgumentParser(
        description='Extract frequency range from tinySA Ultra capture images')
    parser.add_argument('files', metavar='image-file', type=str, nargs='+')
    args = parser.parse_args()

    files = args.files
    show_filename = len(files) > 1

    for filename in files:
        result = _process_file(filename)

        if result is not None:
            if show_filename:
                print(f'{filename}: {result}')
            else:
                print(result)


if '__main__' == __name__:
    main()
