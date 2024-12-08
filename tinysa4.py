#!/usr/bin/env python3

import serial
import struct
import sys
from optparse import OptionParser
from serial.tools import list_ports

VID = 0x0483  # 1155
PID = 0x5740  # 22336
REF_LEVEL = 1 << 9


# Get tinysa device automatically
def getport() -> str:
    device_list = list_ports.comports()

    for device in device_list:
        if device.vid == VID and device.pid == PID:
            return device.device

    raise OSError("device not found")


class TinySA:
    def __init__(self, dev=None):
        self.dev = dev or getport()
        self.serial = None
        self._frequencies = None
        self.points = 101

    @property
    def frequencies(self):
        return self._frequencies

    def set_frequencies(self, start=1e6, stop=350e6, points=None):
        if points:
            self.points = points

        if start > stop:
            start, stop = stop, start
        elif self.points < 2:
            self._frequencies = [start + (stop - start) / 2]
            self.points = 1
        else:
            self._frequencies = [start + x * (stop - start) / (self.points - 1) for x in range(self.points)]

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
        self.serial.write((text + "\r").encode())
        self.serial.readline()  # discard empty line
        data = self.fetch_data()
        return data

    def set_sweep(self, start, stop):
        if start is not None:
            self.send_command("sweep start %d\r" % start)
        if stop is not None:
            self.send_command("sweep stop %d\r" % stop)

    def set_span(self, span):
        if span is not None:
            self.send_command("sweep span %d\r" % span)

    def set_center(self, center):
        if center is not None:
            self.send_command("sweep center %d\r" % center)

    def set_level(self, level):
        if level is not None:
            self.send_command("level %d\r" % level)

    def set_output(self, on):
        if on is not None:
            if on:
                self.send_command("output on\r")
            else:
                self.send_command("output off\r")

    def set_low_output(self):
        self.send_command("mode low output\r")

    def set_low_input(self):
        self.send_command("mode low input\r")

    def set_high_input(self):
        self.send_command("mode high input\r")

    def set_frequency(self, freq):
        if freq is not None:
            self.send_command("freq %d\r" % freq)

    def measure(self, freq):
        if freq is not None:
            self.send_command("hop %d 2\r" % freq)
            data = self.fetch_data()
            for line in data.split('\n'):
                if line:
                    return float(line)

    def temperature(self):
        self.send_command("k\r")
        data = self.fetch_data()
        for line in data.split('\n'):
            if line:
                return float(line)

    def rbw(self, data=0):
        if data == 0:
            self.send_command("rbw auto\r")
            return
        if data < 1:
            self.send_command("rbw %f\r" % data)
            return
        if data >= 1:
            self.send_command("rbw %d\r" % data)

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
        self.send_command("resume\r")

    def pause(self):
        self.send_command("pause\r")

    def marker_value(self, nr=1):
        self.send_command("marker %d\r" % nr)
        data = self.fetch_data()
        line = data.split('\n')[0]
        if line:
            dl = line.strip().split(' ')
            if len(dl) >= 4:
                d = line.strip().split(' ')[3]
                return float(d)
        return 0

    def list(self, file=""):
        self.send_command("sd_list %s\r" % file)
        data = self.fetch_data()
        return data

    def read(self, file):
        self.send_command("sd_read %s\r" % file)
        f = "<1i"
        b = self.serial.read(4)
        size = struct.unpack(f, b)
        size = size[0]
        print(size)
        data = self.serial.read(size)
        return data

    def data(self, array=2):
        self.send_command("data %d\r" % array)
        data = self.fetch_data()
        x = []
        for line in data.split('\n'):
            if line:
                x.append(float(line))
        return np.array(x)

    def fetch_frequencies(self):
        self.send_command("frequencies\r")
        data = self.fetch_data()
        x = []
        for line in data.split('\n'):
            if line:
                x.append(float(line))
        self._frequencies = np.array(x)

    def send_scan(self, start=1e6, stop=900e6, points=None):
        if points:
            self.send_command("scan %d %d %d\r" % (start, stop, points))
        else:
            self.send_command("scan %d %d\r" % (start, stop))

    def scan(self):
        segment_length = 101
        array0 = []
        array1 = []
        if self._frequencies is None:
            self.fetch_frequencies()
        freqs = self._frequencies
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
        from PIL import Image
        width = 480
        height = 320
        if width == 320:
            f = ">76800H"
        else:
            f = ">153600H"
        self.send_command("capture\r")
        b = self.serial.read(width * height * 2)
        x = struct.unpack(f, b)
        # convert pixel format from 565(RGB) to 8888(RGBA)
        arr = np.array(x, dtype=np.uint32)
        arr = 0xFF000000 + ((arr & 0xF800) >> 8) + ((arr & 0x07E0) << 5) + ((arr & 0x001F) << 19)
        return Image.frombuffer('RGBA', (width, height), arr, 'raw', 'RGBA', 0, 1)

    def write_csv(self, x, name):
        f = open(name, "w")
        for i in range(len(x)):
            print("%d, " % self.frequencies[i], "%2.2f" % x[i], file=f)


def main():
    parser = OptionParser(usage="%prog: [options]")
    parser.add_option("-c", "--scan", dest="scan",
                      action="store_true", default=False,
                      help="scan by script", metavar="SCAN")
    parser.add_option("-S", "--start", dest="start",
                      type="float", default=1e6,
                      help="start frequency", metavar="START")
    parser.add_option("-E", "--stop", dest="stop",
                      type="float", default=900e6,
                      help="stop frequency", metavar="STOP")
    parser.add_option("-N", "--points", dest="points",
                      type="int", default=101,
                      help="scan points", metavar="POINTS")
    parser.add_option("-P", "--port", type="int", dest="port",
                      help="port", metavar="PORT")
    parser.add_option("-d", "--dev", dest="device",
                      help="device node", metavar="DEV")
    parser.add_option("-v", "--verbose",
                      action="store_true", dest="verbose", default=False,
                      help="verbose output")
    parser.add_option("-C", "--capture", dest="capture",
                      help="capture current display to FILE", metavar="FILE")
    parser.add_option("-e", dest="command", action="append",
                      help="send raw command", metavar="COMMAND")
    parser.add_option("-o", dest="save",
                      help="write CSV file", metavar="SAVE")
    parser.add_option("-l", dest="list",
                      help="list SD card files", metavar="LIST")
    parser.add_option("-r", dest="read",
                      help="read SD card files", metavar="READ")

    if len(sys.argv) == 1:
        parser.print_help()
        return

    (opt, args) = parser.parse_args()

    nv = TinySA(opt.device or getport())
    nv.set_frequencies(opt.start, opt.stop, opt.points)

    if opt.command:
        for c in opt.command:
            nv.send_command(c + "\r")

        data = nv.fetch_data()
        print(data)
    elif opt.capture:
        print("capturing...")
        img = nv.capture()
        img.save(opt.capture)
    elif opt.list:
        data = nv.list(opt.list)
        print(data)
    elif opt.read:
        data = nv.read(opt.read)

        if len(args) > 0:
            f = open(args[0], "wb")
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
