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

#include <vector>

enum class TraceMode
{
    Off,
    Live,
    MaxHold,
    MinHold,
    MaxDecay,   // max hold whose peaks fall off over time
    Average,
};

constexpr int TRACE_MODE_COUNT = 6;
const char*   trace_mode_name(TraceMode mode);

struct Trace
{
    TraceMode mode           = TraceMode::Off;
    float     falloff_time_s = 5.0f;   // time for a held peak to fall by 10 dB (MaxDecay)
    int       average_count  = 10;     // exponential averaging length (Average)

    std::vector<float> data;

    void reset() { data.clear(); }

    // dt is the time in seconds since the previous sweep
    void update(const std::vector<float>& sweep, double dt);
};
