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

// PlutoSDR / LibreSDR driver (AD936x transceiver) based on libiio and libad9361.
// The radio has no hardware sweep mode, so the host steps the RX LO across the
// requested range, grabs a block of samples at every step and averages FFTs.

#include "device_drivers.h"
#include "fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <ad9361.h>
#include <iio.h>

namespace
{

constexpr double   MIN_FREQ_HZ      = 70e6;
constexpr double   MAX_FREQ_HZ      = 6e9;
constexpr double   USABLE_BAND      = 0.7;       // fraction of the sample rate kept from every step
constexpr unsigned BLOCK_SAMPLES    = 16384;
constexpr float    FULL_SCALE_INV   = 1.0f / 2048.0f;   // 12-bit samples

const double SAMPLE_RATES_MHZ[] = { 2.5, 5.0, 10.0, 15.0, 20.0, 30.72, 40.0, 56.0 };
const char* const GAIN_MODES[]  = { "manual", "slow_attack", "fast_attack", "hybrid" };

class PlutoDevice final : public SpectrumDevice
{
public:
    explicit PlutoDevice(const std::string& uri, const std::string& model)
    : uri_(uri)
    {
        info_.model               = model;
        info_.level_unit          = "dBFS";
        info_.min_hz              = MIN_FREQ_HZ;
        info_.max_hz              = MAX_FREQ_HZ;
        info_.points_configurable = false;

        DeviceSetting rate;
        rate.id      = "sample_rate";
        rate.label   = "Sample rate";
        rate.unit    = "MSPS";
        rate.tooltip = "Wider steps sweep faster but need more USB or network throughput";
        rate.type    = DeviceSetting::Choice;
        rate.requires_restart = true;
        for (double r : SAMPLE_RATES_MHZ)
        {
            char text[32];
            snprintf(text, sizeof(text), "%g", r);
            rate.choices.push_back(text);
        }
        rate.value = 2;   // 10 MSPS
        rate.max   = static_cast<double>(rate.choices.size() - 1);
        settings_.push_back(rate);

        DeviceSetting bins;
        bins.id      = "fft_size";
        bins.label   = "FFT size";
        bins.tooltip = "Larger FFT gives finer frequency resolution";
        bins.type    = DeviceSetting::Choice;
        bins.requires_restart = true;
        for (int n = 64; n <= 4096; n *= 2)
        {
            char text[16];
            snprintf(text, sizeof(text), "%d", n);
            bins.choices.push_back(text);
        }
        bins.value = 3;   // 512
        bins.max   = static_cast<double>(bins.choices.size() - 1);
        settings_.push_back(bins);

        DeviceSetting mode;
        mode.id      = "gain_mode";
        mode.label   = "Gain control";
        mode.tooltip = "Manual uses the gain below; AGC modes ignore it";
        mode.type    = DeviceSetting::Choice;
        for (const char* m : GAIN_MODES)
            mode.choices.push_back(m);
        mode.max = static_cast<double>(mode.choices.size() - 1);
        settings_.push_back(mode);

        DeviceSetting gain;
        gain.id      = "gain";
        gain.label   = "RX gain";
        gain.unit    = "dB";
        gain.tooltip = "Manual gain, 0-73 dB depending on frequency";
        gain.type    = DeviceSetting::Int;
        gain.value   = 40;
        gain.min     = 0;
        gain.max     = 73;
        settings_.push_back(gain);

        DeviceSetting offset;
        offset.id      = "offset";
        offset.label   = "Level offset";
        offset.unit    = "dB";
        offset.tooltip = "Added to the measured dBFS level, e.g. to calibrate against a known source";
        offset.type    = DeviceSetting::Float;
        offset.min     = -150;
        offset.max     = 150;
        settings_.push_back(offset);
    }

    ~PlutoDevice() override { close(); }

    const DeviceInfo& info() const override { return info_; }

    bool open(std::string& error) override
    {
        if (ctx_)
            return true;

        ctx_ = iio_create_context_from_uri(uri_.c_str());
        if (!ctx_)
        {
            error = "Cannot open IIO context " + uri_;
            return false;
        }

        iio_context_set_timeout(ctx_, 10000);

        phy_ = iio_context_find_device(ctx_, "ad9361-phy");
        rx_  = iio_context_find_device(ctx_, "cf-ad9361-lpc");
        if (phy_)
        {
            gain_chan_ = iio_device_find_channel(phy_, "voltage0", false);
            lo_chan_   = iio_device_find_channel(phy_, "altvoltage0", true);
        }
        if (rx_)
        {
            i_chan_ = iio_device_find_channel(rx_, "voltage0", false);
            q_chan_ = iio_device_find_channel(rx_, "voltage1", false);
        }

        if (!gain_chan_ || !lo_chan_ || !i_chan_ || !q_chan_)
        {
            error = "AD9361 channels not found, not a PlutoSDR/LibreSDR?";
            release();
            return false;
        }

        iio_channel_enable(i_chan_);
        iio_channel_enable(q_chan_);
        return true;
    }

    void close() override
    {
        stop();
        release();
    }

    bool start(const SweepConfig& config, std::string& error) override
    {
        stop();

        if (!ctx_)
        {
            error = "Device is not open";
            return false;
        }

        const double fs = SAMPLE_RATES_MHZ[static_cast<int>(setting_value("sample_rate"))] * 1e6;
        const int    n  = 64 << static_cast<int>(setting_value("fft_size"));

        if (ad9361_set_bb_rate(phy_, static_cast<unsigned long>(fs)) != 0)
        {
            error = "Cannot set sample rate";
            return false;
        }

        iio_device_set_kernel_buffers_count(rx_, 2);   // so that one discarded block flushes stale data

        const unsigned samples = std::max<unsigned>(BLOCK_SAMPLES / n, 1) * n;
        buffer_ = iio_device_create_buffer(rx_, samples, false);
        if (!buffer_)
        {
            error = "Cannot create RX buffer";
            return false;
        }

        fft_.setup(n);
        samples_    = samples;
        bin_hz_     = fs / n;
        step_bins_  = static_cast<int>(std::floor(USABLE_BAND * n / 2.0)) * 2;
        grid_start_ = config.start_hz;

        const double span = std::max(config.stop_hz - config.start_hz, bin_hz_);
        steps_ = std::max(1, static_cast<int>(std::ceil(span / (step_bins_ * bin_hz_))));

        const size_t total = static_cast<size_t>(steps_) * step_bins_;
        last_bin_ = std::min(total - 1, std::max<size_t>(1, static_cast<size_t>(std::floor(span / bin_hz_))));

        clear_sweeps();

        stop_requested_ = false;
        running_        = true;
        worker_ = std::thread(&PlutoDevice::run, this);
        return true;
    }

    void stop() override
    {
        if (worker_.joinable())
        {
            stop_requested_ = true;
            if (buffer_)
                iio_buffer_cancel(buffer_);
            worker_.join();
        }

        if (buffer_)
        {
            iio_buffer_destroy(buffer_);
            buffer_ = nullptr;
        }

        running_ = false;
    }

private:
    void release()
    {
        if (ctx_)
        {
            iio_context_destroy(ctx_);
            ctx_ = nullptr;
        }

        phy_ = rx_ = nullptr;
        gain_chan_ = lo_chan_ = i_chan_ = q_chan_ = nullptr;
    }

    void apply_gain()
    {
        const int mode = static_cast<int>(setting_value("gain_mode"));
        const int gain = static_cast<int>(setting_value("gain"));

        if (mode == applied_mode_ && (mode != 0 || gain == applied_gain_))
            return;

        int result = iio_channel_attr_write(gain_chan_, "gain_control_mode", GAIN_MODES[mode]);
        if (result >= 0 && mode == 0)
            result = iio_channel_attr_write_longlong(gain_chan_, "hardwaregain", gain);
        if (result < 0)
            set_error("Cannot apply gain settings");

        applied_mode_ = mode;
        applied_gain_ = gain;
    }

    bool refill()
    {
        return iio_buffer_refill(buffer_) >= 0;
    }

    // Returns false if the worker should stop
    bool capture_step(int step, std::vector<float>& level)
    {
        const double center = grid_start_ + (static_cast<double>(step) * step_bins_ + step_bins_ / 2) * bin_hz_;
        const int result = iio_channel_attr_write_longlong(lo_chan_, "frequency", std::llround(center));
        if (result < 0)
        {
            set_error("Cannot tune to " + std::to_string(center / 1e6) + " MHz (out of device range?)");
            return false;
        }

        if (!refill() || !refill())
            return false;

        const size_t bytes = samples_ * sizeof(int16_t);
        iq_.resize(samples_ * 2);
        i_.resize(samples_);
        q_.resize(samples_);
        iio_channel_read(i_chan_, buffer_, i_.data(), bytes);
        iio_channel_read(q_chan_, buffer_, q_.data(), bytes);

        // Remove the LO leakage at DC
        double mean_i = 0.0, mean_q = 0.0;
        for (unsigned k = 0; k < samples_; ++k)
        {
            mean_i += i_[k];
            mean_q += q_[k];
        }
        mean_i /= samples_;
        mean_q /= samples_;

        for (unsigned k = 0; k < samples_; ++k)
        {
            iq_[k * 2]     = static_cast<int16_t>(std::lround(i_[k] - mean_i));
            iq_[k * 2 + 1] = static_cast<int16_t>(std::lround(q_[k] - mean_q));
        }

        const int n = fft_.size();
        const int segments = static_cast<int>(samples_) / n;
        avg_.assign(n, 0.0f);
        for (int s = 0; s < segments; ++s)
        {
            const std::vector<float>& pwr = fft_.transform(&iq_[static_cast<size_t>(s) * n * 2], FULL_SCALE_INV);
            for (int k = 0; k < n; ++k)
                avg_[k] += std::pow(10.0f, pwr[k] / 10.0f);
        }

        const float offset = static_cast<float>(setting_value("offset"));
        for (int m = 0; m < step_bins_; ++m)
        {
            const int bin = (m - step_bins_ / 2 + n) % n;   // unshifted FFT index
            level[static_cast<size_t>(step) * step_bins_ + m] = 10.0f * std::log10(avg_[bin] / segments + 1e-20f) + offset;
        }

        return true;
    }

    void run()
    {
        applied_mode_ = -1;
        std::vector<float> level(static_cast<size_t>(steps_) * step_bins_);

        while (!stop_requested_)
        {
            apply_gain();

            bool ok = true;
            for (int step = 0; step < steps_ && !stop_requested_; ++step)
            {
                ok = capture_step(step, level);
                if (!ok)
                    break;
            }

            if (stop_requested_)
                break;

            if (!ok)
            {
                set_error("Capture failed");
                break;
            }

            Sweep sweep;
            sweep.start_hz = grid_start_;
            sweep.stop_hz  = grid_start_ + last_bin_ * bin_hz_;
            sweep.level.assign(level.begin(), level.begin() + last_bin_ + 1);
            push_sweep(std::move(sweep));
        }

        running_ = false;
    }

    DeviceInfo  info_;
    std::string uri_;

    iio_context* ctx_        = nullptr;
    iio_device*  phy_        = nullptr;
    iio_device*  rx_         = nullptr;
    iio_channel* gain_chan_  = nullptr;
    iio_channel* lo_chan_    = nullptr;
    iio_channel* i_chan_     = nullptr;
    iio_channel* q_chan_     = nullptr;
    iio_buffer*  buffer_     = nullptr;

    std::thread       worker_;
    std::atomic<bool> stop_requested_{false};

    // Configured in start(), then used by the worker only
    sa::Fft            fft_;
    unsigned           samples_    = 0;
    double             bin_hz_     = 1.0;
    double             grid_start_ = 0.0;
    int                step_bins_  = 0;
    int                steps_      = 0;
    size_t             last_bin_   = 0;
    int                applied_mode_ = -1;
    int                applied_gain_ = -1;
    std::vector<int16_t> i_, q_, iq_;
    std::vector<float>   avg_;
};

std::string device_model(iio_context* ctx)
{
    const char* hw = iio_context_get_attr_value(ctx, "hw_model");
    return hw ? hw : "PlutoSDR";
}

} // unnamed namespace

void enumerate_pluto(std::vector<DeviceEntry>& out)
{
    iio_scan_context* scan = iio_create_scan_context(nullptr, 0);
    if (!scan)
        return;

    iio_context_info** infos = nullptr;
    const ssize_t count = iio_scan_context_get_info_list(scan, &infos);

    for (ssize_t i = 0; i < count; ++i)
    {
        const char* uri = iio_context_info_get_uri(infos[i]);
        if (!uri)
            continue;

        std::string label = iio_context_info_get_description(infos[i]);

        // Skip other IIO devices; if the context is busy keep the entry as is
        if (iio_context* ctx = iio_create_context_from_uri(uri))
        {
            const bool is_ad9361 = iio_context_find_device(ctx, "ad9361-phy") != nullptr;
            if (is_ad9361)
                label = device_model(ctx);
            iio_context_destroy(ctx);

            if (!is_ad9361)
                continue;
        }

        DeviceEntry entry;
        entry.driver = "pluto";
        entry.id     = uri;
        entry.label  = label + " [" + uri + "]";
        out.push_back(entry);
    }

    if (infos)
        iio_context_info_list_free(infos);
    iio_scan_context_destroy(scan);
}

std::unique_ptr<SpectrumDevice> create_pluto(const DeviceEntry& entry)
{
    return std::unique_ptr<SpectrumDevice>(new PlutoDevice(entry.id, "PlutoSDR/LibreSDR"));
}
