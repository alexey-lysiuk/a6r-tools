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

// tinySA (Ultra) driver. Talks to the device's shell over USB CDC serial
// and uses the "scanraw" command which returns '{' ('x' lo hi)* '}',
// where every sample is a little-endian uint16 equal to (dBm + zero) * 32.

#include "device_drivers.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "serial.h"

namespace
{

using Clock = std::chrono::steady_clock;

int ms_since(Clock::time_point t)
{
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count());
}

const char* const RBW_CHOICES[]  = { "auto", "0.2", "1", "3", "10", "30", "100", "300", "600", "850" };
const char* const SPUR_CHOICES[] = { "off", "on", "auto" };

class TinySaDevice final : public SpectrumDevice
{
public:
    explicit TinySaDevice(const std::string& port)
    : port_(port)
    {
        info_.model               = "tinySA Ultra";
        info_.level_unit          = "dBm";
        info_.min_hz              = 100e3;
        info_.max_hz              = 5.3e9;
        info_.min_points          = 10;
        info_.max_points          = 4096;
        info_.default_points      = 450;
        info_.points_configurable = true;

        DeviceSetting rbw;
        rbw.id      = "rbw";
        rbw.label   = "Resolution bandwidth";
        rbw.unit    = "kHz";
        rbw.tooltip = "Narrower RBW lowers the noise floor but slows the sweep";
        rbw.type    = DeviceSetting::Choice;
        for (const char* c : RBW_CHOICES)
            rbw.choices.push_back(c);
        rbw.max = static_cast<double>(rbw.choices.size() - 1);
        settings_.push_back(rbw);

        DeviceSetting lna;
        lna.id      = "lna";
        lna.label   = "Low noise amplifier";
        lna.tooltip = "Internal LNA, improves sensitivity for weak signals";
        lna.type    = DeviceSetting::Bool;
        settings_.push_back(lna);

        DeviceSetting autoatt;
        autoatt.id      = "atten_auto";
        autoatt.label   = "Auto attenuation";
        autoatt.type    = DeviceSetting::Bool;
        autoatt.value   = 1;
        settings_.push_back(autoatt);

        DeviceSetting att;
        att.id      = "atten";
        att.label   = "Input attenuation";
        att.unit    = "dB";
        att.type    = DeviceSetting::Int;
        att.value   = 0;
        att.min     = 0;
        att.max     = 31;
        att.tooltip = "Used when auto attenuation is off";
        settings_.push_back(att);

        DeviceSetting repeat;
        repeat.id      = "repeat";
        repeat.label   = "Repeat count";
        repeat.type    = DeviceSetting::Int;
        repeat.value   = 1;
        repeat.min     = 1;
        repeat.max     = 1000;
        repeat.tooltip = "Measurements averaged per point; higher values reduce noise but slow the sweep";
        settings_.push_back(repeat);

        DeviceSetting spur;
        spur.id      = "spur";
        spur.label   = "Spur removal";
        spur.tooltip = "Suppress internal spurs at the cost of sweep time";
        spur.type    = DeviceSetting::Choice;
        for (const char* c : SPUR_CHOICES)
            spur.choices.push_back(c);
        spur.value = 2;
        spur.max   = static_cast<double>(spur.choices.size() - 1);
        settings_.push_back(spur);
    }

    ~TinySaDevice() override { close(); }

    const DeviceInfo& info() const override { return info_; }

    bool open(std::string& error) override
    {
        if (serial_.is_open())
            return true;

        if (!serial_.open(port_, error))
            return false;

        resync();

        std::string reply;
        serial_.write("version\r");
        read_until("ch>", &reply, 1500);

        if (reply.find("tinySA") == std::string::npos)
        {
            serial_.close();
            error = port_ + " does not look like a tinySA";
            return false;
        }

        // tinySA4 firmware identifies itself as "tinySA4_vX.Y-..."
        const bool ultra = reply.find("tinySA4") != std::string::npos;
        info_.model = ultra ? "tinySA Ultra" : "tinySA";
        if (!ultra)
            info_.max_hz = 350e6;

        reply.clear();
        serial_.write("zero ?\r");
        read_until("ch>", &reply, 1000);
        const size_t pos = reply.find("dBm");
        zero_level_ = ultra ? 174 : 128;
        if (pos != std::string::npos)
        {
            const size_t line = reply.rfind('\n', pos);
            const int parsed = std::atoi(reply.c_str() + (line == std::string::npos ? 0 : line + 1));
            if (parsed > 0)
                zero_level_ = parsed;
        }

        return true;
    }

    void close() override
    {
        stop();
        serial_.close();
    }

    bool start(const SweepConfig& config, std::string& error) override
    {
        stop();

        if (!serial_.is_open())
        {
            error = "Device is not open";
            return false;
        }

        points_ = config.points;
        if (points_ < info_.min_points)
            points_ = info_.min_points;
        if (points_ > info_.max_points)
            points_ = info_.max_points;

        const double span = config.stop_hz - config.start_hz;
        // Firmware uses an integer frequency step: start + i * step
        step_hz_ = static_cast<long long>(span / points_);
        if (step_hz_ <= 0)
        {
            error = "Span is too narrow for the requested number of points";
            return false;
        }

        start_hz_ = static_cast<long long>(config.start_hz);
        stop_hz_  = static_cast<long long>(config.stop_hz);

        clear_sweeps();

        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_.clear();
            for (const DeviceSetting& s : settings_)
                queue_commands(s);
        }

        stop_requested_ = false;
        running_ = true;
        worker_ = std::thread(&TinySaDevice::run, this);
        return true;
    }

    void stop() override
    {
        stop_requested_ = true;

        if (worker_.joinable())
            worker_.join();

        running_ = false;
    }

protected:
    void on_setting_changed(const DeviceSetting& s) override
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        queue_commands(s);
    }

private:
    // Must be called with pending_mutex_ held
    void queue_commands(const DeviceSetting& s)
    {
        char buf[64];

        if (s.id == "rbw")
        {
            snprintf(buf, sizeof(buf), "rbw %s\r", RBW_CHOICES[static_cast<int>(s.value)]);
        }
        else if (s.id == "lna")
        {
            snprintf(buf, sizeof(buf), "lna %s\r", s.value != 0.0 ? "on" : "off");
        }
        else if (s.id == "repeat")
        {
            snprintf(buf, sizeof(buf), "repeat %d\r", static_cast<int>(s.value));
        }
        else if (s.id == "spur")
        {
            snprintf(buf, sizeof(buf), "spur %s\r", SPUR_CHOICES[static_cast<int>(s.value)]);
        }
        else if (s.id == "atten" || s.id == "atten_auto")
        {
            // Both settings map to the single "attenuate" command
            double automatic = s.value;
            double manual    = s.value;
            for (const DeviceSetting& o : settings_)
            {
                if (o.id == "atten_auto")
                    automatic = o.value;
                else if (o.id == "atten")
                    manual = o.value;
            }

            if (automatic != 0.0)
                snprintf(buf, sizeof(buf), "attenuate auto\r");
            else
                snprintf(buf, sizeof(buf), "attenuate %d\r", static_cast<int>(manual));
        }
        else
        {
            return;
        }

        pending_.emplace_back(buf);
    }

    void resync()
    {
        serial_.write("\r");
        read_until("ch>", nullptr, 3000);
        serial_.flush_input();
    }

    // Reads until token is seen or timeout expires. Returns true if the token was found
    bool read_until(const char* token, std::string* out, int timeout_ms)
    {
        std::string text;
        const auto begin = Clock::now();
        char buf[256];

        while (ms_since(begin) < timeout_ms)
        {
            const int count = serial_.read(buf, sizeof(buf), 50);
            if (count < 0)
                break;

            text.append(buf, static_cast<size_t>(count));
            if (text.find(token) != std::string::npos)
            {
                if (out)
                    *out = text;
                return true;
            }
        }

        if (out)
            *out = text;
        return false;
    }

    void apply_pending()
    {
        std::vector<std::string> commands;

        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            commands.swap(pending_);
        }

        for (const std::string& cmd : commands)
        {
            serial_.write(cmd);
            read_until("ch>", nullptr, 1500);
        }
    }

    bool read_exact(uint8_t* dst, size_t count, int timeout_ms)
    {
        size_t got = 0;
        const auto begin = Clock::now();
        bool stop_seen = false;
        Clock::time_point stop_time;

        while (got < count)
        {
            // After a stop request give the sweep a grace period to finish,
            // so that no stale data is left in the serial buffer
            if (stop_requested_ && !stop_seen)
            {
                stop_seen = true;
                stop_time = Clock::now();
            }
            if (stop_seen && ms_since(stop_time) > 10000)
                return false;
            if (ms_since(begin) > timeout_ms)
                return false;

            const int n = serial_.read(dst + got, count - got, 100);
            if (n < 0)
                return false;

            got += static_cast<size_t>(n);
        }

        return true;
    }

    // On failure, error is empty if the connection was lost
    bool scan_once(Sweep& sweep, std::string& error)
    {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "scanraw %lld %lld %d\r", start_hz_, stop_hz_, points_);

        serial_.flush_input();
        if (!serial_.write(cmd, strlen(cmd)))
            return false;

        // Skip command echo up to the start of the data block
        uint8_t byte = 0;
        do
        {
            if (!read_exact(&byte, 1, 5000))
                return false;
        }
        while (byte != '{');

        std::vector<uint8_t> raw(static_cast<size_t>(points_) * 3 + 1);
        if (!read_exact(raw.data(), raw.size(), 60000))
            return false;

        if (raw.back() != '}')
        {
            error = "Unexpected scan data from tinySA";
            return false;
        }

        sweep.start_hz = static_cast<double>(start_hz_);
        sweep.stop_hz  = static_cast<double>(start_hz_ + step_hz_ * (points_ - 1));
        sweep.level.resize(points_);

        for (int i = 0; i < points_; ++i)
        {
            const uint8_t* p = &raw[static_cast<size_t>(i) * 3];
            if (p[0] != 'x')
            {
                error = "Malformed scan data from tinySA";
                return false;
            }

            const unsigned value = p[1] | (static_cast<unsigned>(p[2]) << 8);
            sweep.level[i] = static_cast<float>(value) / 32.0f - static_cast<float>(zero_level_);
        }

        read_until("ch>", nullptr, 1000);
        return true;
    }

    void run()
    {
        resync();

        while (!stop_requested_)
        {
            apply_pending();

            Sweep sweep;
            std::string error;
            if (!scan_once(sweep, error))
            {
                if (!stop_requested_)
                    set_error(error.empty() ? "Lost communication with tinySA" : error);
                break;
            }

            push_sweep(std::move(sweep));
        }

        running_ = false;
    }

    DeviceInfo   info_;
    std::string  port_;
    SerialPort   serial_;
    std::thread  worker_;
    std::atomic<bool> stop_requested_{false};

    int        zero_level_ = 174;
    int        points_     = 450;
    long long  start_hz_   = 0;
    long long  stop_hz_    = 0;
    long long  step_hz_    = 0;

    std::mutex               pending_mutex_;
    std::vector<std::string> pending_;
};

} // unnamed namespace

void enumerate_tinysa(std::vector<DeviceEntry>& out)
{
    for (const std::string& port : list_serial_ports())
    {
        DeviceEntry entry;
        entry.driver = "tinysa";
        entry.id     = port;
        entry.label  = "tinySA on " + port;
        out.push_back(entry);
    }
}

std::unique_ptr<SpectrumDevice> create_tinysa(const DeviceEntry& entry)
{
    return std::unique_ptr<SpectrumDevice>(new TinySaDevice(entry.id));
}
