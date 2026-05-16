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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>

#include "hackrf_noise_gui.h"

#include "imgui.h"

static constexpr float MIN_FREQUENCY_MHZ = 1.0f;
static constexpr float MAX_FREQUENCY_MHZ = 6000.0f;
static constexpr float MIN_BANDWIDTH_MHZ = 2.0f;
static constexpr float MAX_BANDWIDTH_MHZ = 20.0f;
static constexpr int   MIN_VGA_GAIN      = 0;
static constexpr int   MAX_VGA_GAIN      = 47;

static std::atomic<bool> g_tx_running{false};

static int tx_callback(hackrf_transfer* transfer)
{
    uint8_t* buf = transfer->buffer;
    const int32_t len = transfer->valid_length;

    // Thread-local XorShift32 PRNG — fast and thread-safe
    static thread_local uint32_t state = 0;
    if (state == 0)
        state = (uint32_t)time(nullptr) | 1u;

    for (int32_t i = 0; i < len; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        buf[i] = (uint8_t)state;
    }

    return g_tx_running.load(std::memory_order_relaxed) ? 0 : -1;
}

bool start_transmit(hackrf_device** device,
                    float freq_mhz, float bw_mhz, int power_db,
                    std::string& error)
{
    if (!device)
    {
        error = "Invalid device pointer";
        return false;
    }

    bool device_opened = false;
    hackrf_device* opened_device = nullptr;

    auto report_and_cleanup = [&](const char* operation, int code, bool should_reset_tx_flag = false) -> bool
    {
        error = std::string(operation) + " failed: " + hackrf_error_name((hackrf_error)code);

        if (should_reset_tx_flag)
            g_tx_running.store(false, std::memory_order_relaxed);

        if (device_opened)
        {
            hackrf_close(opened_device);
            opened_device = nullptr;
            device_opened = false;
        }

        hackrf_exit();
        return false;
    };

    int result = hackrf_init();
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_init failed: ") + hackrf_error_name((hackrf_error)result);
        return false;
    }

    result = hackrf_open(&opened_device);
    if (result != HACKRF_SUCCESS)
        return report_and_cleanup("hackrf_open", result);
    device_opened = true;

    const uint32_t sample_rate = (uint32_t)(bw_mhz * 1e6f);
    result = hackrf_set_sample_rate(opened_device, sample_rate);
    if (result != HACKRF_SUCCESS)
        return report_and_cleanup("hackrf_set_sample_rate", result);

    const uint32_t filter_bw = hackrf_compute_baseband_filter_bw(sample_rate);
    result = hackrf_set_baseband_filter_bandwidth(opened_device, filter_bw);
    if (result != HACKRF_SUCCESS)
        return report_and_cleanup("hackrf_set_baseband_filter_bandwidth", result);

    const uint64_t freq_hz = (uint64_t)(freq_mhz * 1e6f);
    result = hackrf_set_freq(opened_device, freq_hz);
    if (result != HACKRF_SUCCESS)
        return report_and_cleanup("hackrf_set_freq", result);

    result = hackrf_set_txvga_gain(opened_device, (uint32_t)power_db);
    if (result != HACKRF_SUCCESS)
        return report_and_cleanup("hackrf_set_txvga_gain", result);

    g_tx_running.store(true, std::memory_order_relaxed);

    result = hackrf_start_tx(opened_device, tx_callback, nullptr);
    if (result != HACKRF_SUCCESS)
        return report_and_cleanup("hackrf_start_tx", result, true);

    *device = opened_device;
    return true;
}

void stop_transmit(hackrf_device** device)
{
    if (!*device)
        return;

    g_tx_running.store(false, std::memory_order_relaxed);
    hackrf_stop_tx(*device);
    hackrf_close(*device);
    hackrf_exit();
    *device = nullptr;
}

void draw_ui_content(AppState& state)
{
    // Detect unexpected TX stop
    if (state.transmitting && state.device &&
        hackrf_is_streaming(state.device) != HACKRF_TRUE)
    {
        stop_transmit(&state.device);
        state.transmitting = false;
        state.status_msg   = "Transmission stopped unexpectedly";
        state.status_error = true;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Root window fills the entire OS window
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar   |
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Spacing();
    ImGui::Text("HackRF Noise Generator");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Fixed column widths; slider fills the remaining space
    const float label_col  = 160.0f;
    const float input_col  = 110.0f;
    const float right_pad  = ImGui::GetStyle().WindowPadding.x;

    // ── Frequency ──────────────────────────────────────────────────────
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Frequency (MHz)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    bool freq_changed = ImGui::InputFloat("##freq_in", &state.freq_mhz, 0.1f, 1.0f, "%.3f");
    if (freq_changed)
        state.freq_mhz = std::clamp(state.freq_mhz, MIN_FREQUENCY_MHZ, MAX_FREQUENCY_MHZ);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-right_pad);
    freq_changed |= ImGui::SliderFloat("##freq_sl", &state.freq_mhz,
                                       MIN_FREQUENCY_MHZ, MAX_FREQUENCY_MHZ, "");

    // ── Bandwidth ──────────────────────────────────────────────────────
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bandwidth (MHz)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    bool bw_changed = ImGui::InputFloat("##bw_in", &state.bw_mhz, 0.5f, 1.0f, "%.1f");
    if (bw_changed)
        state.bw_mhz = std::clamp(state.bw_mhz, MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-right_pad);
    bw_changed |= ImGui::SliderFloat("##bw_sl", &state.bw_mhz,
                                     MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ, "");

    // ── TX VGA Gain ────────────────────────────────────────────────────
    ImGui::AlignTextToFramePadding();
    ImGui::Text("TX VGA Gain (dB)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    bool power_changed = ImGui::InputInt("##pwr_in", &state.power_db, 1, 5);
    if (power_changed)
        state.power_db = std::clamp(state.power_db, MIN_VGA_GAIN, MAX_VGA_GAIN);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-right_pad);
    power_changed |= ImGui::SliderInt("##pwr_sl", &state.power_db,
                                      MIN_VGA_GAIN, MAX_VGA_GAIN, "");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── On-the-fly parameter updates ───────────────────────────────────
    if (state.transmitting && state.device)
    {
        if (freq_changed && state.freq_mhz != state.applied_freq_mhz)
        {
            const uint64_t freq_hz = (uint64_t)(state.freq_mhz * 1e6f);
            const int result = hackrf_set_freq(state.device, freq_hz);
            if (result == HACKRF_SUCCESS)
            {
                state.applied_freq_mhz = state.freq_mhz;
                state.status_msg       = "Transmitting";
                state.status_error     = false;
            }
            else
            {
                state.freq_mhz   = state.applied_freq_mhz;
                state.status_msg = std::string("Failed to set frequency: ") +
                                   hackrf_error_name((hackrf_error)result);
                state.status_error = true;
            }
        }

        if (bw_changed && state.bw_mhz != state.applied_bw_mhz)
        {
            const uint32_t sample_rate = (uint32_t)(state.bw_mhz * 1e6f);
            int result = hackrf_set_sample_rate(state.device, sample_rate);
            if (result == HACKRF_SUCCESS)
            {
                const uint32_t filter_bw = hackrf_compute_baseband_filter_bw(sample_rate);
                result = hackrf_set_baseband_filter_bandwidth(state.device, filter_bw);
            }
            if (result == HACKRF_SUCCESS)
            {
                state.applied_bw_mhz = state.bw_mhz;
                state.status_msg     = "Transmitting";
                state.status_error   = false;
            }
            else
            {
                state.bw_mhz     = state.applied_bw_mhz;
                state.status_msg = std::string("Failed to set bandwidth: ") +
                                   hackrf_error_name((hackrf_error)result);
                state.status_error = true;
            }
        }

        if (power_changed && state.power_db != state.applied_power_db)
        {
            const int result = hackrf_set_txvga_gain(state.device, (uint32_t)state.power_db);
            if (result == HACKRF_SUCCESS)
            {
                state.applied_power_db = state.power_db;
                state.status_msg       = "Transmitting";
                state.status_error     = false;
            }
            else
            {
                state.power_db   = state.applied_power_db;
                state.status_msg = std::string("Failed to set gain: ") +
                                   hackrf_error_name((hackrf_error)result);
                state.status_error = true;
            }
        }
    }

    // ── Start / Stop button ────────────────────────────────────────────
    const float btn_width = ImGui::GetContentRegionAvail().x;

    if (!state.transmitting)
    {
        if (ImGui::Button("Start Transmitting", ImVec2(btn_width, 0)))
        {
            std::string error;
            if (start_transmit(&state.device, state.freq_mhz, state.bw_mhz, state.power_db, error))
            {
                state.transmitting      = true;
                state.applied_freq_mhz  = state.freq_mhz;
                state.applied_bw_mhz    = state.bw_mhz;
                state.applied_power_db  = state.power_db;
                state.status_msg        = "Transmitting";
                state.status_error      = false;
            }
            else
            {
                state.status_msg   = error;
                state.status_error = true;
            }
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button("Stop Transmitting", ImVec2(btn_width, 0)))
        {
            stop_transmit(&state.device);
            state.transmitting = false;
            state.status_msg   = "Idle";
            state.status_error = false;
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();

    // ── Status line ────────────────────────────────────────────────────
    if (state.status_error)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", state.status_msg.c_str());
    else if (state.transmitting)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Status: %s", state.status_msg.c_str());
    else
        ImGui::TextDisabled("Status: %s", state.status_msg.c_str());

    ImGui::End();
}
