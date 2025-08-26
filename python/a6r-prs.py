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

import io
import json
import struct
import sys
import typing


def _unpack(fmt: str, stream: typing.BinaryIO):
    size = struct.calcsize(fmt)
    data = stream.read(size)
    return struct.unpack(fmt, data)


class Enums:
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L329-L331
    M_LOW = 0
    M_HIGH = 1
    M_GENLOW = 2
    M_GENHIGH = 3
    M_ULTRA = 4

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L333-L339
    MO_NONE = 0
    MO_AM = 1
    MO_NFM = 2
    MO_NFM2 = 3
    MO_NFM3 = 4
    MO_WFM = 5
    MO_EXTERNAL = 6
    MO_MAX = 7

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L779-L781
    U_DBM = 0
    U_DBMV = 1
    U_DBUV = 2
    U_RAW = 3
    U_VOLT = 4
    U_VPP = 5
    U_WATT = 6
    U_DBC = 7

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1380
    S_OFF = 0
    S_ON = 1
    S_AUTO_OFF = 2
    S_AUTO_ON = 3

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1382
    SD_NORMAL = 0
    SD_PRECISE = 1
    SD_FAST = 2
    SD_NOISE_SOURCE = 3
    SD_MANUAL = 4

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1384
    W_OFF = 0
    W_SMALL = 1
    W_BIG = 2
    W_SUPER = 3

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1862-L1864
    M_OFF = 0
    M_IMD = 1
    M_OIP3 = 2
    M_PHASE_NOISE = 3
    M_SNR = 4
    M_PASS_BAND = 5
    M_LINEARITY = 6
    M_AM = 7
    M_FM = 8
    M_THD = 9
    M_CP = 10
    M_NF_TINYSA = 11
    M_NF_STORE = 12
    M_NF_VALIDATE = 13
    M_NF_AMPLIFIER = 14
    M_DECONV = 15
    M_MAX = 16

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1867-L1869
    T_AUTO = 0
    T_NORMAL = 1
    T_SINGLE = 2
    T_DONE = 3
    T_UP = 4
    T_DOWN = 5
    T_MODE = 6
    T_PRE = 7
    T_POST = 8
    T_MID = 9
    T_BEEP = 10
    T_AUTO_SAVE = 11


class Band:
    def __init__(self):
        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1207-L1219
        self.name = ''  # char[9]
        self.enabled = False
        self.start = 0  # freq_t (uint64_t)
        self.end = 0  # freq_t (uint64_t)
        self.level = 0.0  # float
        self.start_index = 0  # int
        self.stop_index = 0  # int

    @staticmethod
    def load(stream: typing.BinaryIO) -> 'Band':
        b = Band()
        values = _unpack('<9s?6x2Qf2i4x', stream)
        b.name = values[0].split(b'\0')[0].decode('latin_1')
        b.enabled, b.start, b.end, b.level, b.start_index, b.stop_index = values[1:]
        return b


class Preset:
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L197
    MARKER_COUNT = 8
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L198
    TRACES_MAX = 4
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L963
    MARKERS_MAX = MARKER_COUNT
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1208
    BANDS_MAX = 8
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1502
    SETTING_MAGIC = 0x434f4e6d

    def __init__(self):
        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1224-L1238
        self.magic = Preset.SETTING_MAGIC  # uint32_t
        self.auto_reflevel = True
        self.auto_attenuation = True
        self.mirror_masking = False
        self.tracking_output = False
        self.mute = True
        self.auto_if = True
        self.sweep = False
        self.pulse = False
        self.stored = [False for _ in range(Preset.TRACES_MAX)]
        self.normalized = [False for _ in range(Preset.TRACES_MAX)]
        self.bands = [Band() for _ in range(Preset.BANDS_MAX)]

        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1240-L1266
        self.mode = Enums.M_LOW  # uint8_t
        self.below_IF = Enums.S_AUTO_OFF  # uint8_t
        self.unit = Enums.U_DBM  # uint8_t
        self.agc = Enums.S_AUTO_ON  # uint8_t
        self.lna = Enums.S_AUTO_OFF  # uint8_t
        self.modulation = Enums.MO_NONE  # uint8_t
        self.trigger = Enums.T_AUTO  # uint8_t
        self.trigger_mode = Enums.T_MID  # uint8_t
        self.trigger_direction = Enums.T_UP  # uint8_t
        self.trigger_beep = 0  # uint8_t
        self.trigger_auto_save = 0  # uint8_t
        self.step_delay_mode = Enums.SD_NORMAL  # uint8_t
        self.waterfall = Enums.W_OFF  # uint8_t
        self.level_meter = 0  # uint8_t
        self.average = [0 for _ in range(Preset.TRACES_MAX)]  # uint8_t
        self.subtract = [0 for _ in range(Preset.TRACES_MAX)]  # uint8_t
        self.measurement = Enums.M_OFF  # uint8_t
        self.spur_removal = Enums.S_AUTO_OFF  # uint8_t
        self.disable_correction = 0  # uint8_t
        self.normalized_trace = -1  # int8_t
        self.listen = 0  # uint8_t

        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1268-L1295
        self.tracking = 0  # int8_t
        self.atten_step = 0  # uint8_t
        self._active_marker = 0  # int8_t
        self.unit_scale_index = 0  # uint8_t
        self.noise = 5  # uint8_t
        self.lo_drive = 5  # uint8_t
        self.rx_drive = 12  # uint8_t
        self.test = 0  # uint8_t
        self.harmonic = 3  # uint8_t
        self.fast_speedup = 0  # uint8_t
        self.faster_speedup = 0  # uint8_t
        self._traces = 1  # uint8_t
        self.draw_line = 0  # uint8_t
        self.lock_display = 0  # uint8_t
        self.jog_jump = 0  # uint8_t
        self.multi_band = 0  # uint8_t
        self.multi_trace = 0  # uint8_t
        self.trigger_trace = 255  # uint8_t
        self.repeat = 1  # uint16_t
        self.linearity_step = 0  # uint16_t
        self._sweep_points = 450  # uint16_t
        self.attenuate_x2 = 0  # int16_t

        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1297-L1323
        self.step_delay = 0  # uint16_t
        self.offset_delay = 0  # uint16_t
        self.freq_mode = 0  # uint16_t
        self.refer = -1  # int16_t
        self.modulation_depth_x100 = 80  # uint16_t
        self.modulation_deviation_div100 = 30  # uint16_t
        self.decay = 20  # int
        self.attack = 1  # int
        self.slider_position = 0  # int32_t
        self.slider_span = 100000  # freq_t (uint64_t)
        self.rbw_x10 = 0  # uint32_t
        self.vbw_x100 = 0  # uint32_t
        self.scan_after_dirty = [0 for _ in range(Preset.TRACES_MAX)]  # uint32_t
        self.modulation_frequency = 1000.0  # float
        self.reflevel = -10.0  # float
        self.scale = 10.0  # float
        self.external_gain = 0.0  # float
        self.trigger_level = -150.0  # float
        self.level = 0.0  # float
        self.level_sweep = 0.0  # float

        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1325-L1346
        self.frequency_step = 1781737  # freq_t (uint64_t)
        self.frequency0 = 0  # freq_t (uint64_t)
        self.frequency1 = 800000000  # freq_t (uint64_t)
        self.frequency_var = 0  # freq_t (uint64_t)
        self.frequency_IF = 977400000  # freq_t (uint64_t)
        self.frequency_offset = 100000000  # freq_t (uint64_t)
        self.trace_scale = 10.0  # float
        self.trace_refpos = -10.0  # float
        self._markers = 0  # marker_t[MARKERS_MAX]
        self.limits = 0  # limit_t[REFERENCE_MAX][LIMITS_MAX]
        self.sweep_time_us = 0  # systime_t (uint32_t)
        self.measure_sweep_time_us = 0  # systime_t (uint32_t)
        self.actual_sweep_time_us = 0  # systime_t (uint32_t)
        self.additional_step_delay_us = 0  # systime_t (uint32_t)
        self.trigger_grid = 0  # uint32_t

    @staticmethod
    def load(stream: typing.BinaryIO) -> 'Preset':
        p = Preset()

        p.magic = _unpack('<I', stream)[0]
        assert p.magic == Preset.SETTING_MAGIC

        p.auto_reflevel, p.auto_attenuation, p.mirror_masking, p.tracking_output, \
            p.mute, p.auto_if, p.sweep, p.pulse = _unpack('<8?', stream)

        bool_trace_max_fmt = f'<{Preset.TRACES_MAX}?'
        p.stored = _unpack(bool_trace_max_fmt, stream)
        p.normalized = _unpack(bool_trace_max_fmt, stream)
        stream.seek(4, io.SEEK_CUR)  # skip padding bytes

        p.bands = [Band.load(stream) for _ in range(Preset.BANDS_MAX)]

        p.mode, p.below_IF, p.unit, p.agc, p.lna, p.modulation, p.trigger, \
            p.trigger_mode, p.trigger_direction, p.trigger_beep, p.trigger_auto_save, \
            p.step_delay_mode, p.waterfall, p.level_meter = _unpack('<14B', stream)

        uint8_trace_max_fmt = f'<{Preset.TRACES_MAX}B'
        p.average = _unpack(uint8_trace_max_fmt, stream)
        p.subtract = _unpack(uint8_trace_max_fmt, stream)

        p.measurement, p.spur_removal, p.disable_correction, p.normalized_trace, \
            p.listen = _unpack('<3BbB', stream)

        p.tracking, p.atten_step, p._active_marker, p.unit_scale_index, p.noise, \
            p.lo_drive, p.rx_drive, p.test, p.harmonic, p.fast_speedup, p.faster_speedup, \
            p._traces, p.draw_line, p.lock_display, p.jog_jump, p.multi_band, \
            p.multi_trace, p.trigger_trace = _unpack('<bBb15Bx', stream)
        p.repeat, p.linearity_step, p._sweep_points, p.attenuate_x2 = _unpack('<3Hh', stream)

        p.step_delay, p.offset_delay, p.freq_mode, p.refer, p.modulation_depth_x100, \
            p.modulation_deviation_div100, p.decay, p.attack, p.slider_position, \
            p.slider_span, p.rbw_x10, p.vbw_x100 = _unpack('<3Hh2H2x3iQ2I', stream)
        p.scan_after_dirty = _unpack(f'<{Preset.TRACES_MAX}I', stream)
        p.modulation_frequency, p.reflevel, p.scale, p.external_gain, p.trigger_level, \
            p.level, p.level_sweep = _unpack('<7f', stream)

        return p


class PresetJSONEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, Band) or isinstance(o, Preset):
            return o.__dict__

        return json.JSONEncoder.default(self, o)


def convert(path: str):
    stream = open(path, 'rb')
    preset = Preset.load(stream)

    # TODO
    print(json.dumps(preset.__dict__, cls=PresetJSONEncoder, indent=4))


def main():
    if len(sys.argv) == 1:
        print(f'Usage: {sys.argv[0]} .prs ...')
        sys.exit(1)

    for path in sys.argv[1:]:
        convert(path)


if '__main__' == __name__:
    main()
