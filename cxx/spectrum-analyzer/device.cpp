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

#include "device.h"
#include "device_drivers.h"

#include <algorithm>
#include <cmath>

void SpectrumDevice::set_setting(const std::string& id, double value)
{
    DeviceSetting changed;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = std::find_if(settings_.begin(), settings_.end(),
            [&id](const DeviceSetting& s) { return s.id == id; });
        if (it == settings_.end())
            return;

        if (it->type == DeviceSetting::Int && it->step > 0.0)
            value = std::round(value / it->step) * it->step;
        if (it->type == DeviceSetting::Bool)
            value = value != 0.0 ? 1.0 : 0.0;

        it->value = std::min(std::max(value, it->min), it->max);
        changed = *it;
    }

    on_setting_changed(changed);
}

double SpectrumDevice::setting_value(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const DeviceSetting& s : settings_)
    {
        if (s.id == id)
            return s.value;
    }

    return 0.0;
}

bool SpectrumDevice::pop_sweep(Sweep& sweep)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (sweeps_.empty())
        return false;

    sweep = std::move(sweeps_.front());
    sweeps_.pop_front();
    return true;
}

void SpectrumDevice::push_sweep(Sweep&& sweep)
{
    std::lock_guard<std::mutex> lock(mutex_);

    constexpr size_t max_queued = 64;
    if (sweeps_.size() >= max_queued)
        sweeps_.pop_front();

    sweeps_.push_back(std::move(sweep));
}

void SpectrumDevice::clear_sweeps()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sweeps_.clear();
}

void SpectrumDevice::set_error(const std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = error;
}

std::string SpectrumDevice::take_error()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string result;
    result.swap(error_);
    return result;
}

std::vector<DeviceEntry> enumerate_devices()
{
    std::vector<DeviceEntry> result;

#ifdef SA_HAVE_HACKRF
    enumerate_hackrf(result);
#endif
#ifdef SA_HAVE_PLUTO
    enumerate_pluto(result);
#endif
    enumerate_tinysa(result);

    return result;
}

std::unique_ptr<SpectrumDevice> create_device(const DeviceEntry& entry)
{
#ifdef SA_HAVE_HACKRF
    if (entry.driver == "hackrf")
        return create_hackrf(entry);
#endif
#ifdef SA_HAVE_PLUTO
    if (entry.driver == "pluto")
        return create_pluto(entry);
#endif
    if (entry.driver == "tinysa")
        return create_tinysa(entry);

    return nullptr;
}
