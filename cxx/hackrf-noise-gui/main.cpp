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
#include <cstdlib>
#include <ctime>
#include <string>

#include <hackrf.h>

#include <SDL.h>
#include <SDL_opengl.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl2.h"

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

static bool start_transmit(hackrf_device** device,
                            float freq_mhz, float bw_mhz, int power_db,
                            std::string& error)
{
    int result = hackrf_init();
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_init failed: ") + hackrf_error_name((hackrf_error)result);
        return false;
    }

    result = hackrf_open(device);
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_open failed: ") + hackrf_error_name((hackrf_error)result);
        hackrf_exit();
        return false;
    }

    const uint32_t sample_rate = (uint32_t)(bw_mhz * 1e6f);
    result = hackrf_set_sample_rate(*device, sample_rate);
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_set_sample_rate failed: ") + hackrf_error_name((hackrf_error)result);
        hackrf_close(*device);
        hackrf_exit();
        *device = nullptr;
        return false;
    }

    const uint32_t filter_bw = hackrf_compute_baseband_filter_bw(sample_rate);
    result = hackrf_set_baseband_filter_bandwidth(*device, filter_bw);
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_set_baseband_filter_bandwidth failed: ") + hackrf_error_name((hackrf_error)result);
        hackrf_close(*device);
        hackrf_exit();
        *device = nullptr;
        return false;
    }

    const uint64_t freq_hz = (uint64_t)(freq_mhz * 1e6f);
    result = hackrf_set_freq(*device, freq_hz);
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_set_freq failed: ") + hackrf_error_name((hackrf_error)result);
        hackrf_close(*device);
        hackrf_exit();
        *device = nullptr;
        return false;
    }

    result = hackrf_set_txvga_gain(*device, (uint32_t)power_db);
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_set_txvga_gain failed: ") + hackrf_error_name((hackrf_error)result);
        hackrf_close(*device);
        hackrf_exit();
        *device = nullptr;
        return false;
    }

    g_tx_running.store(true, std::memory_order_relaxed);

    result = hackrf_start_tx(*device, tx_callback, nullptr);
    if (result != HACKRF_SUCCESS)
    {
        error = std::string("hackrf_start_tx failed: ") + hackrf_error_name((hackrf_error)result);
        g_tx_running.store(false, std::memory_order_relaxed);
        hackrf_close(*device);
        hackrf_exit();
        *device = nullptr;
        return false;
    }

    return true;
}

static void stop_transmit(hackrf_device** device)
{
    if (!*device)
        return;

    g_tx_running.store(false, std::memory_order_relaxed);
    hackrf_stop_tx(*device);
    hackrf_close(*device);
    hackrf_exit();
    *device = nullptr;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    const SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow(
        "HackRF Noise Generator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        540, 230, window_flags);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowMinimumSize(window, 400, 200);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL2_Init();

    // Current settings
    float freq_mhz = 100.0f;
    float bw_mhz   = 10.0f;
    int   power_db = 20;

    // Last successfully applied settings (for on-the-fly change detection)
    float applied_freq_mhz = freq_mhz;
    float applied_bw_mhz   = bw_mhz;
    int   applied_power_db = power_db;

    bool transmitting = false;
    hackrf_device* device = nullptr;
    std::string status_msg = "Idle";
    bool status_error = false;

    bool done = false;

    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Detect unexpected TX stop
        if (transmitting && device && hackrf_is_streaming(device) != HACKRF_TRUE)
        {
            stop_transmit(&device);
            transmitting = false;
            status_msg = "Transmission stopped unexpectedly";
            status_error = true;
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

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
        bool freq_changed = ImGui::InputFloat("##freq_in", &freq_mhz, 0.1f, 1.0f, "%.3f");
        if (freq_changed)
            freq_mhz = std::clamp(freq_mhz, MIN_FREQUENCY_MHZ, MAX_FREQUENCY_MHZ);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-right_pad);
        freq_changed |= ImGui::SliderFloat("##freq_sl", &freq_mhz,
                                           MIN_FREQUENCY_MHZ, MAX_FREQUENCY_MHZ, "");

        // ── Bandwidth ──────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Bandwidth (MHz)");
        ImGui::SameLine(label_col);
        ImGui::SetNextItemWidth(input_col);
        bool bw_changed = ImGui::InputFloat("##bw_in", &bw_mhz, 0.5f, 1.0f, "%.1f");
        if (bw_changed)
            bw_mhz = std::clamp(bw_mhz, MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-right_pad);
        bw_changed |= ImGui::SliderFloat("##bw_sl", &bw_mhz,
                                         MIN_BANDWIDTH_MHZ, MAX_BANDWIDTH_MHZ, "");

        // ── TX VGA Gain ────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::Text("TX VGA Gain (dB)");
        ImGui::SameLine(label_col);
        ImGui::SetNextItemWidth(input_col);
        bool power_changed = ImGui::InputInt("##pwr_in", &power_db, 1, 5);
        if (power_changed)
            power_db = std::clamp(power_db, MIN_VGA_GAIN, MAX_VGA_GAIN);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-right_pad);
        power_changed |= ImGui::SliderInt("##pwr_sl", &power_db,
                                          MIN_VGA_GAIN, MAX_VGA_GAIN, "");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── On-the-fly parameter updates ───────────────────────────────────
        if (transmitting && device)
        {
            if (freq_changed && freq_mhz != applied_freq_mhz)
            {
                const uint64_t freq_hz = (uint64_t)(freq_mhz * 1e6f);
                const int result = hackrf_set_freq(device, freq_hz);
                if (result == HACKRF_SUCCESS)
                {
                    applied_freq_mhz = freq_mhz;
                    status_msg = "Transmitting";
                    status_error = false;
                }
                else
                {
                    freq_mhz = applied_freq_mhz;
                    status_msg = std::string("Failed to set frequency: ") + hackrf_error_name((hackrf_error)result);
                    status_error = true;
                }
            }

            if (bw_changed && bw_mhz != applied_bw_mhz)
            {
                const uint32_t sample_rate = (uint32_t)(bw_mhz * 1e6f);
                int result = hackrf_set_sample_rate(device, sample_rate);
                if (result == HACKRF_SUCCESS)
                {
                    const uint32_t filter_bw = hackrf_compute_baseband_filter_bw(sample_rate);
                    result = hackrf_set_baseband_filter_bandwidth(device, filter_bw);
                }
                if (result == HACKRF_SUCCESS)
                {
                    applied_bw_mhz = bw_mhz;
                    status_msg = "Transmitting";
                    status_error = false;
                }
                else
                {
                    bw_mhz = applied_bw_mhz;
                    status_msg = std::string("Failed to set bandwidth: ") + hackrf_error_name((hackrf_error)result);
                    status_error = true;
                }
            }

            if (power_changed && power_db != applied_power_db)
            {
                const int result = hackrf_set_txvga_gain(device, (uint32_t)power_db);
                if (result == HACKRF_SUCCESS)
                {
                    applied_power_db = power_db;
                    status_msg = "Transmitting";
                    status_error = false;
                }
                else
                {
                    power_db = applied_power_db;
                    status_msg = std::string("Failed to set gain: ") + hackrf_error_name((hackrf_error)result);
                    status_error = true;
                }
            }
        }

        // ── Start / Stop button ────────────────────────────────────────────
        const float btn_width = ImGui::GetContentRegionAvail().x;

        if (!transmitting)
        {
            if (ImGui::Button("Start Transmitting", ImVec2(btn_width, 0)))
            {
                std::string error;
                if (start_transmit(&device, freq_mhz, bw_mhz, power_db, error))
                {
                    transmitting      = true;
                    applied_freq_mhz  = freq_mhz;
                    applied_bw_mhz    = bw_mhz;
                    applied_power_db  = power_db;
                    status_msg        = "Transmitting";
                    status_error      = false;
                }
                else
                {
                    status_msg   = error;
                    status_error = true;
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
                stop_transmit(&device);
                transmitting = false;
                status_msg   = "Idle";
                status_error = false;
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::Spacing();

        // ── Status line ────────────────────────────────────────────────────
        if (status_error)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", status_msg.c_str());
        else if (transmitting)
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Status: %s", status_msg.c_str());
        else
            ImGui::TextDisabled("Status: %s", status_msg.c_str());

        ImGui::End();

        // ── Render ─────────────────────────────────────────────────────────
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    stop_transmit(&device);

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
