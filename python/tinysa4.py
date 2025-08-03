#!/usr/bin/env python3

import serial
import struct
import sys
from optparse import OptionParser
from serial.tools import list_ports


BMP_HEADER = \
    b'BMz\xb0\x04\x00\x00\x00\x00\x00z\x00\x00\x00l\x00\x00\x00\xe0\x01\x00\x00\xc0\xfe\xff\xff\x01'\
    b'\x00\x10\x00\x03\x00\x00\x00\x00\xb0\x04\x00\xc4\x0e\x00\x00\xc4\x0e\x00\x00\x00\x00\x00\x00'\
    b'\x00\x00\x00\x00\x00\xf8\x00\x00\xe0\x07\x00\x00\x1f\x00\x00\x00\x00\x00\x00\x00BGRs\x00\x00'\
    b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'\
    b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'


class TinySA(object):
    VID = 0x0483  # 1155
    PID = 0x5740  # 22336

    MINIMUM_POINT_COUNT = 101

    def __init__(self, dev=None):
        self.dev = dev or self._getport()
        self.serial = None
        self.frequencies = None
        self.points = 0

    def set_frequencies(self, start=1e6, stop=350e6, points=MINIMUM_POINT_COUNT):
        if points < self.MINIMUM_POINT_COUNT:
            points = self.MINIMUM_POINT_COUNT

        if start < 0:
            start = 0

        if stop < 0:
            stop = 0

        if start > stop:
            start, stop = stop, start

        self.frequencies = [start + x * (stop - start) / (points - 1) for x in range(points)]
        self.points = points

    def open(self):
        if self.serial is None:
            self.serial = serial.Serial(self.dev)

    def close(self):
        if self.serial:
            self.serial.close()
        self.serial = None

    def send_command(self, cmd):
        self.open()
        self.serial.write(cmd.encode())
        self.serial.readline()  # discard empty line

    def cmd(self, text):
        self.open()
        self.serial.write((text + '\r').encode())
        self.serial.readline()  # discard empty line
        data = self.fetch_data()
        return data

    def set_sweep(self, start, stop):
        if start is not None:
            self.send_command('sweep start %d\r' % start)
        if stop is not None:
            self.send_command('sweep stop %d\r' % stop)

    def set_span(self, span):
        if span is not None:
            self.send_command('sweep span %d\r' % span)

    def set_center(self, center):
        if center is not None:
            self.send_command('sweep center %d\r' % center)

    def set_level(self, level):
        if level is not None:
            self.send_command('level %d\r' % level)

    def set_output(self, on):
        if on is not None:
            if on:
                self.send_command('output on\r')
            else:
                self.send_command('output off\r')

    def set_low_output(self):
        self.send_command('mode low output\r')

    def set_low_input(self):
        self.send_command('mode low input\r')

    def set_high_input(self):
        self.send_command('mode high input\r')

    def set_frequency(self, freq):
        if freq is not None:
            self.send_command('freq %d\r' % freq)

    def measure(self, freq):
        if freq is not None:
            self.send_command('hop %d 2\r' % freq)
            data = self.fetch_data()
            for line in data.split('\n'):
                if line:
                    return float(line)

    def temperature(self):
        self.send_command('k\r')
        data = self.fetch_data()
        for line in data.split('\n'):
            if line:
                return float(line)

    def rbw(self, data=0):
        if data == 0:
            self.send_command('rbw auto\r')
            return
        if data < 1:
            self.send_command('rbw %f\r' % data)
            return
        if data >= 1:
            self.send_command('rbw %d\r' % data)

    def fetch_data(self):
        result = ''
        line = ''
        while True:
            c = self.serial.read().decode('utf-8')
            if c == chr(13):
                continue  # ignore CR
            line += c
            if c == chr(10):
                result += line
                line = ''
                continue
            if line.endswith('ch>'):
                # stop on prompt
                break
        return result

    def resume(self):
        self.send_command('resume\r')

    def pause(self):
        self.send_command('pause\r')

    def marker_value(self, nr=1):
        self.send_command('marker %d\r' % nr)
        data = self.fetch_data()
        line = data.split('\n')[0]
        if line:
            dl = line.strip().split(' ')
            if len(dl) >= 4:
                d = line.strip().split(' ')[3]
                return float(d)
        return 0

    def list(self, file=''):
        self.send_command('sd_list %s\r' % file)
        data = self.fetch_data()
        return data

    def read(self, file):
        self.send_command('sd_read %s\r' % file)
        f = '<1i'
        b = self.serial.read(4)
        size = struct.unpack(f, b)
        size = size[0]
        print(size)
        data = self.serial.read(size)
        return data

    def data(self, array=2):
        self.send_command('data %d\r' % array)
        data = self.fetch_data()
        x = []
        for line in data.split('\n'):
            if line:
                x.append(float(line))
        return x

    def fetch_frequencies(self):
        self.send_command('frequencies\r')
        data = self.fetch_data()
        x = []
        for line in data.split('\n'):
            if line:
                x.append(float(line))
        self.frequencies = x

    def send_scan(self, start=1e6, stop=900e6, points=None):
        if points:
            self.send_command('scan %d %d %d\r' % (start, stop, points))
        else:
            self.send_command('scan %d %d\r' % (start, stop))

    def scan(self):
        segment_length = 101
        array0 = []
        array1 = []
        if self.frequencies is None:
            self.fetch_frequencies()
        freqs = self.frequencies
        while len(freqs) > 0:
            seg_start = freqs[0]
            seg_stop = freqs[segment_length - 1] if len(freqs) >= segment_length else freqs[-1]
            length = segment_length if len(freqs) >= segment_length else len(freqs)
            self.send_scan(seg_start, seg_stop, length)
            array0.extend(self.data(0))
            array1.extend(self.data(1))
            freqs = freqs[segment_length:]
        self.resume()
        return array0, array1

    def capture(self):
        self.send_command('capture\r')

        width = 480
        height = 320
        pixels_length = width * height * 2
        pixels = self.serial.read(pixels_length)

        # Swap rows as pixels are stored "bottom-up", starting in the lower left corner,
        # going from left to right, and then row by row from the bottom to the top
        return BMP_HEADER + bytes(pixels[x ^ 1] for x in range(pixels_length))

    def write_csv(self, x, name):
        f = open(name, 'w')
        for i in range(len(x)):
            print('%d, ' % self.frequencies[i], '%2.2f' % x[i], file=f)

    @staticmethod
    def _getport() -> str:
        device_list = list_ports.comports()

        for device in device_list:
            if device.vid == TinySA.VID and device.pid == TinySA.PID:
                return device.device

        raise OSError('device not found')


def main():
    parser = OptionParser(usage='%prog: [options]')
    parser.add_option('-c', '--scan', dest='scan',
                      action='store_true', default=False,
                      help='scan by script', metavar='SCAN')
    parser.add_option('-S', '--start', dest='start',
                      type='float', default=1e6,
                      help='start frequency', metavar='START')
    parser.add_option('-E', '--stop', dest='stop',
                      type='float', default=900e6,
                      help='stop frequency', metavar='STOP')
    parser.add_option('-N', '--points', dest='points',
                      type='int', default=101,
                      help='scan points', metavar='POINTS')
    parser.add_option('-P', '--port', type='int', dest='port',
                      help='port', metavar='PORT')
    parser.add_option('-d', '--dev', dest='device',
                      help='device node', metavar='DEV')
    parser.add_option('-v', '--verbose',
                      action='store_true', dest='verbose', default=False,
                      help='verbose output')
    parser.add_option('-C', '--capture', dest='capture',
                      help='capture current display to FILE', metavar='FILE')
    parser.add_option('-e', '--command', dest='command', action='append',
                      help='send raw command', metavar='COMMAND')
    parser.add_option('-o', '--write', dest='save',
                      help='write CSV file', metavar='SAVE')
    parser.add_option('-l', '--list', dest='list',
                      help='list SD card files', metavar='LIST')
    parser.add_option('-r', '--read', dest='read',
                      help='read SD card files', metavar='READ')

    if len(sys.argv) == 1:
        parser.print_help()
        return

    (opt, args) = parser.parse_args()

    nv = TinySA(opt.device)
    nv.set_frequencies(opt.start, opt.stop, opt.points)

    if opt.command:
        for c in opt.command:
            nv.send_command(c + '\r')

        data = nv.fetch_data()
        print(data)
    elif opt.capture:
        print('capturing...')
        img = nv.capture()
        with open(opt.capture, 'wb') as f:
            f.write(img)
    elif opt.list:
        data = nv.list(opt.list)
        print(data)
    elif opt.read:
        data = nv.read(opt.read)

        if len(args) > 0:
            f = open(args[0], 'wb')
            f.write(data)
            f.close()
        else:
            print(data)
    elif opt.save or opt.scan:
        p = int(opt.port) if opt.port else 0

        if opt.scan or opt.points > 101:
            s = nv.scan()
            s = s[p]
        else:
            if opt.start or opt.stop:
                nv.set_sweep(opt.start, opt.stop)
            nv.fetch_frequencies()
            s = nv.data(p)

        if opt.save:
            nv.write_csv(s, opt.save)


if __name__ == '__main__':
    main()
