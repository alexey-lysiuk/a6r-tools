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

#include "analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "platform.h"

namespace
{

constexpr int GRID_DIVISIONS_X = 10;
constexpr int GRID_DIVISIONS_Y = 10;

constexpr float PLOT_MARGIN_LEFT   = 58.0f;
constexpr float PLOT_MARGIN_RIGHT  = 10.0f;
constexpr float PLOT_MARGIN_TOP    = 8.0f;
constexpr float PLOT_MARGIN_BOTTOM = 22.0f;

ImU32 trace_color(TraceMode mode)
{
    switch (mode)
    {
    case TraceMode::Live:     return IM_COL32(255, 220, 0, 255);
    case TraceMode::MaxHold:  return IM_COL32(255, 80, 80, 255);
    case TraceMode::MinHold:  return IM_COL32(80, 220, 255, 255);
    case TraceMode::MaxDecay: return IM_COL32(90, 255, 120, 255);
    case TraceMode::Average:  return IM_COL32(255, 120, 255, 255);
    default:                  return IM_COL32(255, 255, 255, 255);
    }
}

std::string format_frequency(double hz, int digits = 3)
{
    char buf[64];
    const double a = std::fabs(hz);

    if (a >= 1e9)
        snprintf(buf, sizeof(buf), "%.*f GHz", digits + 3, hz / 1e9);
    else if (a >= 1e6)
        snprintf(buf, sizeof(buf), "%.*f MHz", digits, hz / 1e6);
    else if (a >= 1e3)
        snprintf(buf, sizeof(buf), "%.*f kHz", digits, hz / 1e3);
    else
        snprintf(buf, sizeof(buf), "%.0f Hz", hz);

    return buf;
}

// Shrinks or stretches data to exactly `columns` values; shrinking keeps
// peaks (or dips for min hold traces) so narrow signals do not disappear
void resample(const std::vector<float>& src, int columns, bool use_min, std::vector<float>& dst)
{
    dst.resize(columns);

    const size_t n = src.size();
    if (n == 0)
        return;

    for (int x = 0; x < columns; ++x)
    {
        if (n >= static_cast<size_t>(columns))
        {
            const size_t first = static_cast<size_t>(x) * n / columns;
            const size_t last  = std::max(first + 1, static_cast<size_t>(x + 1) * n / columns);

            float value = src[first];
            for (size_t i = first + 1; i < last; ++i)
                value = use_min ? std::min(value, src[i]) : std::max(value, src[i]);
            dst[x] = value;
        }
        else
        {
            const size_t i = std::min(n - 1, static_cast<size_t>((x + 0.5) * n / columns));
            dst[x] = src[i];
        }
    }
}

ImU32 waterfall_color(float t)
{
    static const float stops[][3] = {
        { 0.00f, 0.00f, 0.12f },
        { 0.00f, 0.00f, 1.00f },
        { 0.00f, 1.00f, 1.00f },
        { 1.00f, 1.00f, 0.00f },
        { 1.00f, 0.00f, 0.00f },
    };
    constexpr int count = sizeof(stops) / sizeof(stops[0]);

    t = std::min(std::max(t, 0.0f), 1.0f) * (count - 1);
    const int   i = std::min(static_cast<int>(t), count - 2);
    const float f = t - i;

    auto lerp = [&](int c) { return stops[i][c] + (stops[i + 1][c] - stops[i][c]) * f; };
    return IM_COL32(static_cast<int>(lerp(0) * 255), static_cast<int>(lerp(1) * 255),
                    static_cast<int>(lerp(2) * 255), 255);
}

} // unnamed namespace

Analyzer::Analyzer()
{
    traces_[0].mode = TraceMode::Live;
    traces_[1].mode = TraceMode::MaxHold;

    waterfall_pixels_.assign(WATERFALL_WIDTH * WATERFALL_HEIGHT, IM_COL32(0, 0, 0, 255));

    refresh_devices();
    sync_frequency_fields();
    status_ = "Select a device and press Connect";
}

Analyzer::~Analyzer()
{
    shutdown();
}

void Analyzer::shutdown()
{
    disconnect();

    if (waterfall_texture_)
    {
        platform_destroy_texture(waterfall_texture_);
        waterfall_texture_ = 0;
    }
}

// ───────────────────────── Device handling ─────────────────────────

void Analyzer::refresh_devices()
{
    std::string previous;
    if (entry_index_ >= 0 && entry_index_ < static_cast<int>(entries_.size()))
        previous = entries_[entry_index_].driver + ":" + entries_[entry_index_].id;

    entries_ = enumerate_devices();
    entry_index_ = 0;

    for (size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].driver + ":" + entries_[i].id == previous)
            entry_index_ = static_cast<int>(i);
    }
}

void Analyzer::connect()
{
    disconnect();

    if (entry_index_ < 0 || entry_index_ >= static_cast<int>(entries_.size()))
    {
        status_ = "No device selected";
        status_error_ = true;
        return;
    }

    std::unique_ptr<SpectrumDevice> device = create_device(entries_[entry_index_]);
    std::string error;

    if (!device || !device->open(error))
    {
        status_ = error.empty() ? "Cannot create device" : error;
        status_error_ = true;
        return;
    }

    device_ = std::move(device);

    const DeviceInfo& info = device_->info();
    start_hz_ = std::min(std::max(start_hz_, info.min_hz), info.max_hz);
    stop_hz_  = std::min(std::max(stop_hz_, info.min_hz), info.max_hz);
    if (stop_hz_ <= start_hz_)
    {
        start_hz_ = info.min_hz;
        stop_hz_  = std::min(info.max_hz, info.min_hz + 1e6);
    }
    points_ = std::min(std::max(points_, info.min_points), info.max_points);
    sync_frequency_fields();

    if (restart())
    {
        status_ = "Connected to " + info.model;
        status_error_ = false;
    }
}

void Analyzer::disconnect()
{
    if (device_)
    {
        device_->close();
        device_.reset();
        status_ = "Disconnected";
        status_error_ = false;
    }
}

bool Analyzer::restart(bool keep_data)
{
    if (!device_)
        return false;

    device_->stop();

    if (keep_data)
        last_sweep_time_ = ImGui::GetTime();   // do not count the pause as decay time
    else
        reset_data();

    SweepConfig config;
    config.start_hz = start_hz_;
    config.stop_hz  = stop_hz_;
    config.points   = points_;

    std::string error;
    if (!device_->start(config, error))
    {
        status_ = error;
        status_error_ = true;
        return false;
    }

    return true;
}

void Analyzer::reset_data()
{
    for (Trace& t : traces_)
        t.reset();

    have_sweep_ = false;
    last_sweep_ = Sweep();
    waterfall_rows_.clear();
    waterfall_dirty_ = true;
    sweep_rate_ = 0.0f;
    sweep_counter_ = 0;
    fps_count_ = 0;
    fps_time_ = ImGui::GetTime();
    last_sweep_time_ = fps_time_;
}

void Analyzer::poll_device()
{
    if (!device_)
        return;

    const std::string error = device_->take_error();
    if (!error.empty())
    {
        status_ = error;
        status_error_ = true;
    }

    Sweep sweep;
    int processed = 0;
    while (processed < 64 && device_->pop_sweep(sweep))
    {
        process_sweep(sweep);
        ++processed;
    }

    const double now = ImGui::GetTime();
    if (now - fps_time_ >= 1.0)
    {
        sweep_rate_ = static_cast<float>(fps_count_ / (now - fps_time_));
        fps_count_ = 0;
        fps_time_ = now;
    }

    if (auto_tune_ && have_sweep_ && now - last_auto_tune_ > 0.5)
    {
        auto_tune();
        last_auto_tune_ = now;
    }
}

void Analyzer::process_sweep(const Sweep& sweep)
{
    if (sweep.level.empty())
        return;

    const double now = ImGui::GetTime();
    const double dt  = std::max(now - last_sweep_time_, 0.0);
    last_sweep_time_ = now;

    const bool changed = !have_sweep_
        || sweep.level.size() != last_sweep_.level.size()
        || sweep.start_hz != last_sweep_.start_hz
        || sweep.stop_hz != last_sweep_.stop_hz;

    if (changed)
    {
        for (Trace& t : traces_)
            t.reset();
        waterfall_rows_.clear();
    }

    for (Trace& t : traces_)
        t.update(sweep.level, dt);

    last_sweep_ = sweep;
    have_sweep_ = true;
    ++fps_count_;
    ++sweep_counter_;

    std::vector<float> row;
    resample(sweep.level, WATERFALL_WIDTH, false, row);
    waterfall_rows_.push_front(std::move(row));
    if (waterfall_rows_.size() > WATERFALL_HEIGHT)
        waterfall_rows_.pop_back();
    waterfall_dirty_ = true;
}

// ───────────────────────── Frequency ─────────────────────────

void Analyzer::sync_frequency_fields()
{
    if (freq_mode_ == 0)
    {
        field_a_ = start_hz_ / 1e6;
        field_b_ = stop_hz_ / 1e6;
    }
    else
    {
        field_a_ = (start_hz_ + stop_hz_) / 2e6;
        field_b_ = (stop_hz_ - start_hz_) / 1e6;
    }
}

void Analyzer::apply_frequency()
{
    double start, stop;

    if (freq_mode_ == 0)
    {
        start = field_a_ * 1e6;
        stop  = field_b_ * 1e6;
    }
    else
    {
        start = (field_a_ - field_b_ / 2.0) * 1e6;
        stop  = (field_a_ + field_b_ / 2.0) * 1e6;
    }

    if (device_)
    {
        const DeviceInfo& info = device_->info();
        start = std::max(start, info.min_hz);
        stop  = std::min(stop, info.max_hz);
    }

    if (stop - start < 1.0)
    {
        status_ = "Stop frequency must be greater than start frequency";
        status_error_ = true;
        sync_frequency_fields();
        return;
    }

    start_hz_ = start;
    stop_hz_  = stop;
    sync_frequency_fields();

    if (device_ && restart())
        status_error_ = false;
}

// ───────────────────────── Amplitude ─────────────────────────

void Analyzer::auto_tune()
{
    float hi = -1e9f;
    float lo = 1e9f;

    for (const Trace& t : traces_)
    {
        if (t.mode == TraceMode::Off)
            continue;

        for (float v : t.data)
        {
            if (!std::isfinite(v))
                continue;
            hi = std::max(hi, v);
            lo = std::min(lo, v);
        }
    }

    if (hi < lo)
        return;

    static const float scales[] = { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f };
    constexpr int divisions = GRID_DIVISIONS_Y;

    const float span = std::max(hi - lo, 1.0f);
    const float margin = 0.05f * span + 1.0f;

    float scale = scales[sizeof(scales) / sizeof(scales[0]) - 1];
    float ref = 0.0f;

    for (float candidate : scales)
    {
        // Round reference level up to a whole division
        const float r = std::ceil((hi + margin) / candidate) * candidate;
        if (r - candidate * divisions <= lo - margin * 0.5f || candidate == scales[sizeof(scales) / sizeof(scales[0]) - 1])
        {
            scale = candidate;
            ref = r;
            break;
        }
    }

    ref_level_ = ref;
    scale_ = scale;
}

// ───────────────────────── UI: controls ─────────────────────────

void Analyzer::draw()
{
    poll_device();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Spectrum Analyzer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float panel_width = 330.0f;

    ImGui::BeginChild("controls", ImVec2(panel_width, 0), ImGuiChildFlags_Borders);
    draw_controls();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("display", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        const ImVec2 pos  = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();

        if (show_waterfall_)
        {
            const float spectrum_height = std::floor(size.y * waterfall_split_);
            draw_spectrum(pos, ImVec2(size.x, spectrum_height));
            draw_waterfall(ImVec2(pos.x, pos.y + spectrum_height), ImVec2(size.x, size.y - spectrum_height));
        }
        else
        {
            draw_spectrum(pos, size);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void Analyzer::draw_controls()
{
    draw_device_section();

    if (ImGui::CollapsingHeader("Frequency", ImGuiTreeNodeFlags_DefaultOpen))
        draw_frequency_section();

    if (ImGui::CollapsingHeader("Amplitude", ImGuiTreeNodeFlags_DefaultOpen))
        draw_amplitude_section();

    if (ImGui::CollapsingHeader("Traces", ImGuiTreeNodeFlags_DefaultOpen))
        draw_traces_section();

    if (ImGui::CollapsingHeader("Waterfall", ImGuiTreeNodeFlags_DefaultOpen))
        draw_waterfall_section();

    if (device_ && !device_->settings().empty()
        && ImGui::CollapsingHeader("Device settings", ImGuiTreeNodeFlags_DefaultOpen))
        draw_device_settings();
}

void Analyzer::draw_device_section()
{
    ImGui::SeparatorText("Device");

    const char* preview = entries_.empty() ? "(no devices found)" : entries_[entry_index_].label.c_str();

    ImGui::BeginDisabled(device_ != nullptr);
    ImGui::SetNextItemWidth(-60.0f);
    if (ImGui::BeginCombo("##device", preview))
    {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        {
            if (ImGui::Selectable(entries_[i].label.c_str(), i == entry_index_))
                entry_index_ = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan"))
        refresh_devices();
    ImGui::EndDisabled();

    if (!device_)
    {
        ImGui::BeginDisabled(entries_.empty());
        if (ImGui::Button("Connect", ImVec2(-1, 0)))
            connect();
        ImGui::EndDisabled();
    }
    else
    {
        const bool running = device_->running();

        if (ImGui::Button(running ? "Pause" : "Resume", ImVec2(150, 0)))
        {
            if (running)
            {
                device_->stop();
            }
            else if (restart(true))
            {
                status_error_ = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", ImVec2(-1, 0)))
            disconnect();
    }

    if (status_error_)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", status_.c_str());
    if (status_error_)
        ImGui::PopStyleColor();

    if (device_ && device_->running())
        ImGui::TextDisabled("%.1f sweeps/s", sweep_rate_);
}

void Analyzer::draw_frequency_section()
{
    const char* const labels[2][2] = { { "Start", "Stop" }, { "Center", "Span" } };

    if (ImGui::RadioButton("Start/Stop", freq_mode_ == 0))
    {
        freq_mode_ = 0;
        sync_frequency_fields();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Center/Span", freq_mode_ == 1))
    {
        freq_mode_ = 1;
        sync_frequency_fields();
    }

    bool apply = false;

    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputDouble(labels[freq_mode_][0], &field_a_, 0.0, 0.0, "%.6f");
    apply |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted("MHz");

    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputDouble(labels[freq_mode_][1], &field_b_, 0.0, 0.0, "%.6f");
    apply |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted("MHz");

    if (device_ && device_->info().points_configurable)
    {
        const DeviceInfo& info = device_->info();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputInt("Points", &points_, 10, 100);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            points_ = std::min(std::max(points_, info.min_points), info.max_points);
            apply = true;
        }
    }

    if (apply)
        apply_frequency();

    if (device_)
    {
        const DeviceInfo& info = device_->info();
        ImGui::TextDisabled("Range: %s - %s", format_frequency(info.min_hz).c_str(),
                            format_frequency(info.max_hz).c_str());
    }
}

void Analyzer::draw_amplitude_section()
{
    const char* unit = device_ ? device_->info().level_unit.c_str() : "dBm";

    ImGui::Checkbox("Auto tune", &auto_tune_);
    ImGui::SameLine();
    if (ImGui::Button("Tune now"))
        auto_tune();

    ImGui::BeginDisabled(auto_tune_);

    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputFloat("Reference level", &ref_level_, 1.0f, 10.0f, "%.1f");
    ImGui::SameLine();
    ImGui::TextUnformatted(unit);

    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputFloat("Scale", &scale_, 1.0f, 5.0f, "%.1f");
    scale_ = std::min(std::max(scale_, 0.1f), 50.0f);
    ImGui::SameLine();
    ImGui::Text("dB/div");

    ImGui::EndDisabled();

    ImGui::TextDisabled("Bottom: %.1f %s", ref_level_ - scale_ * GRID_DIVISIONS_Y, unit);
}

void Analyzer::draw_traces_section()
{
    for (int i = 0; i < TRACE_COUNT; ++i)
    {
        Trace& t = traces_[i];

        ImGui::PushID(i);

        const ImU32 color = t.mode == TraceMode::Off ? IM_COL32(100, 100, 100, 255) : trace_color(t.mode);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Text("T%d", i + 1);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("##mode", trace_mode_name(t.mode)))
        {
            for (int m = 0; m < TRACE_MODE_COUNT; ++m)
            {
                const TraceMode mode = static_cast<TraceMode>(m);
                if (ImGui::Selectable(trace_mode_name(mode), mode == t.mode))
                {
                    t.mode = mode;
                    t.reset();
                }
            }
            ImGui::EndCombo();
        }

        if (t.mode == TraceMode::MaxHold || t.mode == TraceMode::MinHold)
        {
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
                t.reset();
        }
        else if (t.mode == TraceMode::MaxDecay)
        {
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat("Falloff time", &t.falloff_time_s, 0.5f, 60.0f, "%.1f s",
                               ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Time for a peak to fall by 10 dB");
        }
        else if (t.mode == TraceMode::Average)
        {
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderInt("Sweeps", &t.average_count, 2, 100);
        }

        ImGui::PopID();
    }

    if (ImGui::Button("Reset all"))
    {
        for (Trace& t : traces_)
            t.reset();
    }
}

void Analyzer::draw_waterfall_section()
{
    ImGui::Checkbox("Show waterfall", &show_waterfall_);

    if (show_waterfall_)
    {
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("Split", &waterfall_split_, 0.2f, 0.8f, "%.2f");
        if (ImGui::Button("Clear"))
        {
            waterfall_rows_.clear();
            waterfall_dirty_ = true;
        }
    }
}

void Analyzer::draw_device_settings()
{
    // Copy since set_setting() may modify the list
    const std::vector<DeviceSetting> settings = device_->settings();
    bool restart_needed = false;

    for (const DeviceSetting& s : settings)
    {
        ImGui::PushID(s.id.c_str());

        std::string label = s.label;
        if (!s.unit.empty() && s.type != DeviceSetting::Choice)
            label += " (" + s.unit + ")";
        label += "##" + s.id;

        bool changed = false;
        double value = s.value;

        ImGui::SetNextItemWidth(150.0f);

        switch (s.type)
        {
        case DeviceSetting::Bool:
        {
            bool b = s.value != 0.0;
            changed = ImGui::Checkbox(label.c_str(), &b);
            value = b ? 1.0 : 0.0;
            break;
        }

        case DeviceSetting::Int:
        {
            int v = static_cast<int>(s.value);
            changed = ImGui::SliderInt(label.c_str(), &v, static_cast<int>(s.min), static_cast<int>(s.max));
            value = v;
            break;
        }

        case DeviceSetting::Float:
        {
            float v = static_cast<float>(s.value);
            changed = ImGui::SliderFloat(label.c_str(), &v, static_cast<float>(s.min), static_cast<float>(s.max),
                                         "%.1f");
            value = v;
            break;
        }

        case DeviceSetting::Choice:
        {
            const int current = static_cast<int>(s.value);
            if (ImGui::BeginCombo(label.c_str(), s.choices[current].c_str()))
            {
                for (int i = 0; i < static_cast<int>(s.choices.size()); ++i)
                {
                    if (ImGui::Selectable(s.choices[i].c_str(), i == current))
                    {
                        value = i;
                        changed = i != current;
                    }
                }
                ImGui::EndCombo();
            }
            if (!s.unit.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(s.unit.c_str());
            }
            break;
        }
        }

        if (!s.tooltip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", s.tooltip.c_str());

        if (changed)
        {
            device_->set_setting(s.id, value);
            restart_needed |= s.requires_restart;
        }

        ImGui::PopID();
    }

    if (restart_needed && device_->running())
        restart();
}

// ───────────────────────── UI: plots ─────────────────────────

void Analyzer::draw_spectrum(const ImVec2& pos, const ImVec2& size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 p0(pos.x + PLOT_MARGIN_LEFT, pos.y + PLOT_MARGIN_TOP);
    const ImVec2 p1(pos.x + size.x - PLOT_MARGIN_RIGHT, pos.y + size.y - PLOT_MARGIN_BOTTOM);
    const float  w = p1.x - p0.x;
    const float  h = p1.y - p0.y;
    if (w < 50.0f || h < 50.0f)
        return;

    const ImU32 grid_color  = IM_COL32(70, 70, 70, 255);
    const ImU32 text_color  = IM_COL32(200, 200, 200, 255);
    const ImU32 frame_color = IM_COL32(150, 150, 150, 255);

    dl->AddRectFilled(p0, p1, IM_COL32(10, 10, 14, 255));

    const float bottom = ref_level_ - scale_ * GRID_DIVISIONS_Y;
    const char* unit   = device_ ? device_->info().level_unit.c_str() : "dBm";

    for (int i = 0; i <= GRID_DIVISIONS_Y; ++i)
    {
        const float y = p0.y + h * i / GRID_DIVISIONS_Y;
        dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), grid_color);

        char label[32];
        snprintf(label, sizeof(label), "%.1f", ref_level_ - scale_ * i);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p0.x - ts.x - 4.0f, y - ts.y / 2.0f), text_color, label);
    }

    dl->AddText(ImVec2(pos.x + 4.0f, pos.y + 2.0f), text_color, unit);

    const double f0 = have_sweep_ ? last_sweep_.start_hz : start_hz_;
    const double f1 = have_sweep_ ? last_sweep_.stop_hz : stop_hz_;

    float last_label_end = pos.x;

    for (int i = 0; i <= GRID_DIVISIONS_X; ++i)
    {
        const float x = p0.x + w * i / GRID_DIVISIONS_X;
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), grid_color);

        const std::string label = format_frequency(f0 + (f1 - f0) * i / GRID_DIVISIONS_X, 2);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        float tx = x - ts.x / 2.0f;
        tx = std::min(std::max(tx, pos.x), pos.x + size.x - ts.x);
        if (tx < last_label_end + 8.0f)
            continue;   // would overlap the previous label
        last_label_end = tx + ts.x;
        dl->AddText(ImVec2(tx, p1.y + 4.0f), text_color, label.c_str());
    }

    dl->PushClipRect(p0, p1, true);

    if (have_sweep_)
    {
        const int columns = std::max(2, static_cast<int>(w));
        std::vector<float> values;
        std::vector<ImVec2> points;

        for (const Trace& t : traces_)
        {
            if (t.mode == TraceMode::Off || t.data.size() < 2)
                continue;

            const size_t n = t.data.size();
            points.clear();

            if (n >= static_cast<size_t>(columns))
            {
                resample(t.data, columns, t.mode == TraceMode::MinHold, values);
                for (int x = 0; x < columns; ++x)
                {
                    const float y = p1.y - (values[x] - bottom) / (ref_level_ - bottom) * h;
                    points.emplace_back(p0.x + w * (x + 0.5f) / columns, std::min(std::max(y, p0.y - 2.0f), p1.y + 2.0f));
                }
            }
            else
            {
                for (size_t i = 0; i < n; ++i)
                {
                    const float y = p1.y - (t.data[i] - bottom) / (ref_level_ - bottom) * h;
                    points.emplace_back(p0.x + w * i / (n - 1), std::min(std::max(y, p0.y - 2.0f), p1.y + 2.0f));
                }
            }

            dl->AddPolyline(points.data(), static_cast<int>(points.size()), trace_color(t.mode),
                            ImDrawFlags_None, 1.5f);
        }
    }

    dl->PopClipRect();
    dl->AddRect(p0, p1, frame_color);

    // Cursor readout
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("spectrum", ImVec2(w, h));
    if (ImGui::IsItemHovered() && have_sweep_)
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float  t = std::min(std::max((m.x - p0.x) / w, 0.0f), 1.0f);
        const double f = f0 + (f1 - f0) * t;

        dl->AddLine(ImVec2(m.x, p0.y), ImVec2(m.x, p1.y), IM_COL32(255, 255, 255, 120));

        char text[128];
        int len = snprintf(text, sizeof(text), "%s", format_frequency(f, 4).c_str());

        for (const Trace& tr : traces_)
        {
            if (tr.mode == TraceMode::Off || tr.data.empty())
                continue;

            const size_t i = static_cast<size_t>(std::lround(t * (tr.data.size() - 1)));
            len += snprintf(text + len, sizeof(text) - len, "   %s: %.1f %s",
                            trace_mode_name(tr.mode), tr.data[i], unit);
            if (len >= static_cast<int>(sizeof(text)) - 32)
                break;
        }

        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 tp(std::max(p0.x + 6.0f, p1.x - ts.x - 6.0f), p0.y + 4.0f);
        dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 2), ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 2),
                          IM_COL32(0, 0, 0, 180));
        dl->AddText(tp, IM_COL32(255, 255, 255, 255), text);
    }
}

void Analyzer::update_waterfall_texture()
{
    if (!waterfall_texture_)
    {
        waterfall_texture_ = platform_create_texture(WATERFALL_WIDTH, WATERFALL_HEIGHT);
        waterfall_dirty_ = true;
    }

    const float ceil  = ref_level_;
    const float floor = ref_level_ - scale_ * GRID_DIVISIONS_Y;

    if (!waterfall_dirty_ && ceil == waterfall_ceil_ && floor == waterfall_floor_)
        return;

    waterfall_ceil_  = ceil;
    waterfall_floor_ = floor;
    waterfall_dirty_ = false;

    const float inv_range = 1.0f / std::max(ceil - floor, 0.1f);

    // Quantized palette keeps per-pixel work to a table lookup
    static ImU32 palette[256];
    static bool  palette_ready = false;
    if (!palette_ready)
    {
        for (int i = 0; i < 256; ++i)
            palette[i] = waterfall_color(i / 255.0f);
        palette_ready = true;
    }

    for (int y = 0; y < WATERFALL_HEIGHT; ++y)
    {
        uint32_t* dst = &waterfall_pixels_[static_cast<size_t>(y) * WATERFALL_WIDTH];

        if (y >= static_cast<int>(waterfall_rows_.size()))
        {
            std::fill(dst, dst + WATERFALL_WIDTH, IM_COL32(0, 0, 0, 255));
            continue;
        }

        const std::vector<float>& row = waterfall_rows_[y];
        for (int x = 0; x < WATERFALL_WIDTH; ++x)
        {
            const float t = (row[x] - floor) * inv_range;
            dst[x] = palette[static_cast<int>(std::min(std::max(t, 0.0f), 1.0f) * 255.0f)];
        }
    }

    platform_update_texture(waterfall_texture_, waterfall_pixels_.data(), WATERFALL_WIDTH, WATERFALL_HEIGHT);
}

void Analyzer::draw_waterfall(const ImVec2& pos, const ImVec2& size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 p0(pos.x + PLOT_MARGIN_LEFT, pos.y + 4.0f);
    const ImVec2 p1(pos.x + size.x - PLOT_MARGIN_RIGHT, pos.y + size.y - 4.0f);
    if (p1.x - p0.x < 50.0f || p1.y - p0.y < 20.0f)
        return;

    update_waterfall_texture();

    dl->AddImage(waterfall_texture_, p0, p1);
    dl->AddRect(p0, p1, IM_COL32(150, 150, 150, 255));
    dl->AddText(ImVec2(pos.x + 4.0f, p0.y), IM_COL32(200, 200, 200, 255), "Time");
}
