/*
 * Copyright (C) 2025-2026 Alexey Lysiuk
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hackrf.h>

#define MIN_FREQUENCY_HZ  1000000ULL        /* 1 MHz */
#define MAX_FREQUENCY_HZ  6000000000ULL     /* 6000 MHz */
#define MIN_SAMPLE_RATE   2000000U          /* 2 MSps */
#define MAX_SAMPLE_RATE   20000000U         /* 20 MSps */
#define MIN_VGA_GAIN      0
#define MAX_VGA_GAIN      47
#define POLL_INTERVAL_NS  100000000L    /* 100 ms */

static volatile int running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static int tx_callback(hackrf_transfer *transfer)
{
    uint8_t *buffer = transfer->buffer;
    const int32_t length = transfer->valid_length;

    for (int32_t i = 0; i < length; i++)
        buffer[i] = (uint8_t)rand();

    return running ? 0 : -1;
}

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s -f <frequency> -b <bandwidth> -p <power>\n\n", program);
    fprintf(stderr, "  -f <MHz>  Center frequency in MHz (1 to 6000)\n");
    fprintf(stderr, "  -b <MHz>  Signal bandwidth in MHz (2 to 20)\n");
    fprintf(stderr, "  -p <dB>   TX VGA gain in dB (0 to 47)\n");
    fprintf(stderr, "  -h        Show this help message\n");
}

int main(int argc, char *argv[])
{
    double freq_mhz = 0.0;
    double bw_mhz = 0.0;
    int power_db = 0;
    int freq_set = 0, bw_set = 0, power_set = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
            freq_mhz = atof(argv[++i]);
            freq_set = 1;
        }
        else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc)
        {
            bw_mhz = atof(argv[++i]);
            bw_set = 1;
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            power_db = atoi(argv[++i]);
            power_set = 1;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!freq_set || !bw_set || !power_set)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const uint64_t freq_hz = (uint64_t)(freq_mhz * 1e6);

    if (freq_hz < MIN_FREQUENCY_HZ || freq_hz > MAX_FREQUENCY_HZ)
    {
        fprintf(stderr, "Error: frequency must be between 1 and 6000 MHz\n");
        return EXIT_FAILURE;
    }

    const uint32_t sample_rate = (uint32_t)(bw_mhz * 1e6);

    if (sample_rate < MIN_SAMPLE_RATE || sample_rate > MAX_SAMPLE_RATE)
    {
        fprintf(stderr, "Error: bandwidth must be between 2 and 20 MHz\n");
        return EXIT_FAILURE;
    }

    if (power_db < MIN_VGA_GAIN || power_db > MAX_VGA_GAIN)
    {
        fprintf(stderr, "Error: power must be between 0 and 47 dB\n");
        return EXIT_FAILURE;
    }

    srand((unsigned int)time(NULL));

    int result = hackrf_init();

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_init() failed: %s\n", hackrf_error_name(result));
        return EXIT_FAILURE;
    }

    hackrf_device *device = NULL;
    result = hackrf_open(&device);

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_open() failed: %s\n", hackrf_error_name(result));
        hackrf_exit();
        return EXIT_FAILURE;
    }

    result = hackrf_set_sample_rate(device, sample_rate);

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_set_sample_rate() failed: %s\n", hackrf_error_name(result));
        hackrf_close(device);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    const uint32_t filter_bw = hackrf_compute_baseband_filter_bw(sample_rate);
    result = hackrf_set_baseband_filter_bandwidth(device, filter_bw);

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_set_baseband_filter_bandwidth() failed: %s\n", hackrf_error_name(result));
        hackrf_close(device);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    result = hackrf_set_freq(device, freq_hz);

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_set_freq() failed: %s\n", hackrf_error_name(result));
        hackrf_close(device);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    result = hackrf_set_txvga_gain(device, (uint32_t)power_db);

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_set_txvga_gain() failed: %s\n", hackrf_error_name(result));
        hackrf_close(device);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Transmitting noise at %.3f MHz, bandwidth %.1f MHz, TX VGA gain %d dB\n",
           freq_mhz, bw_mhz, power_db);
    printf("Press Ctrl+C to stop...\n");

    result = hackrf_start_tx(device, tx_callback, NULL);

    if (result != HACKRF_SUCCESS)
    {
        fprintf(stderr, "Error: hackrf_start_tx() failed: %s\n", hackrf_error_name(result));
        hackrf_close(device);
        hackrf_exit();
        return EXIT_FAILURE;
    }

    while (running && hackrf_is_streaming(device) == HACKRF_TRUE)
    {
        const struct timespec ts = { 0, POLL_INTERVAL_NS };
        nanosleep(&ts, NULL);
    }

    result = hackrf_stop_tx(device);

    if (result != HACKRF_SUCCESS)
        fprintf(stderr, "Warning: hackrf_stop_tx() failed: %s\n", hackrf_error_name(result));

    hackrf_close(device);
    hackrf_exit();

    printf("Done.\n");

    return EXIT_SUCCESS;
}
