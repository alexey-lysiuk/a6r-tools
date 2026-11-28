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

#pragma once

#include <cstddef>
#include <string>
#include <vector>

class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort() { close(); }

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const;

    // Returns number of bytes read, 0 on timeout, -1 on error
    int  read(void* buffer, size_t size, int timeout_ms);
    bool write(const void* buffer, size_t size);
    bool write(const std::string& text) { return write(text.data(), text.size()); }
    void flush_input();

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int   handle_ = -1;
#endif
};

// Candidate device paths for USB CDC serial devices
std::vector<std::string> list_serial_ports();
