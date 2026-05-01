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

#include <string>

#include <hackrf.h>

struct AppState
{
    float          freq_mhz         = 100.0f;
    float          bw_mhz           = 10.0f;
    int            power_db         = 20;
    float          applied_freq_mhz = 100.0f;
    float          applied_bw_mhz   = 10.0f;
    int            applied_power_db = 20;
    bool           transmitting     = false;
    hackrf_device* device           = nullptr;
    std::string    status_msg       = "Idle";
    bool           status_error     = false;
};

bool start_transmit(hackrf_device** device,
                    float freq_mhz, float bw_mhz, int power_db,
                    std::string& error);
void stop_transmit(hackrf_device** device);

// Renders all ImGui widgets for one frame and updates state accordingly.
// Must be called between ImGui::NewFrame() and ImGui::Render().
void draw_ui_content(AppState& state);
