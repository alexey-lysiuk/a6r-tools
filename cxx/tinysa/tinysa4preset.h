#pragma once

#include <cstddef>
#include <cstdint>

namespace TinySA4
{
	using freq_t = uint64_t;
	using systime_t = uint32_t;

	constexpr size_t BANDS_MAX = 8;
	constexpr size_t BAND_NAME_SIZE = 9;
	constexpr size_t LIMITS_MAX = 8;
	constexpr size_t MARKER_COUNT = 8;
	constexpr size_t MARKERS_MAX = MARKER_COUNT;
	constexpr size_t PRESET_NAME_LENGTH = 10;
	constexpr size_t TRACES_MAX = 4;
	constexpr size_t REFERENCE_MAX = TRACES_MAX;

	struct alignas(8) Band
	{
		char name[BAND_NAME_SIZE];
		bool enabled;
		freq_t start;
		freq_t end;
		float level;
		int start_index;
		int stop_index;
	};

	struct alignas(8) Marker
	{
		uint8_t mtype;
		uint8_t enabled;
		uint8_t ref;
		uint8_t trace;
		int16_t index;
		freq_t frequency;
	};

	struct alignas(8) Limit
	{
		uint8_t enabled;
		float level;
		freq_t frequency;
		int16_t index;
	};

	struct alignas(8) Preset
	{
		uint32_t magic;
		bool auto_reflevel;          // bool
		bool auto_attenuation;       // bool
		bool mirror_masking;         // bool
		bool tracking_output;        // bool
		bool mute;                   // bool
		bool auto_IF;                // bool
		bool sweep;                  // bool
		bool pulse;                  // bool
		bool stored[TRACES_MAX];     // enum
		bool normalized[TRACES_MAX];     // enum
		Band bands[BANDS_MAX];

		uint8_t mode;                // enum
		uint8_t below_IF;            // enum
		uint8_t unit;                // enum
		uint8_t agc;                 // enum
		uint8_t lna;                 // enum
		uint8_t modulation;          // enum
		uint8_t trigger;             // enum
		uint8_t trigger_mode;        // enum
		uint8_t trigger_direction;   // enum
		uint8_t trigger_beep;
		uint8_t trigger_auto_save;
		uint8_t step_delay_mode;     // enum
		uint8_t waterfall;           // enum
		uint8_t level_meter;         // enum
		uint8_t average[TRACES_MAX]; // enum
		uint8_t subtract[TRACES_MAX];// index
		uint8_t measurement;         // enum
		uint8_t spur_removal;        // enum
		uint8_t disable_correction;
		int8_t normalized_trace;
		uint8_t listen;

		int8_t tracking;            // -1...1 Can NOT convert to bool!!!!!!
		uint8_t atten_step;          //  0...1 !!! need convert to bool
		int8_t _active_marker;       // -1...MARKER_MAX
		uint8_t unit_scale_index;    // table index
		uint8_t noise;               // 2...50
		uint8_t lo_drive;            // 0-3 , 3dB steps
		uint8_t rx_drive;            // 0-15 , 7=+20dBm, 3dB steps
		uint8_t test;                // current test number
		uint8_t harmonic;            // used harmonic number 1...5
		uint8_t fast_speedup;        // 0 - 20
		uint8_t faster_speedup;      // 0 - 20
		uint8_t _traces;            // enabled traces flags
		uint8_t draw_line;         // uses the trigger level setting
		uint8_t lock_display;
		uint8_t jog_jump;
		uint8_t multi_band;
		uint8_t multi_trace;
		uint8_t trigger_trace;
		uint16_t repeat;              // 1...100
		uint16_t linearity_step;     // range equal POINTS_COUNT
		uint16_t _sweep_points;
		int16_t attenuate_x2;        // 0...60 !!! in calculation can be < 0

		uint16_t step_delay;         // KM_SAMPLETIME   250...10000, 0=auto
		uint16_t offset_delay;       // KM_OFFSET_DELAY 250...10000, 0=auto

		uint16_t freq_mode;           //  0...1!!! need convert to bool or bit field
		int16_t refer;               // -1 disabled

		uint16_t modulation_depth_x100;      // AM (30% - 100%) multiplied by 100
		uint16_t modulation_deviation_div100;  // FM (2.5kHz to 100kHz) divided by 100

		int decay;                      // KM_DECAY   < 1000000
		int attack;                     // KM_ATTACK  <   20000

		int32_t slider_position;
		freq_t slider_span;

		uint32_t rbw_x10;
		uint32_t vbw_x100;
		uint32_t scan_after_dirty[TRACES_MAX];

		float modulation_frequency;  // 50...6000
		float reflevel;
		float scale;
		float external_gain;
		float trigger_level;
		float level;
		float level_sweep;

		float unit_scale;
		float normalize_level;     // Level to set normalize to, zero if not doing anything

		freq_t frequency_step;
		freq_t frequency0;
		freq_t frequency1;
		freq_t frequency_var;
		freq_t frequency_IF;
		freq_t frequency_offset;
		float trace_scale;
		float trace_refpos;
		Marker _markers[MARKERS_MAX];
		Limit limits[REFERENCE_MAX][LIMITS_MAX];
		systime_t sweep_time_us;
		systime_t measure_sweep_time_us;
		systime_t actual_sweep_time_us;
		systime_t additional_step_delay_us;

		uint32_t trigger_grid;

		uint8_t ultra;    // enum ??
		bool extra_lna;
		int R;            // KM_R
		int32_t exp_aver;
		bool increased_R;
		bool mixer_output;
		uint32_t interval;
		char preset_name[PRESET_NAME_LENGTH];
		bool dBuV;
		int64_t test_argument;            // used for tests
		uint32_t checksum;            // must be last and at 4 byte boundary
	};

	static_assert(sizeof(Preset) == 1584, "Preset size is incorrect");

} // namespace TinySA4
