#!/usr/bin/env python3

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
