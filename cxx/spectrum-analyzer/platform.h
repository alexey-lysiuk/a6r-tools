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

#include <cstdint>

#include "imgui.h"

// Platform specific texture helpers, implemented by each backend (main.cpp, main_macos.mm).
// Textures are RGBA8 with bilinear filtering.

ImTextureID platform_create_texture(int width, int height);
void        platform_update_texture(ImTextureID texture, const uint32_t* rgba, int width, int height);
void        platform_destroy_texture(ImTextureID texture);
