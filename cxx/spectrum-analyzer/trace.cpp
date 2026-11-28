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

#include "trace.h"

#include <algorithm>

const char* trace_mode_name(TraceMode mode)
{
    switch (mode)
    {
    case TraceMode::Off:      return "Off";
    case TraceMode::Live:     return "Live";
    case TraceMode::MaxHold:  return "Max hold";
    case TraceMode::MinHold:  return "Min hold";
    case TraceMode::MaxDecay: return "Max decay";
    case TraceMode::Average:  return "Average";
    }

    return "";
}

void Trace::update(const std::vector<float>& sweep, double dt)
{
    if (mode == TraceMode::Off)
        return;

    if (data.size() != sweep.size())
    {
        data = sweep;
        return;
    }

    const size_t count = sweep.size();

    switch (mode)
    {
    case TraceMode::Off:
        break;

    case TraceMode::Live:
        data = sweep;
        break;

    case TraceMode::MaxHold:
        for (size_t i = 0; i < count; ++i)
            data[i] = std::max(data[i], sweep[i]);
        break;

    case TraceMode::MinHold:
        for (size_t i = 0; i < count; ++i)
            data[i] = std::min(data[i], sweep[i]);
        break;

    case TraceMode::MaxDecay:
    {
        const float fall = static_cast<float>(dt) * 10.0f / std::max(falloff_time_s, 0.01f);
        for (size_t i = 0; i < count; ++i)
            data[i] = std::max(sweep[i], data[i] - fall);
        break;
    }

    case TraceMode::Average:
    {
        const float alpha = 1.0f / static_cast<float>(std::max(average_count, 1));
        for (size_t i = 0; i < count; ++i)
            data[i] += (sweep[i] - data[i]) * alpha;
        break;
    }
    }
}
