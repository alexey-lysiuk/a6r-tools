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

#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <random>
#include <thread>
#include <vector>

#include <librfnm/device.h>

static constexpr uint64_t MIN_FREQUENCY_HZ = 1'000'000ULL;
static constexpr uint64_t MAX_FREQUENCY_HZ = 6'000'000'000ULL;
static constexpr int      MIN_BANDWIDTH_MHZ = 1;
static constexpr int      MAX_BANDWIDTH_MHZ = 100;
static constexpr uint32_t TX_BUFFER_COUNT = 256;

static std::atomic<bool> g_running{true};

static void signal_handler(int signal)
{
    (void)signal;
    g_running.store(false, std::memory_order_relaxed);
}

static const char* failcode_name(const rfnm_api_failcode code)
{
    switch (code)
    {
    case RFNM_API_OK: return "RFNM_API_OK";
    case RFNM_API_PROBE_FAIL: return "RFNM_API_PROBE_FAIL";
    case RFNM_API_TUNE_FAIL: return "RFNM_API_TUNE_FAIL";
    case RFNM_API_GAIN_FAIL: return "RFNM_API_GAIN_FAIL";
    case RFNM_API_TIMEOUT: return "RFNM_API_TIMEOUT";
    case RFNM_API_USB_FAIL: return "RFNM_API_USB_FAIL";
    case RFNM_API_DQBUF_OVERFLOW: return "RFNM_API_DQBUF_OVERFLOW";
    case RFNM_API_NOT_SUPPORTED: return "RFNM_API_NOT_SUPPORTED";
    case RFNM_API_SW_UPGRADE_REQUIRED: return "RFNM_API_SW_UPGRADE_REQUIRED";
    case RFNM_API_DQBUF_NO_DATA: return "RFNM_API_DQBUF_NO_DATA";
    case RFNM_API_MIN_QBUF_CNT_NOT_SATIFIED: return "RFNM_API_MIN_QBUF_CNT_NOT_SATIFIED";
    case RFNM_API_MIN_QBUF_QUEUE_FULL: return "RFNM_API_MIN_QBUF_QUEUE_FULL";
    default: return "RFNM_API_UNKNOWN";
    }
}

static void print_usage(const char* program)
{
    fprintf(stderr, "Usage: %s -f <frequency> -b <bandwidth> -p <power>\n\n", program);
    fprintf(stderr, "  -f <MHz>  Center frequency in MHz (1 to 6000)\n");
    fprintf(stderr, "  -b <MHz>  Signal bandwidth in MHz (1 to 100)\n");
    fprintf(stderr, "  -p <dB>   TX power in dB (device-dependent range)\n");
    fprintf(stderr, "  -h        Show this help message\n");
}

int main(int argc, char* argv[])
{
    double freq_mhz = 0.0;
    int bw_mhz = 0;
    int power_db = 0;
    bool freq_set = false;
    bool bw_set = false;
    bool power_set = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
            freq_mhz = atof(argv[++i]);
            freq_set = true;
            continue;
        }
        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc)
        {
            bw_mhz = atoi(argv[++i]);
            bw_set = true;
            continue;
        }
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            power_db = atoi(argv[++i]);
            power_set = true;
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!freq_set || !bw_set || !power_set)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const uint64_t freq_hz = static_cast<uint64_t>(freq_mhz * 1e6);
    if (freq_hz < MIN_FREQUENCY_HZ || freq_hz > MAX_FREQUENCY_HZ)
    {
        fprintf(stderr, "Error: frequency must be between 1 and 6000 MHz\n");
        return EXIT_FAILURE;
    }

    if (bw_mhz < MIN_BANDWIDTH_MHZ || bw_mhz > MAX_BANDWIDTH_MHZ)
    {
        fprintf(stderr, "Error: bandwidth must be between 1 and 100 MHz\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try
    {
        rfnm::device device(rfnm::TRANSPORT_USB);

        const uint32_t tx_channel = 0;
        const struct rfnm_api_tx_ch* channel_info = device.get_tx_channel(tx_channel);

        if (!channel_info)
        {
            fprintf(stderr, "Error: failed to query TX channel\n");
            return EXIT_FAILURE;
        }

        if (device.get_tx_channel_count() == 0)
        {
            fprintf(stderr, "Error: no LibreSDR TX channels available\n");
            return EXIT_FAILURE;
        }

        if (power_db < channel_info->power_range.min || power_db > channel_info->power_range.max)
        {
            fprintf(stderr, "Error: power must be between %d and %u dB\n",
                    channel_info->power_range.min,
                    channel_info->power_range.max);
            return EXIT_FAILURE;
        }

        rfnm_api_failcode result = device.set_tx_channel_samp_freq_div(tx_channel, 1, 2, false);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_tx_channel_samp_freq_div() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.set_tx_channel_freq(tx_channel, static_cast<int64_t>(freq_hz), false);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_tx_channel_freq() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.set_tx_channel_rfic_lpf_bw(tx_channel, static_cast<int16_t>(bw_mhz), false);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_tx_channel_rfic_lpf_bw() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.set_tx_channel_power(tx_channel, static_cast<int8_t>(power_db), false);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_tx_channel_power() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.set_tx_channel_path(tx_channel, channel_info->path_preferred, false);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_tx_channel_path() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.set_tx_channel_active(tx_channel, RFNM_CH_ON, RFNM_CH_STREAM_AUTO, false);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_tx_channel_active() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.set(rfnm::tx_channel_apply_flags[tx_channel]);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: applying TX settings failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        size_t stream_buffer_bytes = 0;
        result = device.set_stream_format(rfnm::STREAM_FORMAT_CS16, &stream_buffer_bytes);
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: set_stream_format() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        result = device.tx_work_start();
        if (result != RFNM_API_OK)
        {
            fprintf(stderr, "Error: tx_work_start() failed: %s\n", failcode_name(result));
            return EXIT_FAILURE;
        }

        const size_t sample_count = stream_buffer_bytes / sizeof(int16_t);

        std::vector<rfnm::tx_buf> tx_buffers(TX_BUFFER_COUNT);
        std::vector<std::vector<int16_t>> sample_buffers(TX_BUFFER_COUNT, std::vector<int16_t>(sample_count));
        std::queue<size_t> available;

        for (size_t i = 0; i < TX_BUFFER_COUNT; ++i)
        {
            tx_buffers[i].buf = reinterpret_cast<uint8_t*>(sample_buffers[i].data());
            tx_buffers[i].dac_id = channel_info->dac_id;
            tx_buffers[i].phytimer = 0;
            available.push(i);
        }

        uint32_t dac_cc = 0;
        std::mt19937 rng(static_cast<uint32_t>(std::random_device{}()));
        std::uniform_int_distribution<int16_t> noise(-32768, 32767);

        printf("Transmitting noise at %.3f MHz, bandwidth %d MHz, TX power %d dB\n",
               freq_mhz, bw_mhz, power_db);
        printf("Press Ctrl+C to stop...\n");

        while (g_running.load(std::memory_order_relaxed))
        {
            rfnm::tx_buf* completed = nullptr;
            while (device.tx_dqbuf(&completed) == RFNM_API_OK)
            {
                const auto completed_index = static_cast<size_t>(completed - tx_buffers.data());
                if (completed_index < TX_BUFFER_COUNT)
                    available.push(completed_index);
            }

            if (available.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const size_t index = available.front();
            available.pop();

            auto& samples = sample_buffers[index];
            for (auto& sample : samples)
                sample = noise(rng);

            tx_buffers[index].dac_cc = ++dac_cc;

            result = device.tx_qbuf(&tx_buffers[index]);
            if (result == RFNM_API_OK)
                continue;

            available.push(index);

            if (result == RFNM_API_MIN_QBUF_QUEUE_FULL)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            fprintf(stderr, "Error: tx_qbuf() failed: %s\n", failcode_name(result));
            break;
        }

        const rfnm_api_failcode stop_result = device.tx_work_stop();
        if (stop_result != RFNM_API_OK)
            fprintf(stderr, "Warning: tx_work_stop() failed: %s\n", failcode_name(stop_result));

        result = device.set_tx_channel_active(tx_channel, RFNM_CH_OFF, RFNM_CH_STREAM_AUTO, false);
        if (result == RFNM_API_OK)
            result = device.set(rfnm::tx_channel_apply_flags[tx_channel]);
        if (result != RFNM_API_OK)
            fprintf(stderr, "Warning: failed to disable TX channel: %s\n", failcode_name(result));

        printf("Done.\n");
    }
    catch (const std::exception& exception)
    {
        fprintf(stderr, "Error: %s\n", exception.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
