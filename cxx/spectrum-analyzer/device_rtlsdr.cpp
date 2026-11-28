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

// RTL-SDR driver based on librtlsdr. The dongle has no hardware sweep mode, so
// the host steps the tuner across the requested range, grabs a block of samples
// at every step and averages FFTs.

#include "device_drivers.h"
#include "fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <rtl-sdr.h>

namespace
{

constexpr double   MIN_FREQ_HZ    = 24e6;
constexpr double   MAX_FREQ_HZ    = 1766e6;   // R820T/R820T2 tuner
constexpr double   USABLE_BAND    = 0.75;     // fraction of the sample rate kept from every step
constexpr unsigned BLOCK_SAMPLES  = 16384;
constexpr float    FULL_SCALE_INV = 1.0f / 128.0f;

const double SAMPLE_RATES_MHZ[] = { 0.25, 1.024, 1.4, 1.8, 2.048, 2.4, 2.56, 3.2 };

class RtlSdrDevice final : public SpectrumDevice
{
public:
    explicit RtlSdrDevice(uint32_t index, const std::string& model)
    : index_(index)
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
        rate.tooltip = "Wider steps sweep faster; rates above 2.4 MSPS may drop samples on some hosts";
        rate.type    = DeviceSetting::Choice;
        rate.requires_restart = true;
        for (double r : SAMPLE_RATES_MHZ)
        {
            char text[32];
            snprintf(text, sizeof(text), "%g", r);
            rate.choices.push_back(text);
        }
        rate.value = 5;   // 2.4 MSPS
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

        DeviceSetting agc;
        agc.id      = "gain_auto";
        agc.label   = "Automatic gain";
        agc.tooltip = "Tuner AGC; the manual gain below is ignored";
        agc.type    = DeviceSetting::Bool;
        settings_.push_back(agc);

        DeviceSetting gain;
        gain.id      = "gain";
        gain.label   = "Tuner gain";
        gain.unit    = "dB";
        gain.tooltip = "Manual gain; snapped to the nearest value supported by the tuner";
        gain.type    = DeviceSetting::Float;
        gain.value   = 30;
        gain.min     = 0;
        gain.max     = 50;
        settings_.push_back(gain);

        DeviceSetting bias;
        bias.id      = "bias";
        bias.label   = "Antenna port power (bias tee)";
        bias.tooltip = "Supplies 4.5 V on the antenna port. Do not enable unless an active antenna needs it";
        bias.type    = DeviceSetting::Bool;
        settings_.push_back(bias);

        DeviceSetting ppm;
        ppm.id      = "ppm";
        ppm.label   = "Frequency correction";
        ppm.unit    = "ppm";
        ppm.tooltip = "Crystal error correction";
        ppm.type    = DeviceSetting::Int;
        ppm.min     = -200;
        ppm.max     = 200;
        settings_.push_back(ppm);

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

    ~RtlSdrDevice() override { close(); }

    const DeviceInfo& info() const override { return info_; }

    bool open(std::string& error) override
    {
        if (dev_)
            return true;

        if (rtlsdr_open(&dev_, index_) != 0)
        {
            dev_ = nullptr;
            error = "Cannot open RTL-SDR (is it in use or are permissions missing?)";
            return false;
        }

        const int count = rtlsdr_get_tuner_gains(dev_, nullptr);
        if (count > 0)
        {
            gains_.resize(count);
            rtlsdr_get_tuner_gains(dev_, gains_.data());
        }

        return true;
    }

    void close() override
    {
        stop();

        if (dev_)
        {
            rtlsdr_close(dev_);
            dev_ = nullptr;
        }
    }

    bool start(const SweepConfig& config, std::string& error) override
    {
        stop();

        if (!dev_)
        {
            error = "Device is not open";
            return false;
        }

        const double fs = SAMPLE_RATES_MHZ[static_cast<int>(setting_value("sample_rate"))] * 1e6;
        const int    n  = 64 << static_cast<int>(setting_value("fft_size"));

        if (rtlsdr_set_sample_rate(dev_, static_cast<uint32_t>(fs)) != 0)
        {
            error = "Cannot set sample rate";
            return false;
        }

        rtlsdr_set_tuner_bandwidth(dev_, 0);   // automatic
        rtlsdr_set_offset_tuning(dev_, 0);

        fft_.setup(n);
        samples_    = std::max<unsigned>(BLOCK_SAMPLES / n, 1) * n;
        bin_hz_     = fs / n;
        step_bins_  = static_cast<int>(std::floor(USABLE_BAND * n / 2.0)) * 2;
        grid_start_ = config.start_hz;

        const double span = std::max(config.stop_hz - config.start_hz, bin_hz_);
        steps_ = std::max(1, static_cast<int>(std::ceil(span / (step_bins_ * bin_hz_))));

        const size_t total = static_cast<size_t>(steps_) * step_bins_;
        last_bin_ = std::min(total - 1, std::max<size_t>(1, static_cast<size_t>(std::floor(span / bin_hz_))));

        clear_sweeps();
        rtlsdr_reset_buffer(dev_);

        stop_requested_ = false;
        running_        = true;
        worker_ = std::thread(&RtlSdrDevice::run, this);
        return true;
    }

    void stop() override
    {
        if (worker_.joinable())
        {
            stop_requested_ = true;
            worker_.join();
        }

        running_ = false;
    }

private:
    int nearest_gain(double db) const
    {
        int best = static_cast<int>(db * 10.0);
        int diff = 1 << 30;
        for (int g : gains_)
        {
            const int d = std::abs(g - static_cast<int>(db * 10.0));
            if (d < diff)
            {
                diff = d;
                best = g;
            }
        }
        return best;
    }

    // Applied by the worker so that librtlsdr is only used from one thread at a time
    void apply_settings()
    {
        const bool   agc  = setting_value("gain_auto") != 0.0;
        const double gain = setting_value("gain");
        const bool   bias = setting_value("bias") != 0.0;
        const int    ppm  = static_cast<int>(setting_value("ppm"));

        if (agc != applied_agc_ || (!agc && gain != applied_gain_))
        {
            int result = rtlsdr_set_tuner_gain_mode(dev_, agc ? 0 : 1);
            if (result == 0 && !agc)
                result = rtlsdr_set_tuner_gain(dev_, nearest_gain(gain));
            if (result != 0)
                set_error("Cannot apply gain settings");
            applied_agc_  = agc;
            applied_gain_ = gain;
        }

        if (bias != applied_bias_)
        {
            if (rtlsdr_set_bias_tee(dev_, bias ? 1 : 0) != 0)
                set_error("Cannot change bias tee state");
            applied_bias_ = bias;
        }

        if (ppm != applied_ppm_)
        {
            // Returns an error if the value is unchanged (-2), which is fine
            rtlsdr_set_freq_correction(dev_, ppm);
            applied_ppm_ = ppm;
        }
    }

    bool read_block()
    {
        raw_.resize(static_cast<size_t>(samples_) * 2);
        int got = 0;
        const int result = rtlsdr_read_sync(dev_, raw_.data(), static_cast<int>(raw_.size()), &got);
        return result == 0 && got == static_cast<int>(raw_.size());
    }

    bool capture_step(int step, std::vector<float>& level)
    {
        const double center = grid_start_ + (static_cast<double>(step) * step_bins_ + step_bins_ / 2) * bin_hz_;
        if (rtlsdr_set_center_freq(dev_, static_cast<uint32_t>(std::llround(center))) != 0)
        {
            set_error("Cannot tune to " + std::to_string(center / 1e6) + " MHz");
            return false;
        }

        // The first block after retuning may hold stale samples
        if (!read_block() || !read_block())
            return false;

        double mean_i = 0.0, mean_q = 0.0;
        for (unsigned k = 0; k < samples_; ++k)
        {
            mean_i += raw_[k * 2];
            mean_q += raw_[k * 2 + 1];
        }
        mean_i /= samples_;
        mean_q /= samples_;

        iq_.resize(raw_.size());
        for (unsigned k = 0; k < samples_; ++k)
        {
            iq_[k * 2]     = static_cast<int16_t>(std::lround(raw_[k * 2] - mean_i));
            iq_[k * 2 + 1] = static_cast<int16_t>(std::lround(raw_[k * 2 + 1] - mean_q));
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
        applied_agc_  = !applied_agc_;   // force the first apply
        applied_bias_ = !applied_bias_;
        applied_ppm_  = 1 << 20;
        applied_gain_ = -1.0;

        std::vector<float> level(static_cast<size_t>(steps_) * step_bins_);

        while (!stop_requested_)
        {
            apply_settings();

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

    DeviceInfo   info_;
    uint32_t     index_;
    rtlsdr_dev_t* dev_ = nullptr;
    std::vector<int> gains_;   // tenths of dB

    std::thread       worker_;
    std::atomic<bool> stop_requested_{false};

    // Configured in start(), then used by the worker only
    sa::Fft              fft_;
    unsigned             samples_    = 0;
    double               bin_hz_     = 1.0;
    double               grid_start_ = 0.0;
    int                  step_bins_  = 0;
    int                  steps_      = 0;
    size_t               last_bin_   = 0;
    bool                 applied_agc_  = false;
    bool                 applied_bias_ = false;
    int                  applied_ppm_  = 0;
    double               applied_gain_ = -1.0;
    std::vector<uint8_t> raw_;
    std::vector<int16_t> iq_;
    std::vector<float>   avg_;
};

} // unnamed namespace

void enumerate_rtlsdr(std::vector<DeviceEntry>& out)
{
    const uint32_t count = rtlsdr_get_device_count();

    for (uint32_t i = 0; i < count; ++i)
    {
        char manufacturer[256] = {};
        char product[256]      = {};
        char serial[256]       = {};
        rtlsdr_get_device_usb_strings(i, manufacturer, product, serial);

        DeviceEntry entry;
        entry.driver = "rtlsdr";
        entry.id     = std::to_string(i);
        entry.label  = std::string("RTL-SDR ") + (product[0] ? product : rtlsdr_get_device_name(i));
        if (serial[0])
            entry.label += std::string(" (") + serial + ")";

        out.push_back(entry);
    }
}

std::unique_ptr<SpectrumDevice> create_rtlsdr(const DeviceEntry& entry)
{
    return std::unique_ptr<SpectrumDevice>(new RtlSdrDevice(static_cast<uint32_t>(std::stoul(entry.id)), "RTL-SDR"));
}
