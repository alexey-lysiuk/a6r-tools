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
import pathlib
import struct

import serial
from serial.tools import list_ports


# NanoVNA Saver: src/NanoVNASaver/Hardware/LiteVNA64.py
_CMD_READ = 0x10
_ADDR_DEVICE_VARIANT = 0xF0
_ADDR_HARDWARE_REVISION = 0xF2
_ADDR_FW_MAJOR = 0xF3
_ADDR_FW_MINOR = 0xF4

# NanoVNA Saver EXPECTED_HW_VERSION / EXPECTED_FW_VERSION are both 2.2.0.
# Updater probe keeps exact HW match and only enforces FW major for forward compatibility.
_EXPECTED_HW_MAJOR = 2
_EXPECTED_HW_MINOR = 2
_EXPECTED_FW_MAJOR = 2

_SOH = 0x01
_STX = 0x02
_EOT = 0x04
_ACK = 0x06
_NAK = 0x15
_CAN = 0x18
_C = ord('C')
_SUB = 0x1A


def _read_exact(device: serial.Serial, size: int) -> bytes:
    result = bytearray()

    while len(result) < size:
        chunk = device.read(size - len(result))

        if not chunk:
            break

        result += chunk

    return bytes(result)


def _crc16_ccitt(data: bytes) -> int:
    crc = 0

    for byte in data:
        crc ^= byte << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


def _read_register_pair(device: serial.Serial, major_reg: int, minor_reg: int) -> tuple[int, int]:
    command = struct.pack('<BBBB', _CMD_READ, major_reg, _CMD_READ, minor_reg)
    device.write(command)

    response = _read_exact(device, 2)

    if len(response) != 2:
        raise TimeoutError('Timeout reading LiteVNA-64 registers')

    return response[0], response[1]


def _find_port(baudrate: int, timeout: float) -> str:
    for port_name in sorted(port.device for port in list_ports.comports()):
        try:
            with _open_device(port_name, baudrate, timeout) as device:
                hw_version, fw_version = _probe_litevna(device)

                if _is_litevna64(hw_version, fw_version):
                    return port_name
        except (OSError, TimeoutError, RuntimeError, serial.SerialException):
            continue

    raise OSError('No matching LiteVNA-64 serial device found')


def _open_device(device_name: str, baudrate: int, timeout: float) -> serial.Serial:
    return serial.Serial(
        port=device_name,
        baudrate=baudrate,
        timeout=timeout,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
    )


def _probe_litevna(device: serial.Serial) -> tuple[tuple[int, int], tuple[int, int]]:
    hw_version = _read_register_pair(device, _ADDR_DEVICE_VARIANT, _ADDR_HARDWARE_REVISION)
    fw_version = _read_register_pair(device, _ADDR_FW_MAJOR, _ADDR_FW_MINOR)

    return hw_version, fw_version


def _is_litevna64(hw_version: tuple[int, int], fw_version: tuple[int, int]) -> bool:
    # Firmware minor version changes across releases, so only major is enforced here.
    return hw_version == (_EXPECTED_HW_MAJOR, _EXPECTED_HW_MINOR) and fw_version[0] == _EXPECTED_FW_MAJOR


def _wait_for_xmodem_receiver(device: serial.Serial, timeout: float) -> bool:
    deadline = time.time() + timeout

    while time.time() < deadline:
        response = device.read(1)

        if not response:
            continue

        symbol = response[0]

        if symbol in (_C, _NAK):
            return symbol == _C

        if symbol == _CAN:
            raise RuntimeError('Bootloader canceled transfer')

    raise TimeoutError('No XMODEM receiver start signal')


def _wait_for_response(device: serial.Serial, timeout: float) -> int:
    deadline = time.time() + timeout

    while time.time() < deadline:
        response = device.read(1)

        if not response:
            continue

        symbol = response[0]

        if symbol in (_ACK, _NAK, _CAN):
            return symbol

    raise TimeoutError('Timeout waiting for receiver response')


def _send_xmodem_block(device: serial.Serial, block_number: int, payload: bytes, use_crc: bool):
    if len(payload) not in (128, 1024):
        raise ValueError('XMODEM block must be 128 or 1024 bytes')

    header = _STX if len(payload) == 1024 else _SOH
    packet = bytearray((header, block_number & 0xFF, 0xFF - (block_number & 0xFF)))
    packet += payload

    if use_crc:
        packet += struct.pack('>H', _crc16_ccitt(payload))
    else:
        packet.append(sum(payload) & 0xFF)

    device.write(packet)


def _send_xmodem(
    device: serial.Serial,
    firmware_data: bytes,
    max_retries: int,
    receiver_timeout: float,
    response_timeout: float,
    progress: bool,
):
    use_crc = _wait_for_xmodem_receiver(device, receiver_timeout)

    block_size = 1024
    block_number = 1
    sent_bytes = 0
    total_bytes = len(firmware_data)

    for offset in range(0, len(firmware_data), block_size):
        payload = firmware_data[offset:offset + block_size]

        if len(payload) < block_size:
            payload = payload + bytes((_SUB,)) * (block_size - len(payload))

        for attempt in range(1, max_retries + 1):
            _send_xmodem_block(device, block_number, payload, use_crc)
            response = _wait_for_response(device, response_timeout)

            if response == _ACK:
                sent_bytes = min(offset + block_size, total_bytes)

                if progress:
                    percent = (sent_bytes * 100.0) / total_bytes if total_bytes else 100.0
                    print(f'Progress: {sent_bytes}/{total_bytes} bytes ({percent:.1f}%)')

                # XMODEM block IDs are 1..255; many bootloaders treat block 0 as invalid data packet ID.
                block_number = (block_number + 1) & 0xFF
                if block_number == 0:
                    block_number = 1
                break

            if response == _CAN:
                raise RuntimeError('Bootloader canceled transfer')

            if response != _NAK:
                raise RuntimeError(f'Unexpected bootloader response: 0x{response:02X}')

            if attempt == max_retries:
                raise RuntimeError(f'Unable to send block {block_number}, retries exhausted')

    for attempt in range(1, max_retries + 1):
        device.write(bytes((_EOT,)))
        response = _wait_for_response(device, response_timeout)

        if response == _ACK:
            return

        if response == _CAN:
            raise RuntimeError('Bootloader canceled transfer')

        if response != _NAK:
            raise RuntimeError(f'Unexpected EOT response: 0x{response:02X}')

        if attempt == max_retries:
            raise RuntimeError('Unable to finish transfer, EOT not acknowledged')


def _format_version(version: tuple[int, int]) -> str:
    return f'{version[0]}.{version[1]}'


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description='Update LiteVNA-64 firmware over serial using pyserial and XMODEM')

    parser.add_argument('firmware', type=pathlib.Path, help='path to LiteVNA firmware binary')
    parser.add_argument('--port', help='serial port (auto-detected by LiteVNA-64 protocol probe when omitted)')
    parser.add_argument('--baudrate', type=int, default=115200, help='serial baudrate, defaults to 115200')
    parser.add_argument('--timeout', type=float, default=1.0, help='serial timeout in seconds, defaults to 1.0')
    parser.add_argument('--receiver-timeout', type=float, default=30.0, help='timeout waiting for XMODEM receiver start, defaults to 30.0')
    parser.add_argument('--response-timeout', type=float, default=5.0, help='timeout waiting for ACK/NAK responses, defaults to 5.0')
    parser.add_argument('--max-retries', type=int, default=10, help='maximum retries for each XMODEM block, defaults to 10')
    parser.add_argument('--force', action='store_true', help='skip LiteVNA-64 version sanity checks before transfer')
    parser.add_argument('--quiet', action='store_true', help='disable progress output')

    return parser.parse_args()


def main() -> int:
    args = _parse_args()

    firmware_path = args.firmware

    if not firmware_path.is_file():
        raise FileNotFoundError(f'Firmware file not found: {firmware_path}')

    firmware_data = firmware_path.read_bytes()

    if not firmware_data:
        raise RuntimeError('Firmware file is empty')

    device_name = args.port if args.port else _find_port(args.baudrate, args.timeout)

    try:
        with _open_device(device_name, args.baudrate, args.timeout) as device:
            hw_version, fw_version = _probe_litevna(device)

            if not args.force and not _is_litevna64(hw_version, fw_version):
                raise RuntimeError(
                    f'Unexpected LiteVNA signature, hw={_format_version(hw_version)} fw={_format_version(fw_version)}'
                )

            if not args.quiet:
                print(f'Detected LiteVNA-64 on {device_name}: hw={_format_version(hw_version)} fw={_format_version(fw_version)}')
    except (OSError, RuntimeError, TimeoutError, serial.SerialException):
        if not args.force:
            raise

        if not args.quiet:
            print(f'Continuing in forced mode without LiteVNA-64 probe validation on {device_name}')

    if not args.quiet:
        print(f'Opening {device_name} for firmware upload...')

    with _open_device(device_name, args.baudrate, args.timeout) as device:
        _send_xmodem(
            device=device,
            firmware_data=firmware_data,
            max_retries=args.max_retries,
            receiver_timeout=args.receiver_timeout,
            response_timeout=args.response_timeout,
            progress=not args.quiet,
        )

    if not args.quiet:
        print('Firmware update completed successfully')

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
