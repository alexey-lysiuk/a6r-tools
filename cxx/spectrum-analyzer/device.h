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

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Device-agnostic API shared by every supported RF device.
// The UI only talks to SpectrumDevice; drivers implement it and publish
// finished sweeps through push_sweep().

struct SweepConfig
{
    double start_hz = 88e6;
    double stop_hz  = 108e6;
    int    points   = 450;   // honored only if DeviceInfo::points_configurable
};

struct Sweep
{
    double             start_hz = 0.0;   // frequency of level.front()
    double             stop_hz  = 0.0;   // frequency of level.back()
    std::vector<float> level;            // uniformly spaced, in DeviceInfo::level_unit
};

struct DeviceInfo
{
    std::string model;
    std::string level_unit = "dBm";
    double      min_hz     = 0.0;
    double      max_hz     = 0.0;
    int         min_points = 2;
    int         max_points = 1024;
    int         default_points = 450;
    bool        points_configurable = true;
};

// Device specific option exposed to the user
struct DeviceSetting
{
    enum Type { Bool, Int, Float, Choice };

    std::string id;
    std::string label;
    std::string unit;
    std::string tooltip;
    Type        type  = Bool;
    double      value = 0.0;   // Bool: 0 or 1, Choice: index into choices
    double      min   = 0.0;
    double      max   = 1.0;
    double      step  = 1.0;   // Int: values are snapped to multiples of step
    std::vector<std::string> choices;
    bool        requires_restart = false;   // sweep must be restarted to apply
};

struct DeviceEntry
{
    std::string driver;
    std::string id;
    std::string label;
};

class SpectrumDevice
{
public:
    virtual ~SpectrumDevice() = default;

    virtual const DeviceInfo& info() const = 0;

    virtual bool open(std::string& error) = 0;
    virtual void close() = 0;

    // start() must discard any previously queued sweeps
    virtual bool start(const SweepConfig& config, std::string& error) = 0;
    virtual void stop() = 0;
    virtual bool running() const { return running_; }

    // Settings are modified from the UI thread only
    const std::vector<DeviceSetting>& settings() const { return settings_; }
    void set_setting(const std::string& id, double value);

    // Non-blocking, returns false if no complete sweep is available
    bool pop_sweep(Sweep& sweep);

    // Returns and clears an asynchronous error reported by the worker
    std::string take_error();

protected:
    virtual void on_setting_changed(const DeviceSetting& setting) { (void)setting; }

    void   push_sweep(Sweep&& sweep);
    void   clear_sweeps();
    void   set_error(const std::string& error);
    double setting_value(const std::string& id) const;

    std::vector<DeviceSetting> settings_;
    std::atomic<bool>          running_{false};

private:
    mutable std::mutex  mutex_;
    std::deque<Sweep>   sweeps_;
    std::string         error_;
};

std::vector<DeviceEntry>        enumerate_devices();
std::unique_ptr<SpectrumDevice> create_device(const DeviceEntry& entry);
