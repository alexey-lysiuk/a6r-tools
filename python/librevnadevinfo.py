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

from libreVNA import libreVNA

vna = libreVNA('localhost', 19542)

commands = (
    '*IDN',
    'DEVice:INFo:FWREVision',
    'DEVice:INFo:HWREVision',
    'DEVice:INFo:LIMits:MINFrequency',
    'DEVice:INFo:LIMits:MAXFrequency',
    'DEVice:INFo:LIMits:MINIFBW',
    'DEVice:INFo:LIMits:MAXIFBW',
    'DEVice:INFo:LIMits:MAXPoints',
    'DEVice:INFo:LIMits:MINPOWer',
    'DEVice:INFo:LIMits:MAXPOWer',
    'DEVice:INFo:LIMits:MINRBW',
    'DEVice:INFo:LIMits:MAXRBW',
    'DEVice:INFo:LIMits:MAXHARMonicfrequency',
    'DEVice:INFo:TEMPeratures',
)

for command in commands:
    result = vna.query(command + '?')
    print(f'{command}: {result}')
