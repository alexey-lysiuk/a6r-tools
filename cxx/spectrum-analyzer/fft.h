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

#include <cmath>
#include <complex>
#include <vector>

namespace sa
{

constexpr double kPi = 3.14159265358979323846;

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

    // Input: interleaved integer I/Q, full_scale_inv = 1 / (full scale amplitude).
    // Output: power in dBFS per FFT bin (unshifted)
    template <typename T>
    const std::vector<float>& transform(const T* iq, float full_scale_inv)
    {
        for (int i = 0; i < size_; ++i)
        {
            const float w = window_[i] * full_scale_inv;
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

} // namespace sa
