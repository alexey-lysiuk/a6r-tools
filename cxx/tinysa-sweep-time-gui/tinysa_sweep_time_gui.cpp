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
#include <array>
#include <cmath>
#include <cstdint>

#include "tinysa_sweep_time_gui.h"

#include "imgui.h"

namespace
{

constexpr int SD_NORMAL = 0;
constexpr int SD_PRECISE = 1;
constexpr int SD_FAST = 2;

constexpr uint32_t MINIMUM_SWEEP_TIME_FAST_US = 1800U;
constexpr uint32_t MINIMUM_SWEEP_TIME_SLOW_US = 15000U;
constexpr uint32_t REPEAT_TIME_US = 111U;
constexpr uint32_t MEASURE_TIME_US = 127U;
constexpr uint32_t ONE_MS_TIME_US = 1000U;

constexpr std::array<uint16_t, 9> SUPPORTED_RBW_X10 = {
    3, 10, 30, 100, 300, 1000, 3000, 6000, 8500
};

struct StepDelayEntry
{
    uint16_t rbw_x10;
    uint16_t step_delay;
};

constexpr std::array<StepDelayEntry, 9> STEP_DELAY_TABLE = {{
    {8500, 150},
    {6000, 150},
    {3000, 150},
    {1000, 300},
    {300, 400},
    {100, 900},
    {30, 1600},
    {10, 4000},
    {3, 18700}
}};

struct SweepTimeResult
{
    uint32_t minimum_sweep_time_us = 0;
    uint16_t actual_rbw_x10 = 0;
    uint16_t vbw_steps = 1;
    uint32_t step_delay_us = 0;
    double frequency_step_khz = 0.0;
};

uint16_t resolve_rbw_x10(uint16_t rbw_x10)
{
    const auto it = std::lower_bound(SUPPORTED_RBW_X10.begin(), SUPPORTED_RBW_X10.end(), rbw_x10);
    if (it == SUPPORTED_RBW_X10.end())
        return SUPPORTED_RBW_X10.back();
    return *it;
}

uint32_t resolve_step_delay_us(uint16_t actual_rbw_x10, int step_delay_mode, bool zero_span)
{
    if (zero_span)
        return 0;

    uint32_t step_delay = STEP_DELAY_TABLE.back().step_delay;
    for (const auto& entry : STEP_DELAY_TABLE)
    {
        if (actual_rbw_x10 >= entry.rbw_x10)
        {
            step_delay = entry.step_delay;
            break;
        }
    }

    if (step_delay_mode == SD_PRECISE)
        step_delay += (step_delay >> 2);

    return step_delay;
}

SweepTimeResult calculate_minimum_sweep_time(const AppState& state)
{
    constexpr double MIN_FREQ_MHZ = 0.0;
    constexpr double MAX_FREQ_MHZ = 6000.0;

    const double start_freq_mhz = std::clamp(state.start_freq_mhz, MIN_FREQ_MHZ, MAX_FREQ_MHZ);
    const double stop_freq_mhz = std::clamp(state.stop_freq_mhz, MIN_FREQ_MHZ, MAX_FREQ_MHZ);

    const int sweep_points = std::clamp(state.sweep_points, 2, 450);

    const double start_hz = start_freq_mhz * 1e6;
    const double stop_hz = stop_freq_mhz * 1e6;
    const double span_hz = std::max(0.0, stop_hz - start_hz);

    const bool zero_span = span_hz == 0.0;

    const double frequency_step_hz = zero_span ? 0.0 : (span_hz / static_cast<double>(sweep_points - 1));
    const uint32_t frequency_step_x10 = zero_span ? 3000U : static_cast<uint32_t>(std::llround(frequency_step_hz / 100.0));

    uint32_t target_rbw_x10 = state.rbw_khz <= 0.0
        ? static_cast<uint32_t>(state.step_delay_mode == SD_FAST ? frequency_step_x10 : 2 * frequency_step_x10)
        : static_cast<uint32_t>(std::llround(state.rbw_khz * 10.0));

    target_rbw_x10 = std::clamp(target_rbw_x10, 1U, 8500U);
    const uint16_t actual_rbw_x10 = resolve_rbw_x10(static_cast<uint16_t>(target_rbw_x10));

    uint16_t vbw_steps = 1;
    if (!zero_span)
    {
        uint32_t target_frequency_step_x10 = 0;
        if (state.step_delay_mode == SD_FAST)
            target_frequency_step_x10 = frequency_step_x10;
        else if (state.step_delay_mode == SD_PRECISE)
            target_frequency_step_x10 = 4 * frequency_step_x10;
        else
            target_frequency_step_x10 = 2 * frequency_step_x10;

        if (target_frequency_step_x10 > actual_rbw_x10)
            vbw_steps = static_cast<uint16_t>((target_frequency_step_x10 + actual_rbw_x10 - 1) / actual_rbw_x10);
    }

    const uint32_t step_delay_us = resolve_step_delay_us(actual_rbw_x10, state.step_delay_mode, zero_span);

    const uint32_t repeat = state.vbw_repeat <= 0 ? 1U : static_cast<uint32_t>(state.vbw_repeat);

    uint32_t bare_sweep_time_us = (step_delay_us + MEASURE_TIME_US) * static_cast<uint32_t>(sweep_points);

    if (zero_span)
    {
        bare_sweep_time_us = MINIMUM_SWEEP_TIME_FAST_US;
        const bool force_slow_zero_span = (repeat != 1U) ||
                                          (state.requested_sweep_time_ms * ONE_MS_TIME_US >= 100U * ONE_MS_TIME_US) ||
                                          state.spur_removal;
        if (force_slow_zero_span)
            bare_sweep_time_us = MINIMUM_SWEEP_TIME_SLOW_US;
    }

    uint32_t minimum_sweep_time_us = static_cast<uint32_t>(vbw_steps) * (state.spur_removal ? 2U : 1U) * bare_sweep_time_us;
    minimum_sweep_time_us += (repeat - 1U) * REPEAT_TIME_US * static_cast<uint32_t>(sweep_points);

    return {
        minimum_sweep_time_us,
        actual_rbw_x10,
        vbw_steps,
        step_delay_us,
        frequency_step_hz / 1000.0
    };
}

}

void draw_ui_content(AppState& state)
{
    state.start_freq_mhz = std::clamp(state.start_freq_mhz, 0.0, 6000.0);
    state.stop_freq_mhz = std::clamp(state.stop_freq_mhz, 0.0, 6000.0);
    state.sweep_points = std::clamp(state.sweep_points, 2, 450);
    state.rbw_khz = std::clamp(state.rbw_khz, 0.0, 850.0);
    state.vbw_repeat = std::clamp(state.vbw_repeat, 0, 1000);
    state.step_delay_mode = std::clamp(state.step_delay_mode, 0, 2);
    state.requested_sweep_time_ms = std::clamp(state.requested_sweep_time_ms, 0.0, 600000.0);

    const SweepTimeResult result = calculate_minimum_sweep_time(state);

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Spacing();
    ImGui::Text("tinySA Ultra Minimum Sweep Time Calculator");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float label_col = 230.0f;
    const float input_col = 140.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Start Frequency (MHz)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    ImGui::InputDouble("##start_mhz", &state.start_freq_mhz, 0.1, 1.0, "%.6f");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Stop Frequency (MHz)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    ImGui::InputDouble("##stop_mhz", &state.stop_freq_mhz, 0.1, 1.0, "%.6f");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Sweep Points");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    ImGui::InputInt("##points", &state.sweep_points, 1, 10);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("RBW (kHz, 0 = Auto)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    ImGui::InputDouble("##rbw_khz", &state.rbw_khz, 0.1, 1.0, "%.3f");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("VBW / Repeat (0 = Auto)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    ImGui::InputInt("##repeat", &state.vbw_repeat, 1, 10);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Sweep Mode");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    const char* modes[] = { "Normal", "Precise", "Fast" };
    ImGui::Combo("##mode", &state.step_delay_mode, modes, IM_ARRAYSIZE(modes));

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Spur Removal");
    ImGui::SameLine(label_col);
    ImGui::Checkbox("##spur", &state.spur_removal);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Requested Sweep Time (ms)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    ImGui::InputDouble("##requested_ms", &state.requested_sweep_time_ms, 1.0, 10.0, "%.3f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const double minimum_sweep_time_ms = static_cast<double>(result.minimum_sweep_time_us) / 1000.0;

    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                       "Minimum Sweep Time: %.3f ms (%.0f us)",
                       minimum_sweep_time_ms,
                       static_cast<double>(result.minimum_sweep_time_us));

    ImGui::Spacing();
    ImGui::TextDisabled("Actual RBW: %.1f kHz", static_cast<double>(result.actual_rbw_x10) / 10.0);
    ImGui::TextDisabled("VBW Steps: %u", result.vbw_steps);
    ImGui::TextDisabled("Step Delay: %u us", result.step_delay_us);
    ImGui::TextDisabled("Frequency Step: %.3f kHz", result.frequency_step_khz);

    ImGui::End();
}
