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
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#include <ad9361.h>
#include <iio.h>

#include "libresdr_noise_gui.h"

#include "imgui.h"

static constexpr uint64_t MIN_FREQUENCY_HZ  = 1000000ULL;
static constexpr uint64_t MAX_FREQUENCY_HZ  = 6000000000ULL;
static constexpr int64_t  MIN_BANDWIDTH_HZ  = 1000000LL;
static constexpr int64_t  MAX_BANDWIDTH_HZ  = 100000000LL;
static constexpr int      MIN_POWER_DB      = -90;
static constexpr int      MAX_POWER_DB      = 0;
static constexpr uint32_t TX_MIN_SAMPLE_RATE_HZ = 20000000UL;
static constexpr uint32_t TX_MAX_SAMPLE_RATE_HZ = 61440000UL;
static constexpr size_t   TX_BUFFER_SAMPLES = 16384;

struct TxRuntime
{
    iio_context*      context   = nullptr;
    iio_device*       phy       = nullptr;
    iio_device*       tx_dev    = nullptr;
    iio_channel*      phy_tx    = nullptr;
    iio_channel*      lo_tx     = nullptr;
    iio_channel*      tx_i      = nullptr;
    iio_channel*      tx_q      = nullptr;
    iio_buffer*       tx_buffer = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> had_error{false};
    std::mutex        error_mutex;
    std::string       error_message;
    std::thread       tx_thread;
};

static uint32_t compute_sample_rate_hz(int64_t bandwidth_hz)
{
    const int64_t preferred_rate_hz = bandwidth_hz * 2;
    if (preferred_rate_hz < (int64_t)TX_MIN_SAMPLE_RATE_HZ)
        return TX_MIN_SAMPLE_RATE_HZ;
    if (preferred_rate_hz > (int64_t)TX_MAX_SAMPLE_RATE_HZ)
        return TX_MAX_SAMPLE_RATE_HZ;
    return (uint32_t)preferred_rate_hz;
}

static int16_t next_noise_sample(uint32_t& state)
{
    if (state == 0)
        state = 1U;

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (int16_t)state;
}

static void set_runtime_error(TxRuntime* runtime, const std::string& error)
{
    std::lock_guard<std::mutex> lock(runtime->error_mutex);
    runtime->error_message = error;
    runtime->had_error.store(true, std::memory_order_relaxed);
}

static std::string take_runtime_error(TxRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(runtime->error_mutex);
    return runtime->error_message;
}

static std::string errno_message(const char* prefix, int error_code)
{
    if (error_code != 0)
        return std::string(prefix) + ": errno=" + std::to_string(error_code) + " (" + strerror(error_code) + ")";

    return std::string(prefix) + " (errno was not set)";
}

static iio_context* create_iio_context_auto(std::string& error)
{
    iio_scan_context* scan_context = iio_create_scan_context(nullptr, 0);
    if (!scan_context)
    {
        error = errno_message("failed to create IIO scan context", errno);
        return nullptr;
    }

    iio_context_info** context_info = nullptr;
    const ssize_t context_count = iio_scan_context_get_info_list(scan_context, &context_info);

    iio_context* context = nullptr;

    if (context_count < 0)
    {
        error = "failed to scan IIO contexts (error code: " + std::to_string(context_count) + ")";
    }
    else if (context_count == 1)
    {
        const char* uri = iio_context_info_get_uri(context_info[0]);
        if (uri)
            context = iio_create_context_from_uri(uri);

        if (!context)
            error = errno_message("failed to create IIO context from scanned URI", errno);
    }
    else if (context_count > 1)
    {
        error = std::to_string(context_count) + " IIO contexts detected, skipping auto-selection";
    }

    if (context_count >= 0 && context_info)
        iio_context_info_list_free(context_info);
    iio_scan_context_destroy(scan_context);

    if (context)
        return context;

    context = iio_create_default_context();
    if (!context)
        error = errno_message("failed to create default IIO context", errno);

    return context;
}

static bool configure_tx(TxRuntime* runtime, uint64_t freq_hz, int64_t bandwidth_hz, int power_db, std::string& error)
{
    runtime->context = create_iio_context_auto(error);
    if (!runtime->context)
        return false;

    iio_device* phy = iio_context_find_device(runtime->context, "ad9361-phy");
    runtime->phy = phy;
    runtime->tx_dev = iio_context_find_device(runtime->context, "cf-ad9361-dds-core-lpc");
    if (!phy || !runtime->tx_dev)
    {
        error = "failed to find AD9361 devices in IIO context";
        return false;
    }

    runtime->phy_tx = iio_device_find_channel(phy, "voltage0", true);
    runtime->lo_tx = iio_device_find_channel(phy, "altvoltage1", true);
    if (!runtime->phy_tx || !runtime->lo_tx)
    {
        error = "failed to locate TX configuration channels";
        return false;
    }

    runtime->tx_i = iio_device_find_channel(runtime->tx_dev, "voltage0", true);
    runtime->tx_q = iio_device_find_channel(runtime->tx_dev, "voltage1", true);
    if (!runtime->tx_i || !runtime->tx_q)
    {
        error = "failed to locate TX streaming channels";
        return false;
    }

    int result = ad9361_set_bb_rate(phy, compute_sample_rate_hz(bandwidth_hz));
    if (result < 0)
    {
        error = "ad9361_set_bb_rate() failed: " + std::to_string(result);
        return false;
    }

    result = iio_channel_attr_write_longlong(runtime->phy_tx, "rf_bandwidth", bandwidth_hz);
    if (result < 0)
    {
        error = "setting rf_bandwidth failed: " + std::to_string(result);
        return false;
    }

    result = iio_channel_attr_write_double(runtime->phy_tx, "hardwaregain", (double)power_db);
    if (result < 0)
    {
        error = "setting hardwaregain failed: " + std::to_string(result);
        return false;
    }

    result = iio_channel_attr_write_longlong(runtime->lo_tx, "frequency", (long long)freq_hz);
    if (result < 0)
    {
        error = "setting frequency failed: " + std::to_string(result);
        return false;
    }

    iio_channel_enable(runtime->tx_i);
    iio_channel_enable(runtime->tx_q);

    runtime->tx_buffer = iio_device_create_buffer(runtime->tx_dev, TX_BUFFER_SAMPLES, false);
    if (!runtime->tx_buffer)
    {
        error = "failed to create TX buffer";
        return false;
    }

    return true;
}

static void transmit_loop(TxRuntime* runtime)
{
    uint32_t noise_rng_state = (uint32_t)time(nullptr) ^ ((uint32_t)clock() << 1);

    while (runtime->running.load(std::memory_order_relaxed))
    {
        const ptrdiff_t step = iio_buffer_step(runtime->tx_buffer);
        char* sample_ptr = (char*)iio_buffer_first(runtime->tx_buffer, runtime->tx_i);
        char* end = (char*)iio_buffer_end(runtime->tx_buffer);

        while (sample_ptr < end)
        {
            int16_t* i_sample = (int16_t*)sample_ptr;
            int16_t* q_sample = i_sample + 1;

            *i_sample = next_noise_sample(noise_rng_state);
            *q_sample = next_noise_sample(noise_rng_state);

            sample_ptr += step;
        }

        const ssize_t push_result = iio_buffer_push(runtime->tx_buffer);
        if (push_result < 0)
        {
            set_runtime_error(runtime, "iio_buffer_push() failed: " + std::to_string(push_result));
            runtime->running.store(false, std::memory_order_relaxed);
            return;
        }
    }
}

bool start_transmit(TxRuntime** runtime,
                    float freq_mhz, int bw_mhz, int power_db,
                    std::string& error)
{
    if (!runtime)
    {
        error = "Invalid runtime pointer";
        return false;
    }

    const uint64_t freq_hz = (uint64_t)(freq_mhz * 1e6f);
    const int64_t bandwidth_hz = (int64_t)bw_mhz * 1000000LL;

    if (freq_hz < MIN_FREQUENCY_HZ || freq_hz > MAX_FREQUENCY_HZ)
    {
        error = "frequency must be between 1 and 6000 MHz";
        return false;
    }

    if (bandwidth_hz < MIN_BANDWIDTH_HZ || bandwidth_hz > MAX_BANDWIDTH_HZ)
    {
        error = "bandwidth must be between 1 and 100 MHz";
        return false;
    }

    TxRuntime* tx_runtime = new TxRuntime();

    if (!configure_tx(tx_runtime, freq_hz, bandwidth_hz, power_db, error))
    {
        stop_transmit(&tx_runtime);
        return false;
    }

    tx_runtime->running.store(true, std::memory_order_relaxed);
    tx_runtime->tx_thread = std::thread(transmit_loop, tx_runtime);

    *runtime = tx_runtime;
    return true;
}

void stop_transmit(TxRuntime** runtime)
{
    if (!runtime || !*runtime)
        return;

    TxRuntime* tx_runtime = *runtime;

    tx_runtime->running.store(false, std::memory_order_relaxed);

    if (tx_runtime->tx_thread.joinable())
        tx_runtime->tx_thread.join();

    if (tx_runtime->tx_buffer)
        iio_buffer_destroy(tx_runtime->tx_buffer);
    if (tx_runtime->tx_i)
        iio_channel_disable(tx_runtime->tx_i);
    if (tx_runtime->tx_q)
        iio_channel_disable(tx_runtime->tx_q);
    if (tx_runtime->context)
        iio_context_destroy(tx_runtime->context);

    delete tx_runtime;
    *runtime = nullptr;
}

void draw_ui_content(AppState& state)
{
    if (state.transmitting && state.runtime && !state.runtime->running.load(std::memory_order_relaxed))
    {
        std::string runtime_error;
        bool has_error = state.runtime->had_error.load(std::memory_order_relaxed);
        if (has_error)
            runtime_error = take_runtime_error(state.runtime);

        stop_transmit(&state.runtime);
        state.transmitting = false;
        state.status_msg = has_error ? runtime_error : "Transmission stopped unexpectedly";
        state.status_error = has_error;
    }

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
    ImGui::Text("LibreSDR Noise Generator");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float label_col = 180.0f;
    const float input_col = 110.0f;
    const float right_pad = ImGui::GetStyle().WindowPadding.x;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Frequency (MHz)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    bool freq_changed = ImGui::InputFloat("##freq_in", &state.freq_mhz, 0.1f, 1.0f, "%.3f");
    if (freq_changed)
        state.freq_mhz = std::clamp(state.freq_mhz, 1.0f, 6000.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-right_pad);
    freq_changed |= ImGui::SliderFloat("##freq_sl", &state.freq_mhz, 1.0f, 6000.0f, "");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bandwidth (MHz)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    bool bw_changed = ImGui::InputInt("##bw_in", &state.bw_mhz, 1, 5);
    if (bw_changed)
        state.bw_mhz = std::clamp(state.bw_mhz, 1, 100);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-right_pad);
    bw_changed |= ImGui::SliderInt("##bw_sl", &state.bw_mhz, 1, 100, "");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("TX Hardware Gain (dB)");
    ImGui::SameLine(label_col);
    ImGui::SetNextItemWidth(input_col);
    bool power_changed = ImGui::InputInt("##pwr_in", &state.power_db, 1, 5);
    if (power_changed)
        state.power_db = std::clamp(state.power_db, MIN_POWER_DB, MAX_POWER_DB);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-right_pad);
    power_changed |= ImGui::SliderInt("##pwr_sl", &state.power_db, MIN_POWER_DB, MAX_POWER_DB, "");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (state.transmitting && state.runtime)
    {
        if (freq_changed && state.freq_mhz != state.applied_freq_mhz)
        {
            const uint64_t freq_hz = (uint64_t)(state.freq_mhz * 1e6f);
            const int result = iio_channel_attr_write_longlong(state.runtime->lo_tx, "frequency", (long long)freq_hz);
            if (result >= 0)
            {
                state.applied_freq_mhz = state.freq_mhz;
                state.status_msg = "Transmitting";
                state.status_error = false;
            }
            else
            {
                state.freq_mhz = state.applied_freq_mhz;
                state.status_msg = "Failed to set frequency: " + std::to_string(result);
                state.status_error = true;
            }
        }

        if (bw_changed && state.bw_mhz != state.applied_bw_mhz)
        {
            const int64_t bandwidth_hz = (int64_t)state.bw_mhz * 1000000LL;
            constexpr int INVALID_RESULT = -EINVAL;
            int bandwidth_result = INVALID_RESULT;
            if (state.runtime->phy)
            {
                bandwidth_result = ad9361_set_bb_rate(state.runtime->phy, compute_sample_rate_hz(bandwidth_hz));
                if (bandwidth_result >= 0)
                {
                    bandwidth_result = iio_channel_attr_write_longlong(state.runtime->phy_tx, "rf_bandwidth", bandwidth_hz);
                }
            }
            if (bandwidth_result >= 0)
            {
                state.applied_bw_mhz = state.bw_mhz;
                state.status_msg = "Transmitting";
                state.status_error = false;
            }
            else
            {
                state.bw_mhz = state.applied_bw_mhz;
                state.status_msg = "Failed to set bandwidth: " + std::to_string(bandwidth_result);
                state.status_error = true;
            }
        }

        if (power_changed && state.power_db != state.applied_power_db)
        {
            const int result = iio_channel_attr_write_double(state.runtime->phy_tx, "hardwaregain", (double)state.power_db);
            if (result >= 0)
            {
                state.applied_power_db = state.power_db;
                state.status_msg = "Transmitting";
                state.status_error = false;
            }
            else
            {
                state.power_db = state.applied_power_db;
                state.status_msg = "Failed to set gain: " + std::to_string(result);
                state.status_error = true;
            }
        }
    }

    const float btn_width = ImGui::GetContentRegionAvail().x;

    if (!state.transmitting)
    {
        if (ImGui::Button("Start Transmitting", ImVec2(btn_width, 0)))
        {
            std::string error;
            if (start_transmit(&state.runtime, state.freq_mhz, state.bw_mhz, state.power_db, error))
            {
                state.transmitting = true;
                state.applied_freq_mhz = state.freq_mhz;
                state.applied_bw_mhz = state.bw_mhz;
                state.applied_power_db = state.power_db;
                state.status_msg = "Transmitting";
                state.status_error = false;
            }
            else
            {
                state.status_msg = error;
                state.status_error = true;
            }
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button("Stop Transmitting", ImVec2(btn_width, 0)))
        {
            stop_transmit(&state.runtime);
            state.transmitting = false;
            state.status_msg = "Idle";
            state.status_error = false;
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();

    if (state.status_error)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", state.status_msg.c_str());
    else if (state.transmitting)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Status: %s", state.status_msg.c_str());
    else
        ImGui::TextDisabled("Status: %s", state.status_msg.c_str());

    ImGui::End();
}
