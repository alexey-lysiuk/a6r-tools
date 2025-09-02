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


def _decode(binary: typing.Union[bytearray, bytes]) -> str:
    return binary.split(b'\0')[0].decode('latin_1')


def _pack(fmt: str, stream: typing.BinaryIO, *args):
    data = struct.pack(fmt, *args)
    stream.write(data)


def _unpack(fmt: str, stream: typing.BinaryIO):
    size = struct.calcsize(fmt)
    data = stream.read(size)
    return struct.unpack(fmt, data)


def _calculate_checksum(stream: typing.BinaryIO, start_pos: int) -> int:
    stream.seek(start_pos)

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/flash.c#L146
    checksum_bytes = 1576  # == (void*)&setting.checksum - (void*)&setting
    rawdata = stream.read(checksum_bytes)
    uints = struct.unpack(f'<{checksum_bytes // 4}I', rawdata)

    checksum = 0
    mask = (1 << 32) - 1

    for n in uints:
        checksum = (checksum >> 31) | (checksum << 1)
        checksum &= mask
        checksum += n
        checksum &= mask

    return checksum


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

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L911-L913
    M_NORMAL = 0
    M_REFERENCE = 1
    M_DELTA = 2
    M_NOISE = 4
    M_STORED = 8
    M_AVER = 16
    M_TRACKING = 32
    M_DELETE = 64

    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L915-L917
    M_DISABLED = 0
    M_ENABLED = 1

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


class Struct:
    class JSONEncoder(json.JSONEncoder):
        def default(self, o):
            if isinstance(o, Struct):
                return o.__dict__

            return json.JSONEncoder.default(self, o)

    def from_dict(self, dictionary: dict):
        keys = set(key for key in self.__dict__)

        for key, value in dictionary.items():
            if key in keys and not Struct._convert_list(value, self.__dict__[key]):
                self.__dict__[key] = value

    @staticmethod
    def _has_struct_items(left, right):
        return isinstance(left, list) and len(left) > 0 and isinstance(left[0], Struct) \
            and isinstance(right, list) and len(right) > 0 and isinstance(right[0], dict)

    @staticmethod
    def _convert_list(src, dst) -> bool:
        if isinstance(src, list) and len(src) > 0 and isinstance(dst, list) and len(dst) > 0:
            for src_item, dst_item in zip(src, dst):
                if isinstance(src_item, dict) and isinstance(dst_item, Struct):
                    dst_item.from_dict(src_item)
                elif isinstance(src_item, list) and isinstance(dst_item, list):
                    if not Struct._convert_list(src_item, dst_item):
                        return False
                else:
                    return False
        else:
            return False

        return True


class Marker(Struct):
    def __init__(self):
        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L937-L944
        self.mtype = 0  # uint8_t
        self.enabled = 0  # uint8_t
        self.ref = 0  # uint8_t
        self.trace = 0  # uint8_t
        self.index = 0  # uint8_t
        self.frequency = 0  # freq_t (uint64_t)

    def from_binary(self, stream: typing.BinaryIO):
        self.mtype, self.enabled, self.ref, self.trace, self.index, self.frequency = _unpack(_Formats.MARKER, stream)

    def to_binary(self, stream: typing.BinaryIO):
        _pack(_Formats.MARKER, stream, self.mtype, self.enabled, self.ref, self.trace, self.index, self.frequency)


class Limit(Struct):
    def __init__(self):
        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L953-L958
        self.enabled = 0  # uint8_t
        self.level = 0.0  # float
        self.frequency = 0  # # freq_t (uint64_t)
        self.index = 0  # int16_t

    def from_binary(self, stream: typing.BinaryIO):
        self.enabled, self.level, self.frequency, self.index = _unpack(_Formats.LIMIT, stream)

    def to_binary(self, stream: typing.BinaryIO):
        _pack(_Formats.LIMIT, stream, self.enabled, self.level, self.frequency, self.index)


class Band(Struct):
    def __init__(self):
        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1207-L1219
        self.name = ''  # char[9]
        self.enabled = False
        self.start = 0  # freq_t (uint64_t)
        self.end = 0  # freq_t (uint64_t)
        self.level = 0.0  # float
        self.start_index = 0  # int
        self.stop_index = 0  # int

    def from_binary(self, stream: typing.BinaryIO):
        name, self.enabled, self.start, self.end, self.level, \
            self.start_index, self.stop_index = _unpack(_Formats.BAND, stream)
        self.name = _decode(name)

    def to_binary(self, stream: typing.BinaryIO):
        _pack(_Formats.BAND, stream, self.name.encode(), self.enabled, self.start, self.end,
            self.level, self.start_index, self.stop_index)


class Preset(Struct):
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L197
    MARKER_COUNT = 8
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L198
    TRACES_MAX = 4
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L948
    LIMITS_MAX = 8
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L952
    REFERENCE_MAX = TRACES_MAX
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L963
    MARKERS_MAX = MARKER_COUNT
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1208
    BANDS_MAX = 8
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1361
    PRESET_NAME_LENGTH = 10
    # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1502
    SETTING_MAGIC = 0x434f4e6d

    def __init__(self):
        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1224-L1238
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
        self.unit_scale = 0.0  # float
        self.normalize_level = 0.0  # float
        self.frequency_step = 1781737  # freq_t (uint64_t)
        self.frequency0 = 0  # freq_t (uint64_t)
        self.frequency1 = 800000000  # freq_t (uint64_t)
        self.frequency_var = 0  # freq_t (uint64_t)
        self.frequency_IF = 977400000  # freq_t (uint64_t)
        self.frequency_offset = 100000000  # freq_t (uint64_t)
        self.trace_scale = 10.0  # float
        self.trace_refpos = -10.0  # float
        self._markers = [Marker() for _ in range(Preset.MARKERS_MAX)]  # marker_t
        self.limits = [[Limit() for _ in range(Preset.REFERENCE_MAX)] for _ in range(Preset.LIMITS_MAX)]  # limit_t
        self.sweep_time_us = 0  # systime_t (uint32_t)
        self.measure_sweep_time_us = 0  # systime_t (uint32_t)
        self.actual_sweep_time_us = 0  # systime_t (uint32_t)
        self.additional_step_delay_us = 0  # systime_t (uint32_t)
        self.trigger_grid = 0  # uint32_t

        # https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1351-L1366
        self.ultra = 0  # uint8_t
        self.extra_lna = False
        self.R = 0  # int
        self.exp_aver = 0  # int32_t
        self.increased_R = False
        self.mixer_output = True
        self.interval = 0  # uint32_t
        self.preset_name = ''  # char[PRESET_NAME_LENGTH]
        self.dBuV = False
        self.test_argument = 0  # int64_t

    def from_binary(self, stream: typing.BinaryIO):
        start_pos = stream.tell()

        magic = _unpack(_Formats.MAGIC, stream)[0]
        assert magic == self.SETTING_MAGIC

        self.auto_reflevel, self.auto_attenuation, self.mirror_masking, self.tracking_output, \
            self.mute, self.auto_if, self.sweep, self.pulse = _unpack(_Formats.PRESET_1, stream)

        self.stored = _unpack(_Formats.BOOL_TRACES, stream)
        self.normalized = _unpack(_Formats.BOOL_TRACES, stream)
        stream.seek(4, io.SEEK_CUR)  # skip padding bytes

        self._load_struct_items(stream, self.bands, self.BANDS_MAX)

        self.mode, self.below_IF, self.unit, self.agc, self.lna, self.modulation, self.trigger, \
            self.trigger_mode, self.trigger_direction, self.trigger_beep, self.trigger_auto_save, \
            self.step_delay_mode, self.waterfall, self.level_meter = _unpack(_Formats.PRESET_2, stream)

        self.average = _unpack(_Formats.UINT8_TRACES, stream)
        self.subtract = _unpack(_Formats.UINT8_TRACES, stream)

        self.measurement, self.spur_removal, self.disable_correction, self.normalized_trace, self.listen, \
            self.tracking, self.atten_step, self._active_marker, self.unit_scale_index, self.noise, \
            self.lo_drive, self.rx_drive, self.test, self.harmonic, self.fast_speedup, self.faster_speedup, \
            self._traces, self.draw_line, self.lock_display, self.jog_jump, self.multi_band, \
            self.multi_trace, self.trigger_trace, self.repeat, self.linearity_step, self._sweep_points, \
            self.attenuate_x2, self.step_delay, self.offset_delay, self.freq_mode, self.refer, \
            self.modulation_depth_x100, self.modulation_deviation_div100, self.decay, self.attack, \
            self.slider_position, self.slider_span, self.rbw_x10, self.vbw_x100 = _unpack(_Formats.PRESET_3, stream)
        self.scan_after_dirty = _unpack(_Formats.UINT_TRACES, stream)
        self.modulation_frequency, self.reflevel, self.scale, self.external_gain, self.trigger_level, self.level, \
            self.level_sweep, self.unit_scale, self.normalize_level, self.frequency_step, self.frequency0, \
            self.frequency1, self.frequency_var, self.frequency_IF, self.frequency_offset, self.trace_scale, \
            self.trace_refpos = _unpack(_Formats.PRESET_4, stream)

        self._load_struct_items(stream, self._markers, self.MARKERS_MAX)

        assert len(self.limits) == self.LIMITS_MAX

        for limit in self.limits:
            self._load_struct_items(stream, limit, self.REFERENCE_MAX)

        self.sweep_time_us, self.measure_sweep_time_us, self.actual_sweep_time_us, self.additional_step_delay_us, \
            self.trigger_grid, self.ultra, self.extra_lna, self.R, self.exp_aver, self.increased_R, self.mixer_output, \
            self.interval, name, self.dBuV, self.test_argument = _unpack(_Formats.PRESET_5, stream)
        self.preset_name = _decode(name)

        file_checksum = _unpack(_Formats.CHECKSUM, stream)[0]
        checksum = _calculate_checksum(stream, start_pos)

        assert checksum == file_checksum

    @staticmethod
    def _load_struct_items(stream: typing.BinaryIO, collection: list, count: int):
        # TODO: Ensure correct item count in collection
        assert len(collection) == count

        for item in collection:
            item.from_binary(stream)

    def to_binary(self, stream: typing.BinaryIO):
        # TODO: Ensure correct item count in collections

        start_pos = stream.tell()

        _pack(_Formats.MAGIC, stream, self.SETTING_MAGIC)
        _pack(_Formats.PRESET_1, stream,
            self.auto_reflevel, self.auto_attenuation, self.mirror_masking,
            self.tracking_output, self.mute, self.auto_if, self.sweep, self.pulse)

        _pack(_Formats.BOOL_TRACES, stream, *self.stored)
        _pack(_Formats.BOOL_TRACES, stream, *self.normalized)
        _pack('<I', stream, 0)  # write padding bytes

        self._save_struct_items(stream, self.bands, self.BANDS_MAX)

        _pack(_Formats.PRESET_2, stream, \
            self.mode, self.below_IF, self.unit, self.agc, self.lna, self.modulation, self.trigger, \
            self.trigger_mode, self.trigger_direction, self.trigger_beep, self.trigger_auto_save, \
            self.step_delay_mode, self.waterfall, self.level_meter)

        _pack(_Formats.UINT8_TRACES, stream, *self.average)
        _pack(_Formats.UINT8_TRACES, stream, *self.subtract)

        _pack(_Formats.PRESET_3, stream, \
            self.measurement, self.spur_removal, self.disable_correction, self.normalized_trace, self.listen, \
            self.tracking, self.atten_step, self._active_marker, self.unit_scale_index, self.noise, \
            self.lo_drive, self.rx_drive, self.test, self.harmonic, self.fast_speedup, self.faster_speedup, \
            self._traces, self.draw_line, self.lock_display, self.jog_jump, self.multi_band, \
            self.multi_trace, self.trigger_trace, self.repeat, self.linearity_step, self._sweep_points, \
            self.attenuate_x2, self.step_delay, self.offset_delay, self.freq_mode, self.refer, \
            self.modulation_depth_x100, self.modulation_deviation_div100, self.decay, self.attack, \
            self.slider_position, self.slider_span, self.rbw_x10, self.vbw_x100)
        _pack(_Formats.UINT_TRACES, stream, *self.scan_after_dirty)
        _pack(_Formats.PRESET_4, stream, \
            self.modulation_frequency, self.reflevel, self.scale, self.external_gain, self.trigger_level, self.level, \
            self.level_sweep, self.unit_scale, self.normalize_level, self.frequency_step, self.frequency0, \
            self.frequency1, self.frequency_var, self.frequency_IF, self.frequency_offset, self.trace_scale, \
            self.trace_refpos)

        self._save_struct_items(stream, self._markers, self.MARKERS_MAX)

        for limit in self.limits:
            self._save_struct_items(stream, limit, self.REFERENCE_MAX)

        _pack(_Formats.PRESET_5, stream, \
            self.sweep_time_us, self.measure_sweep_time_us, self.actual_sweep_time_us, self.additional_step_delay_us, \
            self.trigger_grid, self.ultra, self.extra_lna, self.R, self.exp_aver, self.increased_R, self.mixer_output, \
            self.interval, self.preset_name.encode(), self.dBuV, self.test_argument)

        checksum = _calculate_checksum(stream, start_pos)
        _pack(_Formats.CHECKSUM, checksum)

    @staticmethod
    def _save_struct_items(stream: typing.BinaryIO, collection: list, count: int):
        # TODO: Ensure correct item count in collection
        assert len(collection) == count

        for item in collection:
            item.to_binary(stream)

    def from_json(self, stream: typing.TextIO):
        self.from_dict(json.load(stream))

    def to_json(self, indent:int = 4):
        return json.dumps(self.__dict__, cls=Struct.JSONEncoder, indent=indent)


class _Formats:
    MARKER = '<5B3xQ'
    LIMIT = '<B3xfQh6x'
    BAND = '<9s?6x2Qf2i4x'
    MAGIC = '<I'
    PRESET_1 = '<8?'
    PRESET_2 = '<14B'
    PRESET_3 = '<3BbBbBb15Bx3Hh3Hh2H2x3iQ2I'
    PRESET_4 = '<9f4x6Q2f'
    PRESET_5 = f'<5IB?2x2i2?2xI{Preset.PRESET_NAME_LENGTH}s?5xQ'
    CHECKSUM = '<I4x'
    BOOL_TRACES = f'<{Preset.TRACES_MAX}?'
    UINT8_TRACES = f'<{Preset.TRACES_MAX}B'
    UINT_TRACES = f'<{Preset.TRACES_MAX}I'


def convert(path: str):
    preset = Preset()

    if path.endswith('.prs'):
        stream = open(path, 'rb')
        preset.from_binary(stream)
        print(preset.to_json())
    elif path.endswith('.json'):
        text_stream = open(path, encoding='ascii')
        preset.from_json(text_stream)
        binary_stream = open(path + '.prs', 'wb')
        preset.to_binary(binary_stream)
    else:
        assert False


def main():
    if len(sys.argv) == 1:
        print(f'Usage: {sys.argv[0]} .prs ...')
        sys.exit(1)

    for path in sys.argv[1:]:
        convert(path)


if '__main__' == __name__:
    main()
