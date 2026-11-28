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

// HackRF driver. Uses firmware sweep mode (as hackrf_sweep does): the device
// retunes by itself and tags every 16 KiB block with its tuning frequency,
// the host only has to run an FFT over each block.

#include "device_drivers.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <hackrf.h>

namespace
{

constexpr uint32_t SAMPLE_RATE_HZ   = 20000000;
constexpr uint32_t BASEBAND_BW_HZ   = 15000000;
constexpr uint32_t TUNE_STEP_MHZ    = 20;
constexpr uint32_t TUNE_OFFSET_HZ   = 7500000;
constexpr uint32_t BLOCK_BYTES  = 16384;
constexpr uint32_t MAX_FREQ_MHZ     = 7250;
constexpr double   MIN_FREQ_HZ      = 1e6;
constexpr double   MAX_FREQ_HZ      = 6e9;
constexpr double   kPi              = 3.14159265358979323846;

bool init_libhackrf()
{
    static const bool ok = (hackrf_init() == HACKRF_SUCCESS);
    return ok;
}

class Fft
{
public:
    void setup(int size)
    {
        size_ = size;

        twiddle_.resize(size / 2);
        for (int i = 0; i < size / 2; ++i)
        {
            const double angle = -2.0 * kPi * i / size;
            twiddle_[i] = { static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)) };
        }

        bitrev_.resize(size);
        int bits = 0;
        while ((1 << bits) < size)
            ++bits;
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            bitrev_[i] = r;
        }

        window_.resize(size);
        window_sum_ = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            window_[i] = 0.5f * (1.0f - std::cos(static_cast<float>(2.0 * kPi * i / (size - 1))));
            window_sum_ += window_[i];
        }

        data_.assign(size, {});
        power_.assign(size, 0.0f);
    }

    int size() const { return size_; }

    // Input: interleaved signed 8-bit I/Q. Output: power in dBFS per FFT bin (unshifted)
    const std::vector<float>& transform(const int8_t* iq)
    {
        for (int i = 0; i < size_; ++i)
        {
            const float w = window_[i] / 128.0f;
            data_[bitrev_[i]] = { iq[i * 2] * w, iq[i * 2 + 1] * w };
        }

        for (int len = 2; len <= size_; len <<= 1)
        {
            const int half = len / 2;
            const int stride = size_ / len;

            for (int base = 0; base < size_; base += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const std::complex<float> t = data_[base + k + half] * twiddle_[k * stride];
                    const std::complex<float> u = data_[base + k];
                    data_[base + k]        = u + t;
                    data_[base + k + half] = u - t;
                }
            }
        }

        const float scale = 1.0f / (window_sum_ * window_sum_);
        for (int i = 0; i < size_; ++i)
        {
            const float magsq = std::norm(data_[i]) * scale;
            power_[i] = 10.0f * std::log10(magsq + 1e-20f);
        }

        return power_;
    }

private:
    int size_ = 0;
    float window_sum_ = 1.0f;
    std::vector<std::complex<float>> twiddle_;
    std::vector<std::complex<float>> data_;
    std::vector<int>   bitrev_;
    std::vector<float> window_;
    std::vector<float> power_;
};

class HackRfDevice final : public SpectrumDevice
{
public:
    explicit HackRfDevice(const std::string& serial)
    : serial_(serial)
    {
        info_.model                = "HackRF";
        info_.level_unit           = "dBFS";
        info_.min_hz               = MIN_FREQ_HZ;
        info_.max_hz               = MAX_FREQ_HZ;
        info_.points_configurable  = false;

        DeviceSetting bins;
        bins.id      = "fft_size";
        bins.label   = "FFT bin width";
        bins.tooltip = "Frequency resolution; narrower bins mean more points and slower sweeps";
        bins.type    = DeviceSetting::Choice;
        bins.requires_restart = true;
        for (int n = 16; n <= 4096; n *= 2)
        {
            char text[32];
            const double bw = static_cast<double>(SAMPLE_RATE_HZ) / n;
            if (bw >= 1e6)
                snprintf(text, sizeof(text), "%.3g MHz (%d)", bw / 1e6, n);
            else
                snprintf(text, sizeof(text), "%.4g kHz (%d)", bw / 1e3, n);
            bins.choices.push_back(text);
        }
        bins.value = 5;   // 512-point FFT, ~39 kHz
        bins.max   = static_cast<double>(bins.choices.size() - 1);
        settings_.push_back(bins);

        DeviceSetting lna;
        lna.id = "lna_gain";
        lna.label = "LNA gain";
        lna.unit = "dB";
        lna.tooltip = "RX IF gain, 0-40 dB in 8 dB steps";
        lna.type = DeviceSetting::Int;
        lna.value = 16;
        lna.min = 0;
        lna.max = 40;
        lna.step = 8;
        settings_.push_back(lna);

        DeviceSetting vga;
        vga.id = "vga_gain";
        vga.label = "VGA gain";
        vga.unit = "dB";
        vga.tooltip = "RX baseband gain, 0-62 dB in 2 dB steps";
        vga.type = DeviceSetting::Int;
        vga.value = 20;
        vga.min = 0;
        vga.max = 62;
        vga.step = 2;
        settings_.push_back(vga);

        DeviceSetting amp;
        amp.id = "amp";
        amp.label = "RF amplifier (+14 dB)";
        amp.tooltip = "Front-end RX/TX amplifier; may overload on strong signals";
        amp.type = DeviceSetting::Bool;
        settings_.push_back(amp);

        DeviceSetting bias;
        bias.id = "bias";
        bias.label = "Antenna port power (bias tee)";
        bias.tooltip = "Supplies 3.3 V on the antenna port. Do not enable unless an active antenna needs it";
        bias.type = DeviceSetting::Bool;
        settings_.push_back(bias);

        DeviceSetting offset;
        offset.id = "offset";
        offset.label = "Level offset";
        offset.unit = "dB";
        offset.tooltip = "Added to the measured dBFS level, e.g. to calibrate against a known source";
        offset.type = DeviceSetting::Float;
        offset.value = 0;
        offset.min = -150;
        offset.max = 150;
        settings_.push_back(offset);
    }

    ~HackRfDevice() override { close(); }

    const DeviceInfo& info() const override { return info_; }

    bool open(std::string& error) override
    {
        if (dev_)
            return true;

        if (!init_libhackrf())
        {
            error = "hackrf_init() failed";
            return false;
        }

        const int result = serial_.empty()
            ? hackrf_open(&dev_)
            : hackrf_open_by_serial(serial_.c_str(), &dev_);
        if (result != HACKRF_SUCCESS)
        {
            dev_ = nullptr;
            error = std::string("Cannot open HackRF: ") + hackrf_error_name(static_cast<hackrf_error>(result));
            return false;
        }

        return true;
    }

    void close() override
    {
        stop();

        if (dev_)
        {
            hackrf_close(dev_);
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

        const int fft_size = 16 << static_cast<int>(setting_value("fft_size"));

        uint32_t start_mhz = static_cast<uint32_t>(std::floor(std::max(config.start_hz, MIN_FREQ_HZ) / 1e6));
        uint32_t stop_mhz  = static_cast<uint32_t>(std::ceil(config.stop_hz / 1e6));
        if (stop_mhz <= start_mhz)
            stop_mhz = start_mhz + 1;

        // Whole number of tuning steps, as hackrf_sweep does
        const uint32_t steps = 1 + (stop_mhz - start_mhz - 1) / TUNE_STEP_MHZ;
        stop_mhz = start_mhz + steps * TUNE_STEP_MHZ;
        if (stop_mhz > MAX_FREQ_MHZ)
        {
            error = "Frequency range is out of HackRF limits";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);

            fft_.setup(fft_size);
            grid_start_hz_ = static_cast<double>(start_mhz) * 1e6;
            first_header_  = static_cast<uint64_t>(start_mhz) * 1000000ull;
            bin_hz_        = static_cast<double>(SAMPLE_RATE_HZ) / fft_size;
            total_bins_    = static_cast<size_t>(steps) * fft_size;
            level_.assign(total_bins_, -200.0f);
            sweep_started_ = false;

            const double lo = (config.start_hz - grid_start_hz_) / bin_hz_;
            const double hi = (config.stop_hz - grid_start_hz_) / bin_hz_;
            crop_first_ = static_cast<size_t>(std::max(0.0, std::ceil(lo)));
            crop_last_  = static_cast<size_t>(std::max(0.0, std::floor(hi)));
            crop_last_  = std::min(crop_last_, total_bins_ - 1);
            if (crop_last_ <= crop_first_)
                crop_last_ = std::min(crop_first_ + 1, total_bins_ - 1);
        }

        clear_sweeps();

        uint16_t range[2] = { static_cast<uint16_t>(start_mhz), static_cast<uint16_t>(stop_mhz) };

        int result = hackrf_set_sample_rate_manual(dev_, SAMPLE_RATE_HZ, 1);
        if (result == HACKRF_SUCCESS)
            result = hackrf_set_baseband_filter_bandwidth(dev_, BASEBAND_BW_HZ);
        if (result == HACKRF_SUCCESS)
            result = hackrf_init_sweep(dev_, range, 1, BLOCK_BYTES,
                                       TUNE_STEP_MHZ * 1000000u, TUNE_OFFSET_HZ, INTERLEAVED);
        if (result == HACKRF_SUCCESS)
            result = hackrf_start_rx_sweep(dev_, &HackRfDevice::rx_callback, this);
        if (result != HACKRF_SUCCESS)
        {
            error = std::string("Cannot start sweep: ") + hackrf_error_name(static_cast<hackrf_error>(result));
            return false;
        }

        apply_all_settings();
        running_ = true;
        return true;
    }

    void stop() override
    {
        if (dev_ && running_)
            hackrf_stop_rx(dev_);

        running_ = false;
    }

    bool running() const override
    {
        return running_ && dev_ && hackrf_is_streaming(dev_) == HACKRF_TRUE;
    }

protected:
    void on_setting_changed(const DeviceSetting& s) override
    {
        if (dev_ && running_)
            apply_setting(s.id, s.value);
    }

private:
    void apply_all_settings()
    {
        for (const DeviceSetting& s : settings_)
            apply_setting(s.id, s.value);
    }

    void apply_setting(const std::string& id, double value)
    {
        int result = HACKRF_SUCCESS;

        if (id == "lna_gain")
            result = hackrf_set_lna_gain(dev_, static_cast<uint32_t>(value));
        else if (id == "vga_gain")
            result = hackrf_set_vga_gain(dev_, static_cast<uint32_t>(value));
        else if (id == "amp")
            result = hackrf_set_amp_enable(dev_, value != 0.0 ? 1 : 0);
        else if (id == "bias")
            result = hackrf_set_antenna_enable(dev_, value != 0.0 ? 1 : 0);

        if (result != HACKRF_SUCCESS)
            set_error(std::string("Cannot apply setting '") + id + "': "
                      + hackrf_error_name(static_cast<hackrf_error>(result)));
    }

    static int rx_callback(hackrf_transfer* transfer)
    {
        static_cast<HackRfDevice*>(transfer->rx_ctx)->process(transfer);
        return 0;
    }

    void process(const hackrf_transfer* transfer)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        const int n = fft_.size();
        const float offset = static_cast<float>(setting_value("offset"));
        const int8_t* buf = reinterpret_cast<const int8_t*>(transfer->buffer);
        const int blocks = transfer->valid_length / BLOCK_BYTES;

        for (int b = 0; b < blocks; ++b, buf += BLOCK_BYTES)
        {
            const uint8_t* ubuf = reinterpret_cast<const uint8_t*>(buf);
            if (ubuf[0] != 0x7F || ubuf[1] != 0x7F)
                continue;

            uint64_t frequency = 0;
            for (int i = 9; i >= 2; --i)
                frequency = (frequency << 8) | ubuf[i];

            if (frequency == first_header_)
            {
                if (sweep_started_)
                    publish(offset);
                sweep_started_ = true;
            }

            if (!sweep_started_ || frequency < first_header_ || frequency > static_cast<uint64_t>(MAX_FREQ_MHZ) * 1000000ull)
                continue;

            // Last N samples of the block; the head is skipped to let the tuner settle
            const std::vector<float>& pwr = fft_.transform(buf + BLOCK_BYTES - n * 2);

            const size_t base = static_cast<size_t>((frequency - first_header_) * n / SAMPLE_RATE_HZ);
            store(base + 1,         &pwr[1 + (n * 5) / 8], n / 4);
            store(base + n / 2 + 1, &pwr[1 + n / 8],       n / 4);
        }
    }

    void store(size_t index, const float* src, int count)
    {
        for (int i = 0; i < count && index + i < total_bins_; ++i)
            level_[index + i] = src[i];
    }

    void publish(float offset)
    {
        Sweep sweep;
        sweep.start_hz = grid_start_hz_ + crop_first_ * bin_hz_;
        sweep.stop_hz  = grid_start_hz_ + crop_last_ * bin_hz_;
        sweep.level.assign(level_.begin() + crop_first_, level_.begin() + crop_last_ + 1);

        if (offset != 0.0f)
        {
            for (float& v : sweep.level)
                v += offset;
        }

        push_sweep(std::move(sweep));
    }

    DeviceInfo    info_;
    std::string   serial_;
    hackrf_device* dev_ = nullptr;

    // Accessed from the USB callback thread, guarded by state_mutex_
    std::mutex         state_mutex_;
    Fft                fft_;
    std::vector<float> level_;
    double             grid_start_hz_ = 0.0;
    double             bin_hz_        = 1.0;
    uint64_t           first_header_  = 0;
    size_t             total_bins_    = 0;
    size_t             crop_first_    = 0;
    size_t             crop_last_     = 0;
    bool               sweep_started_ = false;
};

} // unnamed namespace

void enumerate_hackrf(std::vector<DeviceEntry>& out)
{
    if (!init_libhackrf())
        return;

    hackrf_device_list_t* list = hackrf_device_list();
    if (!list)
        return;

    for (int i = 0; i < list->devicecount; ++i)
    {
        DeviceEntry entry;
        entry.driver = "hackrf";
        entry.id     = list->serial_numbers[i] ? list->serial_numbers[i] : "";

        std::string label = "HackRF ";
        label += hackrf_usb_board_id_name(static_cast<hackrf_usb_board_id>(list->usb_board_ids[i]));
        if (entry.id.size() > 8)
            label += " (..." + entry.id.substr(entry.id.size() - 8) + ")";
        entry.label = label;

        out.push_back(entry);
    }

    hackrf_device_list_free(list);
}

std::unique_ptr<SpectrumDevice> create_hackrf(const DeviceEntry& entry)
{
    return std::unique_ptr<SpectrumDevice>(new HackRfDevice(entry.id));
}
