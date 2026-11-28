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

// Fobos SDR driver. Supports both firmware variants: the original one through
// libfobos (fobos_rx_*) and the agile one through libfobos-sdr-agile (fobos_sdr_*).
// The APIs differ only in names, so one driver is instantiated with a small
// traits class for each. The device has no sweep mode in the way it is used here:
// the host steps the tuner across the range, reads a block of samples at every
// step and averages FFTs.

#include "device_drivers.h"
#include "fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <thread>

#ifdef SA_HAVE_FOBOS
#include <fobos.h>
#endif
#ifdef SA_HAVE_FOBOS_AGILE
#include <fobos_sdr.h>
#endif

namespace
{

constexpr double   MIN_FREQ_HZ   = 50e6;
constexpr double   MAX_FREQ_HZ   = 6e9;
constexpr double   USABLE_BAND   = 0.7;       // fraction of the sample rate kept from every step
constexpr unsigned BLOCK_SAMPLES = 16384;     // complex samples per read, multiple of 8192
constexpr int      SETTLE_BLOCKS = 2;         // blocks dropped after every retune

const double SAMPLE_RATES_MHZ[] = { 8.0, 10.0, 12.5, 16.0, 20.0, 25.0, 32.0, 40.0, 50.0 };
const char* const CLOCK_CHOICES[] = { "internal", "external" };

#ifdef SA_HAVE_FOBOS
struct OriginalApi
{
    using Device = fobos_dev_t;

    static constexpr const char* driver = "fobos";
    static constexpr const char* model  = "Fobos SDR";
    static constexpr bool        agile  = false;

    static int count() { return fobos_rx_get_device_count(); }
    static int list(char* serials) { return fobos_rx_list_devices(serials); }
    static int open(Device** dev, uint32_t index) { return fobos_rx_open(dev, index); }
    static int close(Device* dev) { return fobos_rx_close(dev); }
    static int set_frequency(Device* dev, double hz) { double actual; return fobos_rx_set_frequency(dev, hz, &actual); }
    static int set_samplerate(Device* dev, double hz) { double actual; return fobos_rx_set_samplerate(dev, hz, &actual); }
    static int set_bandwidth_ratio(Device*, double) { return 0; }
    static int set_lna_gain(Device* dev, unsigned v) { return fobos_rx_set_lna_gain(dev, v); }
    static int set_vga_gain(Device* dev, unsigned v) { return fobos_rx_set_vga_gain(dev, v); }
    static int set_clk_source(Device* dev, int v) { return fobos_rx_set_clk_source(dev, v); }
    static int start_sync(Device* dev, uint32_t len) { return fobos_rx_start_sync(dev, len); }
    static int read_sync(Device* dev, float* buf, uint32_t* len) { return fobos_rx_read_sync(dev, buf, len); }
    static int stop_sync(Device* dev) { return fobos_rx_stop_sync(dev); }
    static const char* error_name(int e) { return fobos_rx_error_name(e); }
};
#endif

#ifdef SA_HAVE_FOBOS_AGILE
struct AgileApi
{
    using Device = fobos_sdr_dev_t;

    static constexpr const char* driver = "fobos-agile";
    static constexpr const char* model  = "Fobos SDR (agile)";
    static constexpr bool        agile  = true;

    static int count() { return fobos_sdr_get_device_count(); }
    static int list(char* serials) { return fobos_sdr_list_devices(serials); }
    static int open(Device** dev, uint32_t index) { return fobos_sdr_open(dev, index); }
    static int close(Device* dev) { return fobos_sdr_close(dev); }
    static int set_frequency(Device* dev, double hz) { return fobos_sdr_set_frequency(dev, hz); }
    static int set_samplerate(Device* dev, double hz) { return fobos_sdr_set_samplerate(dev, hz); }
    static int set_bandwidth_ratio(Device* dev, double r) { return fobos_sdr_set_auto_bandwidth(dev, r); }
    static int set_lna_gain(Device* dev, unsigned v) { return fobos_sdr_set_lna_gain(dev, v); }
    static int set_vga_gain(Device* dev, unsigned v) { return fobos_sdr_set_vga_gain(dev, v); }
    static int set_clk_source(Device* dev, int v) { return fobos_sdr_set_clk_source(dev, v); }
    static int start_sync(Device* dev, uint32_t len) { return fobos_sdr_start_sync(dev, len); }
    static int read_sync(Device* dev, float* buf, uint32_t* len) { return fobos_sdr_read_sync(dev, buf, len); }
    static int stop_sync(Device* dev) { return fobos_sdr_stop_sync(dev); }
    static const char* error_name(int e) { return fobos_sdr_error_name(e); }
};
#endif

template <typename Api>
class FobosDevice final : public SpectrumDevice
{
public:
    explicit FobosDevice(uint32_t index)
    : index_(index)
    {
        info_.model               = Api::model;
        info_.level_unit          = "dBFS";
        info_.min_hz              = MIN_FREQ_HZ;
        info_.max_hz              = MAX_FREQ_HZ;
        info_.points_configurable = false;

        DeviceSetting rate;
        rate.id      = "sample_rate";
        rate.label   = "Sample rate";
        rate.unit    = "MSPS";
        rate.tooltip = "Wider steps sweep faster; the hardware picks the nearest supported rate";
        rate.type    = DeviceSetting::Choice;
        rate.requires_restart = true;
        for (double r : SAMPLE_RATES_MHZ)
        {
            char text[32];
            snprintf(text, sizeof(text), "%g", r);
            rate.choices.push_back(text);
        }
        rate.value = 4;   // 20 MSPS
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
        bins.value = 4;   // 1024
        bins.max   = static_cast<double>(bins.choices.size() - 1);
        settings_.push_back(bins);

        DeviceSetting lna;
        lna.id      = "lna_gain";
        lna.label   = "LNA gain";
        lna.tooltip = "Low noise amplifier: 0 and 1 = 0 dB, 2 = +16 dB, 3 = +33 dB";
        lna.type    = DeviceSetting::Int;
        lna.value   = 0;
        lna.min     = 0;
        lna.max     = 3;
        settings_.push_back(lna);

        DeviceSetting vga;
        vga.id      = "vga_gain";
        vga.label   = "VGA gain";
        vga.tooltip = "Variable gain amplifier, 0-31, 2 dB per step";
        vga.type    = DeviceSetting::Int;
        vga.value   = 10;
        vga.min     = 0;
        vga.max     = 31;
        settings_.push_back(vga);

        DeviceSetting clock;
        clock.id      = "clock";
        clock.label   = "Clock source";
        clock.type    = DeviceSetting::Choice;
        clock.requires_restart = true;
        for (const char* c : CLOCK_CHOICES)
            clock.choices.push_back(c);
        clock.max = static_cast<double>(clock.choices.size() - 1);
        settings_.push_back(clock);

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

    ~FobosDevice() override { close(); }

    const DeviceInfo& info() const override { return info_; }

    bool open(std::string& error) override
    {
        if (dev_)
            return true;

        const int result = Api::open(&dev_, index_);
        if (result != 0)
        {
            dev_ = nullptr;
            error = std::string("Cannot open Fobos SDR: ") + Api::error_name(result);
            return false;
        }

        return true;
    }

    void close() override
    {
        stop();

        if (dev_)
        {
            Api::close(dev_);
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

        int result = Api::set_clk_source(dev_, static_cast<int>(setting_value("clock")));
        if (result == 0)
            result = Api::set_samplerate(dev_, fs);
        if (result == 0)
            result = Api::set_bandwidth_ratio(dev_, 0.9);
        if (result == 0)
            result = Api::start_sync(dev_, BLOCK_SAMPLES);
        if (result != 0)
        {
            error = std::string("Cannot start streaming: ") + Api::error_name(result);
            return false;
        }

        sync_started_ = true;

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

        stop_requested_ = false;
        running_        = true;
        worker_ = std::thread(&FobosDevice::run, this);
        return true;
    }

    void stop() override
    {
        if (worker_.joinable())
        {
            stop_requested_ = true;
            worker_.join();
        }

        if (dev_ && sync_started_)
        {
            Api::stop_sync(dev_);
            sync_started_ = false;
        }

        running_ = false;
    }

private:
    void apply_gains()
    {
        const int lna = static_cast<int>(setting_value("lna_gain"));
        const int vga = static_cast<int>(setting_value("vga_gain"));

        if (lna != applied_lna_)
        {
            if (Api::set_lna_gain(dev_, static_cast<unsigned>(lna)) != 0)
                set_error("Cannot apply LNA gain");
            applied_lna_ = lna;
        }

        if (vga != applied_vga_)
        {
            if (Api::set_vga_gain(dev_, static_cast<unsigned>(vga)) != 0)
                set_error("Cannot apply VGA gain");
            applied_vga_ = vga;
        }
    }

    bool read_block()
    {
        raw_.resize(static_cast<size_t>(samples_) * 2);
        uint32_t got = 0;
        const int result = Api::read_sync(dev_, raw_.data(), &got);
        return result == 0 && got >= samples_;
    }

    bool capture_step(int step, std::vector<float>& level)
    {
        const double center = grid_start_ + (static_cast<double>(step) * step_bins_ + step_bins_ / 2) * bin_hz_;
        if (Api::set_frequency(dev_, center) != 0)
        {
            set_error("Cannot tune to " + std::to_string(center / 1e6) + " MHz");
            return false;
        }

        for (int i = 0; i <= SETTLE_BLOCKS; ++i)
        {
            if (!read_block())
                return false;
        }

        double mean_i = 0.0, mean_q = 0.0;
        for (unsigned k = 0; k < samples_; ++k)
        {
            mean_i += raw_[k * 2];
            mean_q += raw_[k * 2 + 1];
        }
        mean_i /= samples_;
        mean_q /= samples_;

        for (unsigned k = 0; k < samples_; ++k)
        {
            raw_[k * 2]     -= static_cast<float>(mean_i);
            raw_[k * 2 + 1] -= static_cast<float>(mean_q);
        }

        const int n = fft_.size();
        const int segments = static_cast<int>(samples_) / n;
        avg_.assign(n, 0.0f);
        for (int s = 0; s < segments; ++s)
        {
            const std::vector<float>& pwr = fft_.transform(&raw_[static_cast<size_t>(s) * n * 2], 1.0f);
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
        applied_lna_ = applied_vga_ = -1;
        std::vector<float> level(static_cast<size_t>(steps_) * step_bins_);

        while (!stop_requested_)
        {
            apply_gains();

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

    DeviceInfo          info_;
    uint32_t            index_;
    typename Api::Device* dev_ = nullptr;
    bool                sync_started_ = false;

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
    int                applied_lna_ = -1;
    int                applied_vga_ = -1;
    std::vector<float> raw_;
    std::vector<float> avg_;
};

template <typename Api>
void enumerate(std::vector<DeviceEntry>& out)
{
    if (Api::count() <= 0)
        return;

    char serials[8192] = {};
    if (Api::list(serials) < 0)
        return;

    std::istringstream stream(serials);
    std::string serial;
    int index = 0;

    while (stream >> serial)
    {
        DeviceEntry entry;
        entry.driver = Api::driver;
        entry.id     = std::to_string(index++);
        entry.label  = std::string(Api::model) + " (" + serial + ")";
        out.push_back(entry);
    }
}

template <typename Api>
std::unique_ptr<SpectrumDevice> create(const DeviceEntry& entry)
{
    return std::unique_ptr<SpectrumDevice>(new FobosDevice<Api>(static_cast<uint32_t>(std::stoul(entry.id))));
}

} // unnamed namespace

#ifdef SA_HAVE_FOBOS
void enumerate_fobos(std::vector<DeviceEntry>& out) { enumerate<OriginalApi>(out); }
std::unique_ptr<SpectrumDevice> create_fobos(const DeviceEntry& entry) { return create<OriginalApi>(entry); }
#endif

#ifdef SA_HAVE_FOBOS_AGILE
void enumerate_fobos_agile(std::vector<DeviceEntry>& out) { enumerate<AgileApi>(out); }
std::unique_ptr<SpectrumDevice> create_fobos_agile(const DeviceEntry& entry) { return create<AgileApi>(entry); }
#endif
