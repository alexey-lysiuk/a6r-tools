/*
 * Copyright (C) 2026 Alexey Lysiuk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "serial.h"

#include <cstring>

#ifdef _WIN32

#include <windows.h>

bool SerialPort::open(const std::string& path, std::string& error)
{
    close();

    const std::string full = "\\\\.\\" + path;
    HANDLE h = ::CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        error = "Cannot open " + path + " (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (::GetCommState(h, &dcb))
    {
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity   = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary  = TRUE;
        dcb.fParity  = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl  = DTR_CONTROL_ENABLE;
        dcb.fRtsControl  = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX  = FALSE;
        ::SetCommState(h, &dcb);
    }

    ::SetupComm(h, 65536, 65536);
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    handle_ = h;
    return true;
}

void SerialPort::close()
{
    if (handle_)
    {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

bool SerialPort::is_open() const
{
    return handle_ != nullptr;
}

int SerialPort::read(void* buffer, size_t size, int timeout_ms)
{
    if (!handle_)
        return -1;

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout        = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant   = timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : 1;
    timeouts.WriteTotalTimeoutConstant  = 2000;
    ::SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts);

    DWORD count = 0;
    if (!::ReadFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(size), &count, nullptr))
        return -1;

    return static_cast<int>(count);
}

bool SerialPort::write(const void* buffer, size_t size)
{
    if (!handle_)
        return false;

    DWORD count = 0;
    return ::WriteFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(size), &count, nullptr)
        && count == size;
}

void SerialPort::flush_input()
{
    if (handle_)
        ::PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR);
}

std::vector<std::string> list_serial_ports()
{
    std::vector<std::string> result;

    for (int i = 1; i <= 64; ++i)
    {
        const std::string name = "COM" + std::to_string(i);
        const std::string full = "\\\\.\\" + name;

        HANDLE h = ::CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(h);
            result.push_back(name);
        }
        else if (::GetLastError() == ERROR_ACCESS_DENIED)
        {
            result.push_back(name);   // exists but busy
        }
    }

    return result;
}

#else // !_WIN32

#include <cerrno>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

bool SerialPort::open(const std::string& path, std::string& error)
{
    close();

    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        error = "Cannot open " + path + ": " + std::strerror(errno);
        return false;
    }

    termios tio = {};
    if (::tcgetattr(fd, &tio) == 0)
    {
        ::cfmakeraw(&tio);
        ::cfsetispeed(&tio, B115200);
        ::cfsetospeed(&tio, B115200);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~CRTSCTS;
        tio.c_cc[VMIN]  = 0;
        tio.c_cc[VTIME] = 0;
        ::tcsetattr(fd, TCSANOW, &tio);
    }

    ::tcflush(fd, TCIOFLUSH);

    handle_ = fd;
    return true;
}

void SerialPort::close()
{
    if (handle_ >= 0)
    {
        ::close(handle_);
        handle_ = -1;
    }
}

bool SerialPort::is_open() const
{
    return handle_ >= 0;
}

int SerialPort::read(void* buffer, size_t size, int timeout_ms)
{
    if (handle_ < 0)
        return -1;

    pollfd pfd = {};
    pfd.fd     = handle_;
    pfd.events = POLLIN;

    const int ready = ::poll(&pfd, 1, timeout_ms);
    if (ready < 0)
        return errno == EINTR ? 0 : -1;
    if (ready == 0)
        return 0;
    if (pfd.revents & (POLLERR | POLLNVAL))
        return -1;

    const ssize_t count = ::read(handle_, buffer, size);
    if (count < 0)
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (count == 0 && (pfd.revents & POLLHUP))
        return -1;

    return static_cast<int>(count);
}

bool SerialPort::write(const void* buffer, size_t size)
{
    if (handle_ < 0)
        return false;

    const char* data = static_cast<const char*>(buffer);

    while (size > 0)
    {
        const ssize_t count = ::write(handle_, data, size);
        if (count < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                pollfd pfd = {};
                pfd.fd     = handle_;
                pfd.events = POLLOUT;
                ::poll(&pfd, 1, 1000);
                continue;
            }
            return false;
        }

        data += count;
        size -= static_cast<size_t>(count);
    }

    return true;
}

void SerialPort::flush_input()
{
    if (handle_ >= 0)
        ::tcflush(handle_, TCIFLUSH);
}

std::vector<std::string> list_serial_ports()
{
    std::vector<std::string> result;

#ifdef __APPLE__
    const char* const patterns[] = { "/dev/cu.usbmodem*" };
#else
    const char* const patterns[] = { "/dev/ttyACM*" };
#endif

    for (const char* pattern : patterns)
    {
        glob_t g = {};
        if (::glob(pattern, 0, nullptr, &g) == 0)
        {
            for (size_t i = 0; i < g.gl_pathc; ++i)
                result.emplace_back(g.gl_pathv[i]);
        }
        ::globfree(&g);
    }

    return result;
}

#endif // _WIN32
