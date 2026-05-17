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

#include <ad9361.h>
#include <iio.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define MIN_FREQUENCY_HZ 1000000ULL
#define MAX_FREQUENCY_HZ 6000000000ULL
#define MIN_BANDWIDTH_HZ 1000000LL
#define MAX_BANDWIDTH_HZ 100000000LL
#define TX_MIN_SAMPLE_RATE_HZ 20000000UL
#define TX_MAX_SAMPLE_RATE_HZ 61440000UL
#define TX_BUFFER_SAMPLES 16384

static volatile sig_atomic_t g_running = 1;

static uint32_t g_noise_rng_state = 0xA5A5A5A5U;

static void signal_handler(int signal)
{
    (void)signal;
    g_running = 0;
}

static int16_t next_noise_sample(void)
{
    /* Prevent xorshift32 from entering a zero lock-up state. */
    if (g_noise_rng_state == 0)
        g_noise_rng_state = 1U;

    g_noise_rng_state ^= g_noise_rng_state << 13;
    g_noise_rng_state ^= g_noise_rng_state >> 17;
    g_noise_rng_state ^= g_noise_rng_state << 5;
    return (int16_t)g_noise_rng_state;
}

static uint32_t compute_sample_rate_hz(int64_t bandwidth_hz)
{
    const int64_t preferred_rate_hz = bandwidth_hz * 2;
    if (preferred_rate_hz < (int64_t)TX_MIN_SAMPLE_RATE_HZ)
        return TX_MIN_SAMPLE_RATE_HZ;
    if (preferred_rate_hz > (int64_t)TX_MAX_SAMPLE_RATE_HZ)
        return TX_MAX_SAMPLE_RATE_HZ;
    return (uint32_t)preferred_rate_hz;
}

static void print_usage(const char* program)
{
    fprintf(stderr, "Usage: %s -f <frequency> -b <bandwidth> -p <power>\n\n", program);
    fprintf(stderr, "  -f <MHz>  Center frequency in MHz (1 to 6000)\n");
    fprintf(stderr, "  -b <MHz>  RF bandwidth in MHz (1 to 100)\n");
    fprintf(stderr, "  -p <dB>   TX hardware gain in dB (device-dependent range)\n");
    fprintf(stderr, "  -h        Show this help message\n");
}

static bool parse_double(const char* text, double* value)
{
    char* end = NULL;

    if (!text || !value)
        return false;

    errno = 0;
    *value = strtod(text, &end);

    return errno == 0 && end != text && end && *end == '\0';
}

static bool parse_int(const char* text, int* value)
{
    char* end = NULL;
    long parsed = 0;

    if (!text || !value)
        return false;

    errno = 0;
    parsed = strtol(text, &end, 10);

    if (errno != 0 || end == text || !end || *end != '\0')
        return false;

    if (parsed < INT32_MIN || parsed > INT32_MAX)
        return false;

    *value = (int)parsed;
    return true;
}

static void print_errno_error(const char* message, int error_code)
{
    if (error_code != 0)
        fprintf(stderr, "%s: errno=%d (%s)\n", message, error_code, strerror(error_code));
    else
        fprintf(stderr, "%s (errno was not set)\n", message);
}

static struct iio_context* create_iio_context_auto(void)
{
    struct iio_scan_context* scan_context = NULL;
    struct iio_context_info** context_info = NULL;
    ssize_t context_count = 0;
    struct iio_context* context = NULL;
    int scan_context_error_code = 0;

    scan_context = iio_create_scan_context(NULL, 0);
    if (scan_context)
    {
        context_count = iio_scan_context_get_info_list(scan_context, &context_info);
        if (context_count < 0)
        {
            fprintf(stderr,
                    "Warning: failed to scan IIO contexts (error code: %zd)\n",
                    context_count);
        }
        else if (context_count == 1)
        {
            const char* uri = iio_context_info_get_uri(context_info[0]);
            if (uri)
            {
                errno = 0;
                context = iio_create_context_from_uri(uri);
                if (!context)
                {
                    print_errno_error("Error: failed to create IIO context from scanned URI", errno);
                    fprintf(stderr, "Warning: falling back to default IIO context\n");
                }
            }
        }
        else if (context_count > 1)
        {
            fprintf(stderr,
                    "Warning: %zd IIO contexts detected, skipping auto-selection\n",
                    context_count);
        }

        if (context_count >= 0 && context_info)
            iio_context_info_list_free(context_info);
        iio_scan_context_destroy(scan_context);
    }
    else
    {
        scan_context_error_code = errno;
        print_errno_error("Warning: failed to create IIO scan context", scan_context_error_code);
    }

    if (context)
        return context;

    errno = 0;
    context = iio_create_default_context();
    if (!context)
        print_errno_error("Error: failed to create IIO context", errno);

    return context;
}

static int configure_tx(struct iio_context** context_out,
                        struct iio_device** tx_dev_out,
                        struct iio_channel** tx_i_out,
                        struct iio_channel** tx_q_out,
                        uint64_t freq_hz,
                        int64_t bandwidth_hz,
                        int gain_db)
{
    struct iio_context* context = NULL;
    struct iio_device* phy = NULL;
    struct iio_device* tx_dev = NULL;
    struct iio_channel* phy_tx = NULL;
    struct iio_channel* lo_tx = NULL;
    struct iio_channel* tx_i = NULL;
    struct iio_channel* tx_q = NULL;
    int result = 0;

    context = create_iio_context_auto();
    if (!context)
        return -1;

    phy = iio_context_find_device(context, "ad9361-phy");
    tx_dev = iio_context_find_device(context, "cf-ad9361-dds-core-lpc");

    if (!phy || !tx_dev)
    {
        fprintf(stderr, "Error: failed to find AD9361 devices in IIO context\n");
        iio_context_destroy(context);
        return -1;
    }

    phy_tx = iio_device_find_channel(phy, "voltage0", true);
    lo_tx = iio_device_find_channel(phy, "altvoltage1", true);

    if (!phy_tx || !lo_tx)
    {
        fprintf(stderr, "Error: failed to locate TX configuration channels\n");
        iio_context_destroy(context);
        return -1;
    }

    tx_i = iio_device_find_channel(tx_dev, "voltage0", true);
    tx_q = iio_device_find_channel(tx_dev, "voltage1", true);

    if (!tx_i || !tx_q)
    {
        fprintf(stderr, "Error: failed to locate TX streaming channels\n");
        iio_context_destroy(context);
        return -1;
    }

    result = ad9361_set_bb_rate(phy, compute_sample_rate_hz(bandwidth_hz));
    if (result < 0)
    {
        fprintf(stderr, "Error: ad9361_set_bb_rate() failed: %d\n", result);
        iio_context_destroy(context);
        return -1;
    }

    result = iio_channel_attr_write_longlong(phy_tx, "rf_bandwidth", bandwidth_hz);
    if (result < 0)
    {
        fprintf(stderr, "Error: setting rf_bandwidth failed: %d\n", result);
        iio_context_destroy(context);
        return -1;
    }

    result = iio_channel_attr_write_double(phy_tx, "hardwaregain", (double)gain_db);
    if (result < 0)
    {
        fprintf(stderr, "Error: setting hardwaregain failed: %d\n", result);
        iio_context_destroy(context);
        return -1;
    }

    result = iio_channel_attr_write_longlong(lo_tx, "frequency", (long long)freq_hz);
    if (result < 0)
    {
        fprintf(stderr, "Error: setting frequency failed: %d\n", result);
        iio_context_destroy(context);
        return -1;
    }

    iio_channel_enable(tx_i);
    iio_channel_enable(tx_q);

    *context_out = context;
    *tx_dev_out = tx_dev;
    *tx_i_out = tx_i;
    *tx_q_out = tx_q;

    return 0;
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
            if (!parse_double(argv[++i], &freq_mhz))
            {
                fprintf(stderr, "Error: invalid frequency value\n");
                return EXIT_FAILURE;
            }
            freq_set = true;
            continue;
        }

        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &bw_mhz))
            {
                fprintf(stderr, "Error: invalid bandwidth value\n");
                return EXIT_FAILURE;
            }
            bw_set = true;
            continue;
        }

        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            if (!parse_int(argv[++i], &power_db))
            {
                fprintf(stderr, "Error: invalid power value\n");
                return EXIT_FAILURE;
            }
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

    const uint64_t freq_hz = (uint64_t)(freq_mhz * 1e6);
    const int64_t bandwidth_hz = (int64_t)bw_mhz * 1000000LL;

    if (freq_hz < MIN_FREQUENCY_HZ || freq_hz > MAX_FREQUENCY_HZ)
    {
        fprintf(stderr, "Error: frequency must be between 1 and 6000 MHz\n");
        return EXIT_FAILURE;
    }

    if (bandwidth_hz < MIN_BANDWIDTH_HZ || bandwidth_hz > MAX_BANDWIDTH_HZ)
    {
        fprintf(stderr, "Error: bandwidth must be between 1 and 100 MHz\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct iio_context* context = NULL;
    struct iio_device* tx_dev = NULL;
    struct iio_channel* tx_i = NULL;
    struct iio_channel* tx_q = NULL;
    struct iio_buffer* tx_buffer = NULL;

    if (configure_tx(&context, &tx_dev, &tx_i, &tx_q, freq_hz, bandwidth_hz, power_db) != 0)
        return EXIT_FAILURE;

    tx_buffer = iio_device_create_buffer(tx_dev, TX_BUFFER_SAMPLES, false);
    if (!tx_buffer)
    {
        fprintf(stderr, "Error: failed to create TX buffer\n");
        iio_context_destroy(context);
        return EXIT_FAILURE;
    }

    g_noise_rng_state = (uint32_t)time(NULL) ^ ((uint32_t)clock() << 1);

    printf("Transmitting noise at %.3f MHz, bandwidth %d MHz, TX hardware gain %d dB\n",
           freq_mhz, bw_mhz, power_db);
    printf("Press Ctrl+C to stop...\n");

    while (g_running)
    {
        const ptrdiff_t step = iio_buffer_step(tx_buffer);
        char* sample_ptr = (char*)iio_buffer_first(tx_buffer, tx_i);
        char* end = (char*)iio_buffer_end(tx_buffer);

        while (sample_ptr < end)
        {
            /*
             * TX buffer format is CS16 with interleaved I/Q, so each complex
             * sample is stored as two consecutive int16_t values.
             */
            int16_t* i_sample = (int16_t*)sample_ptr;
            int16_t* q_sample = i_sample + 1;

            *i_sample = next_noise_sample();
            *q_sample = next_noise_sample();

            sample_ptr += step;
        }

        const ssize_t push_result = iio_buffer_push(tx_buffer);
        if (push_result < 0)
        {
            fprintf(stderr, "Error: iio_buffer_push() failed: %zd\n", push_result);
            break;
        }
    }

    iio_buffer_destroy(tx_buffer);
    iio_channel_disable(tx_i);
    iio_channel_disable(tx_q);
    iio_context_destroy(context);

    printf("Done.\n");

    return EXIT_SUCCESS;
}
