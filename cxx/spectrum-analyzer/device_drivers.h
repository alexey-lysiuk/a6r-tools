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

#include "device.h"

// Each driver exposes an enumerator and a factory; register new ones in device.cpp

#ifdef SA_HAVE_HACKRF
void enumerate_hackrf(std::vector<DeviceEntry>& out);
std::unique_ptr<SpectrumDevice> create_hackrf(const DeviceEntry& entry);
#endif

#ifdef SA_HAVE_PLUTO
void enumerate_pluto(std::vector<DeviceEntry>& out);
std::unique_ptr<SpectrumDevice> create_pluto(const DeviceEntry& entry);
#endif

#ifdef SA_HAVE_RTLSDR
void enumerate_rtlsdr(std::vector<DeviceEntry>& out);
std::unique_ptr<SpectrumDevice> create_rtlsdr(const DeviceEntry& entry);
#endif

void enumerate_tinysa(std::vector<DeviceEntry>& out);
std::unique_ptr<SpectrumDevice> create_tinysa(const DeviceEntry& entry);
