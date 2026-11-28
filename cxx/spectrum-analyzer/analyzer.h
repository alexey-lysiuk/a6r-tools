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

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "device.h"
#include "trace.h"

class Analyzer
{
public:
    Analyzer();
    ~Analyzer();

    // Must be called between ImGui::NewFrame() and ImGui::Render()
    void draw();

    // Releases device and GPU resources; call before the graphics backend shuts down
    void shutdown();

private:
    static constexpr int TRACE_COUNT = 4;
    static constexpr int WATERFALL_WIDTH  = 1024;
    static constexpr int WATERFALL_HEIGHT = 256;

    // Device handling
    void refresh_devices();
    void connect();
    void disconnect();
    // keep_data preserves traces and waterfall; they are still cleared if the sweep geometry changes
    bool restart(bool keep_data = false);
    void poll_device();
    void process_sweep(const Sweep& sweep);
    void reset_data();

    // Frequency settings
    void apply_frequency();
    void sync_frequency_fields();

    // Amplitude
    void auto_tune();

    // UI
    void draw_controls();
    void draw_device_section();
    void draw_frequency_section();
    void draw_amplitude_section();
    void draw_traces_section();
    void draw_waterfall_section();
    void draw_device_settings();
    void draw_spectrum(const ImVec2& pos, const ImVec2& size);
    void draw_waterfall(const ImVec2& pos, const ImVec2& size);
    void update_waterfall_texture();

    std::unique_ptr<SpectrumDevice> device_;
    std::vector<DeviceEntry>        entries_;
    int                             entry_index_ = 0;
    std::string                     status_;
    bool                            status_error_ = false;

    // Frequency
    double start_hz_  = 88e6;
    double stop_hz_   = 108e6;
    int    freq_mode_ = 0;     // 0: start/stop, 1: center/span
    double field_a_   = 0.0;   // MHz; start or center
    double field_b_   = 0.0;   // MHz; stop or span
    int    points_    = 450;

    // Amplitude
    float ref_level_ = 0.0f;
    float scale_     = 10.0f;   // dB per division
    bool  auto_tune_ = true;
    double last_auto_tune_ = 0.0;

    // Data
    Trace              traces_[TRACE_COUNT];
    Sweep              last_sweep_;
    bool               have_sweep_ = false;
    double             last_sweep_time_ = 0.0;
    unsigned           sweep_counter_ = 0;
    double             fps_time_ = 0.0;
    unsigned           fps_count_ = 0;
    float              sweep_rate_ = 0.0f;

    // Waterfall
    bool                           show_waterfall_ = true;
    float                          waterfall_split_ = 0.5f;   // fraction of height for spectrum
    std::deque<std::vector<float>> waterfall_rows_;
    std::vector<uint32_t>          waterfall_pixels_;
    ImTextureID                    waterfall_texture_ = 0;
    bool                           waterfall_dirty_ = true;
    float                          waterfall_floor_ = -100.0f;
    float                          waterfall_ceil_  = 0.0f;
};
