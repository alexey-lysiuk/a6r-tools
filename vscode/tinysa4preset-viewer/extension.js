//
// Copyright (C) 2025-2026 Alexey Lysiuk
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

'use strict';

const vscode = require('vscode');

// ── Constants ──────────────────────────────────────────────────────────────────

const SETTING_MAGIC = 0x434f4e6d;  // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1502
const PRESET_NAME_LENGTH = 10;      // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1361
const TRACES_MAX = 4;               // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L198
const MARKERS_MAX = 8;              // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L197
const BANDS_MAX = 8;                // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1208
const LIMITS_MAX = 8;               // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L948
const REFERENCE_MAX = TRACES_MAX;   // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L952
const CHECKSUM_BYTES = 1576;        // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/flash.c#L146

// ── Enum lookup tables ─────────────────────────────────────────────────────────
// https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h

const MODE_NAMES = {        // M_LOW … M_ULTRA (L329-L331)
    0: 'Low input', 1: 'High input', 2: 'Low output', 3: 'High output', 4: 'Ultra'
};
const UNIT_NAMES = {        // U_DBM … U_DBC (L779-L781)
    0: 'dBm', 1: 'dBmV', 2: 'dBμV', 3: 'Raw', 4: 'Volt', 5: 'Vpp', 6: 'Watt', 7: 'dBc'
};
const MODULATION_NAMES = {  // MO_NONE … MO_EXTERNAL (L333-L339)
    0: 'None', 1: 'AM', 2: 'NFM', 3: 'NFM2', 4: 'NFM3', 5: 'WFM', 6: 'External'
};
const S_NAMES = {           // S_OFF … S_AUTO_ON (L1380)
    0: 'Off', 1: 'On', 2: 'Auto off', 3: 'Auto on'
};
const SD_NAMES = {          // SD_NORMAL … SD_MANUAL (L1382)
    0: 'Normal', 1: 'Precise', 2: 'Fast', 3: 'Noise source', 4: 'Manual'
};
const W_NAMES = {           // W_OFF … W_SUPER (L1384)
    0: 'Off', 1: 'Small', 2: 'Big', 3: 'Super'
};
const MEASUREMENT_NAMES = { // M_OFF … M_DECONV (L1862-L1864)
    0: 'Off', 1: 'IMD', 2: 'OIP3', 3: 'Phase noise', 4: 'SNR', 5: 'Pass band',
    6: 'Linearity', 7: 'AM', 8: 'FM', 9: 'THD', 10: 'CP', 11: 'NF (tinySA)',
    12: 'NF (store)', 13: 'NF (validate)', 14: 'NF (amplifier)', 15: 'Deconv'
};
const TRIGGER_NAMES = {     // T_AUTO … T_AUTO_SAVE (L1867-L1869)
    0: 'Auto', 1: 'Normal', 2: 'Single', 3: 'Done', 4: 'Up', 5: 'Down',
    6: 'Mode', 7: 'Pre', 8: 'Post', 9: 'Mid', 10: 'Beep', 11: 'Auto save'
};
const MARKER_TYPE_FLAGS = { // M_NORMAL … M_DELETE (L911-L913)
    0: 'Normal', 1: 'Reference', 2: 'Delta', 4: 'Noise',
    8: 'Stored', 16: 'Average', 32: 'Tracking', 64: 'Delete'
};

function enumName(table, value) {
    return table[value] !== undefined ? table[value] : String(value);
}

function markerTypeName(mtype) {
    if (mtype === 0) return 'Normal';
    const flags = [];
    for (const [bit, name] of Object.entries(MARKER_TYPE_FLAGS)) {
        if (Number(bit) !== 0 && (mtype & Number(bit)) !== 0) flags.push(name);
    }
    return flags.length > 0 ? flags.join(', ') : String(mtype);
}

// ── Binary reader ──────────────────────────────────────────────────────────────

class BinaryReader {
    constructor(data) {
        this.view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        this.offset = 0;
    }

    readUint8()   { return this.view.getUint8(this.offset++); }
    readInt8()    { return this.view.getInt8(this.offset++); }
    readBool()    { return this.view.getUint8(this.offset++) !== 0; }
    readUint16()  { const v = this.view.getUint16(this.offset, true); this.offset += 2; return v; }
    readInt16()   { const v = this.view.getInt16(this.offset, true); this.offset += 2; return v; }
    readUint32()  { const v = this.view.getUint32(this.offset, true); this.offset += 4; return v; }
    readInt32()   { const v = this.view.getInt32(this.offset, true); this.offset += 4; return v; }
    readFloat32() { const v = this.view.getFloat32(this.offset, true); this.offset += 4; return v; }

    readUint64() {
        // Frequencies are at most ~6 GHz, well within Number.MAX_SAFE_INTEGER
        const lo = this.view.getUint32(this.offset, true);
        const hi = this.view.getUint32(this.offset + 4, true);
        this.offset += 8;
        return hi * 0x100000000 + lo;
    }

    skip(n) { this.offset += n; }

    readString(n) {
        let str = '';
        for (let i = 0; i < n; i++) {
            const c = this.view.getUint8(this.offset + i);
            if (c === 0) break;
            str += String.fromCharCode(c);
        }
        this.offset += n;
        return str;
    }
}

// ── Checksum ───────────────────────────────────────────────────────────────────
// https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/flash.c#L146

function calculateChecksum(data) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const count = CHECKSUM_BYTES / 4;
    let checksum = 0;
    for (let i = 0; i < count; i++) {
        const n = view.getUint32(i * 4, true);
        checksum = ((checksum << 1) | (checksum >>> 31)) >>> 0;  // rotate left by 1
        checksum = (checksum + n) >>> 0;                          // add with uint32 wrap
    }
    return checksum;
}

// ── Sub-structure parsers ──────────────────────────────────────────────────────

// https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L937-L944
// Format: '<B?3B3xQ'
function parseMarker(reader) {
    const m = {};
    m.mtype     = reader.readUint8();   // B
    m.enabled   = reader.readBool();    // ?
    m.ref       = reader.readUint8();   // B
    m.trace     = reader.readUint8();   // B
    m.index     = reader.readUint8();   // B
    reader.skip(3);                      // 3x
    m.frequency = reader.readUint64();  // Q
    return m;
}

// https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L953-L958
// Format: '<?3xfQh6x'
function parseLimit(reader) {
    const l = {};
    l.enabled   = reader.readBool();    // ?
    reader.skip(3);                      // 3x
    l.level     = reader.readFloat32(); // f
    l.frequency = reader.readUint64();  // Q
    l.index     = reader.readInt16();   // h
    reader.skip(6);                      // 6x
    return l;
}

// https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1207-L1219
// Format: '<9s?6x2Qf2i4x'
function parseBand(reader) {
    const b = {};
    b.name        = reader.readString(9); // 9s
    b.enabled     = reader.readBool();    // ?
    reader.skip(6);                        // 6x
    b.start       = reader.readUint64();  // Q
    b.end         = reader.readUint64();  // Q
    b.level       = reader.readFloat32(); // f
    b.start_index = reader.readInt32();   // i
    b.stop_index  = reader.readInt32();   // i
    reader.skip(4);                        // 4x
    return b;
}

// ── Main preset parser ─────────────────────────────────────────────────────────
// Mirrors python/tinysa4preset.py Preset.from_binary()

function parsePreset(data) {
    const reader = new BinaryReader(data);
    const p = {};

    // Magic: '<I'
    const magic = reader.readUint32();
    if (magic !== SETTING_MAGIC) {
        throw new Error(`Invalid preset file: wrong magic 0x${magic.toString(16).padStart(8, '0')}`);
    }

    // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1224-L1238
    // PRESET_1: '<8?'
    p.auto_reflevel    = reader.readBool();
    p.auto_attenuation = reader.readBool();
    p.mirror_masking   = reader.readBool();
    p.tracking_output  = reader.readBool();
    p.mute             = reader.readBool();
    p.auto_if          = reader.readBool();
    p.sweep            = reader.readBool();
    p.pulse            = reader.readBool();

    // BOOL_TRACES: '<4?'  (stored and normalized arrays)
    p.stored     = [reader.readBool(), reader.readBool(), reader.readBool(), reader.readBool()];
    p.normalized = [reader.readBool(), reader.readBool(), reader.readBool(), reader.readBool()];
    reader.skip(4);  // padding

    // 8 bands: '<9s?6x2Qf2i4x'
    p.bands = [];
    for (let i = 0; i < BANDS_MAX; i++) p.bands.push(parseBand(reader));

    // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1240-L1266
    // PRESET_2: '<14B'
    p.mode              = reader.readUint8();
    p.below_if          = reader.readUint8();
    p.unit              = reader.readUint8();
    p.agc               = reader.readUint8();
    p.lna               = reader.readUint8();
    p.modulation        = reader.readUint8();
    p.trigger           = reader.readUint8();
    p.trigger_mode      = reader.readUint8();
    p.trigger_direction = reader.readUint8();
    p.trigger_beep      = reader.readUint8();
    p.trigger_auto_save = reader.readUint8();
    p.step_delay_mode   = reader.readUint8();
    p.waterfall         = reader.readUint8();
    p.level_meter       = reader.readUint8();

    // UINT8_TRACES: '<4B'
    p.average  = [reader.readUint8(), reader.readUint8(), reader.readUint8(), reader.readUint8()];
    p.subtract = [reader.readUint8(), reader.readUint8(), reader.readUint8(), reader.readUint8()];

    // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1268-L1323
    // PRESET_3: '<3BbBbBb15Bx3Hh3Hh2H2x3iQ2I'
    p.measurement                 = reader.readUint8();
    p.spur_removal                = reader.readUint8();
    p.disable_correction          = reader.readUint8();
    p.normalized_trace            = reader.readInt8();
    p.listen                      = reader.readUint8();
    p.tracking                    = reader.readInt8();
    p.atten_step                  = reader.readUint8();
    p.active_marker               = reader.readInt8();
    p.unit_scale_index            = reader.readUint8();
    p.noise                       = reader.readUint8();
    p.lo_drive                    = reader.readUint8();
    p.rx_drive                    = reader.readUint8();
    p.test                        = reader.readUint8();
    p.harmonic                    = reader.readUint8();
    p.fast_speedup                = reader.readUint8();
    p.faster_speedup              = reader.readUint8();
    p.traces                      = reader.readUint8();
    p.draw_line                   = reader.readUint8();
    p.lock_display                = reader.readUint8();
    p.jog_jump                    = reader.readUint8();
    p.multi_band                  = reader.readUint8();
    p.multi_trace                 = reader.readUint8();
    p.trigger_trace               = reader.readUint8();
    reader.skip(1);                                      // x
    p.repeat                      = reader.readUint16();
    p.linearity_step              = reader.readUint16();
    p.sweep_points                = reader.readUint16();
    p.attenuate_x2                = reader.readInt16();
    p.step_delay                  = reader.readUint16();
    p.offset_delay                = reader.readUint16();
    p.freq_mode                   = reader.readUint16();
    p.refer                       = reader.readInt16();
    p.modulation_depth_x100       = reader.readUint16();
    p.modulation_deviation_div100 = reader.readUint16();
    reader.skip(2);                                      // 2x
    p.decay                       = reader.readInt32();
    p.attack                      = reader.readInt32();
    p.slider_position             = reader.readInt32();
    p.slider_span                 = reader.readUint64();
    p.rbw_x10                     = reader.readUint32();
    p.vbw_x100                    = reader.readUint32();

    // UINT_TRACES: '<4I'
    p.scan_after_dirty = [reader.readUint32(), reader.readUint32(), reader.readUint32(), reader.readUint32()];

    // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1325-L1346
    // PRESET_4: '<9f4x6Q2f'
    p.modulation_frequency = reader.readFloat32();
    p.reflevel             = reader.readFloat32();
    p.scale                = reader.readFloat32();
    p.external_gain        = reader.readFloat32();
    p.trigger_level        = reader.readFloat32();
    p.level                = reader.readFloat32();
    p.level_sweep          = reader.readFloat32();
    p.unit_scale           = reader.readFloat32();
    p.normalize_level      = reader.readFloat32();
    reader.skip(4);                                      // 4x
    p.frequency_step   = reader.readUint64();
    p.frequency0       = reader.readUint64();
    p.frequency1       = reader.readUint64();
    p.frequency_var    = reader.readUint64();
    p.frequency_if     = reader.readUint64();
    p.frequency_offset = reader.readUint64();
    p.trace_scale      = reader.readFloat32();
    p.trace_refpos     = reader.readFloat32();

    // 8 markers: '<B?3B3xQ'
    p.markers = [];
    for (let i = 0; i < MARKERS_MAX; i++) p.markers.push(parseMarker(reader));

    // 8×4 limits: '<?3xfQh6x'
    p.limits = [];
    for (let i = 0; i < LIMITS_MAX; i++) {
        const row = [];
        for (let j = 0; j < REFERENCE_MAX; j++) row.push(parseLimit(reader));
        p.limits.push(row);
    }

    // https://github.com/erikkaashoek/tinySA/blob/26e33a0d9c367a3e1ca71463e80fd2118c3e9ea7/nanovna.h#L1351-L1366
    // PRESET_5: '<5IB?2x2i2?2xI10s?5xQ'
    p.sweep_time_us            = reader.readUint32();
    p.measure_sweep_time_us    = reader.readUint32();
    p.actual_sweep_time_us     = reader.readUint32();
    p.additional_step_delay_us = reader.readUint32();
    p.trigger_grid             = reader.readUint32();
    p.ultra                    = reader.readUint8();
    p.extra_lna                = reader.readBool();
    reader.skip(2);                                      // 2x
    p.r                        = reader.readInt32();
    p.exp_aver                 = reader.readInt32();
    p.increased_r              = reader.readBool();
    p.mixer_output             = reader.readBool();
    reader.skip(2);                                      // 2x
    p.interval                 = reader.readUint32();
    p.preset_name              = reader.readString(PRESET_NAME_LENGTH);
    p.dbuv                     = reader.readBool();
    reader.skip(5);                                      // 5x
    p.test_argument            = reader.readUint64();

    // CHECKSUM: '<I4x'
    const fileChecksum = reader.readUint32();
    reader.skip(4);  // 4x

    const calculatedChecksum = calculateChecksum(data);
    if (calculatedChecksum !== fileChecksum) {
        throw new Error(
            `Checksum mismatch: file=0x${fileChecksum.toString(16).padStart(8, '0')}, ` +
            `calculated=0x${calculatedChecksum.toString(16).padStart(8, '0')}`
        );
    }

    return p;
}

// ── Format helpers ─────────────────────────────────────────────────────────────

function formatFreq(hz) {
    if (hz === 0) return '0 Hz';
    if (hz < 1000) return `${hz} Hz`;
    if (hz < 1000000) return `${(hz / 1000).toFixed(3)} kHz`;
    if (hz < 1000000000) return `${(hz / 1000000).toFixed(6)} MHz`;
    return `${(hz / 1000000000).toFixed(9)} GHz`;
}

function formatTime(us) {
    if (us === 0) return '0';
    if (us < 1000) return `${us} μs`;
    if (us < 1000000) return `${(us / 1000).toFixed(3)} ms`;
    return `${(us / 1000000).toFixed(3)} s`;
}

function formatRBW(rbw_x10) {
    if (rbw_x10 === 0) return 'Auto';
    return formatFreq(rbw_x10 / 10);
}

function formatVBW(vbw_x100) {
    if (vbw_x100 === 0) return 'Auto';
    return formatFreq(vbw_x100 / 100);
}

// HTML-escape a string to prevent XSS in the generated WebView HTML
function esc(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// ── HTML generator helpers ─────────────────────────────────────────────────────

// Render a key-value table (two-column: label | value)
function kvTable(rows) {
    const rowsHtml = rows.map(([k, v]) =>
        `<tr><th>${esc(k)}</th><td>${esc(String(v))}</td></tr>`
    ).join('\n');
    return `<table class="kv">\n${rowsHtml}\n</table>`;
}

// Render a collapsible section
function section(title, content, collapsed = false) {
    const openAttr = collapsed ? '' : ' open';
    return `<details${openAttr}>
<summary><span class="section-title">${esc(title)}</span></summary>
<div class="section-content">${content}</div>
</details>`;
}

// ── WebView HTML generator ─────────────────────────────────────────────────────

function getWebviewHtml(preset, filename) {
    const name = preset.preset_name || filename;

    // ── Overview ────────────────────────────────────────────────────────────────
    const overviewRows = [
        ['Preset name',  preset.preset_name || '(unnamed)'],
        ['Mode',         enumName(MODE_NAMES, preset.mode)],
        ['Unit',         enumName(UNIT_NAMES, preset.unit)],
        ['Measurement',  enumName(MEASUREMENT_NAMES, preset.measurement)],
        ['Waterfall',    enumName(W_NAMES, preset.waterfall)],
        ['Multi-band',   preset.multi_band  ? 'Yes' : 'No'],
        ['Multi-trace',  preset.multi_trace ? 'Yes' : 'No'],
    ];

    // ── Frequency ───────────────────────────────────────────────────────────────
    const freqRows = [
        ['Start',             formatFreq(preset.frequency0)],
        ['Stop',              formatFreq(preset.frequency1)],
        ['Step',              formatFreq(preset.frequency_step)],
        ['Sweep points',      preset.sweep_points],
        ['RBW',               formatRBW(preset.rbw_x10)],
        ['VBW',               formatVBW(preset.vbw_x100)],
        ['IF frequency',      formatFreq(preset.frequency_if)],
        ['Frequency offset',  formatFreq(preset.frequency_offset)],
    ];

    // ── Level ───────────────────────────────────────────────────────────────────
    const levelRows = [
        ['Reference level',    `${preset.reflevel.toFixed(2)} dB`],
        ['Scale',              `${preset.scale.toFixed(2)} dB/div`],
        ['Trace ref position', `${preset.trace_refpos.toFixed(2)} dB`],
        ['Trace scale',        `${preset.trace_scale.toFixed(2)} dB/div`],
        ['Attenuation',        preset.attenuate_x2 !== 0 ? `${(preset.attenuate_x2 / 2).toFixed(1)} dB` : 'None'],
        ['External gain',      `${preset.external_gain.toFixed(2)} dB`],
        ['Auto ref level',     preset.auto_reflevel    ? 'Yes' : 'No'],
        ['Auto attenuation',   preset.auto_attenuation ? 'Yes' : 'No'],
    ];

    // ── Traces ──────────────────────────────────────────────────────────────────
    const activeTraces = [];
    for (let i = 0; i < TRACES_MAX; i++) {
        if (preset.traces & (1 << i)) activeTraces.push(i + 1);
    }

    let tracesHtml;
    if (activeTraces.length > 0) {
        let rows = '<thead><tr><th>Trace</th><th>Average</th><th>Subtract</th><th>Stored</th><th>Normalized</th></tr></thead><tbody>';
        for (let i = 0; i < TRACES_MAX; i++) {
            if (preset.traces & (1 << i)) {
                rows += `<tr>
<td>${i + 1}</td>
<td>${preset.average[i]}</td>
<td>${preset.subtract[i]}</td>
<td>${preset.stored[i]     ? 'Yes' : 'No'}</td>
<td>${preset.normalized[i] ? 'Yes' : 'No'}</td>
</tr>`;
            }
        }
        tracesHtml = `<p>Active: ${activeTraces.join(', ')}</p><table>${rows}</tbody></table>`;
    } else {
        tracesHtml = '<p class="empty">No active traces</p>';
    }

    // ── Markers ─────────────────────────────────────────────────────────────────
    const enabledMarkers = preset.markers.filter(m => m.enabled);
    let markersHtml;
    if (enabledMarkers.length > 0) {
        let rows = '<thead><tr><th>#</th><th>Type</th><th>Trace</th><th>Frequency</th></tr></thead><tbody>';
        for (let i = 0; i < MARKERS_MAX; i++) {
            const m = preset.markers[i];
            if (m.enabled) {
                rows += `<tr>
<td>${i + 1}</td>
<td>${esc(markerTypeName(m.mtype))}</td>
<td>${m.trace + 1}</td>
<td>${esc(formatFreq(m.frequency))}</td>
</tr>`;
            }
        }
        markersHtml = `<table>${rows}</tbody></table>`;
    } else {
        markersHtml = '<p class="empty">No active markers</p>';
    }

    // ── Bands ───────────────────────────────────────────────────────────────────
    const enabledBands = preset.bands.filter(b => b.enabled);
    let bandsHtml;
    if (enabledBands.length > 0) {
        let rows = '<thead><tr><th>Name</th><th>Start</th><th>Stop</th><th>Level</th></tr></thead><tbody>';
        for (const b of enabledBands) {
            rows += `<tr>
<td>${esc(b.name || '—')}</td>
<td>${esc(formatFreq(b.start))}</td>
<td>${esc(formatFreq(b.end))}</td>
<td>${esc(b.level.toFixed(2))} dB</td>
</tr>`;
        }
        bandsHtml = `<table>${rows}</tbody></table>`;
    } else {
        bandsHtml = '<p class="empty">No active bands</p>';
    }

    // ── Limits ──────────────────────────────────────────────────────────────────
    let hasLimits = false;
    let limitRows = '<thead><tr><th>Limit</th><th>Ref</th><th>Frequency</th><th>Level</th></tr></thead><tbody>';
    for (let i = 0; i < LIMITS_MAX; i++) {
        for (let j = 0; j < REFERENCE_MAX; j++) {
            const l = preset.limits[i][j];
            if (l.enabled) {
                hasLimits = true;
                limitRows += `<tr>
<td>${i + 1}</td>
<td>${j + 1}</td>
<td>${esc(formatFreq(l.frequency))}</td>
<td>${esc(l.level.toFixed(2))} dB</td>
</tr>`;
            }
        }
    }
    const limitsHtml = hasLimits
        ? `<table>${limitRows}</tbody></table>`
        : '<p class="empty">No active limits</p>';

    // ── Sweep & trigger ─────────────────────────────────────────────────────────
    const sweepRows = [
        ['Trigger',           enumName(TRIGGER_NAMES, preset.trigger)],
        ['Trigger mode',      enumName(TRIGGER_NAMES, preset.trigger_mode)],
        ['Trigger direction', enumName(TRIGGER_NAMES, preset.trigger_direction)],
        ['Trigger level',     `${preset.trigger_level.toFixed(2)} dB`],
        ['Trigger beep',      preset.trigger_beep      ? 'Yes' : 'No'],
        ['Trigger auto save', preset.trigger_auto_save ? 'Yes' : 'No'],
        ['Repeat',            preset.repeat],
        ['Step delay mode',   enumName(SD_NAMES, preset.step_delay_mode)],
        ['Step delay',        preset.step_delay > 0 ? `${preset.step_delay} μs` : 'None'],
        ['Sweep time',        formatTime(preset.actual_sweep_time_us)],
    ];

    // ── Signal path ─────────────────────────────────────────────────────────────
    const signalRows = [
        ['AGC',            enumName(S_NAMES, preset.agc)],
        ['LNA',            enumName(S_NAMES, preset.lna)],
        ['Extra LNA',      preset.extra_lna     ? 'Yes' : 'No'],
        ['Below IF',       enumName(S_NAMES, preset.below_if)],
        ['Spur removal',   enumName(S_NAMES, preset.spur_removal)],
        ['Mirror masking', preset.mirror_masking ? 'Yes' : 'No'],
        ['Auto IF',        preset.auto_if        ? 'Yes' : 'No'],
        ['Mixer output',   preset.mixer_output   ? 'On' : 'Off'],
        ['Mute',           preset.mute           ? 'Yes' : 'No'],
    ];

    // ── Modulation ──────────────────────────────────────────────────────────────
    const modRows = [
        ['Type',      enumName(MODULATION_NAMES, preset.modulation)],
        ['Frequency', `${preset.modulation_frequency.toFixed(1)} Hz`],
        ['Depth',     `${(preset.modulation_depth_x100 / 100).toFixed(2)}`],
        ['Deviation', `${(preset.modulation_deviation_div100 * 100).toFixed(0)} Hz`],
        ['Level',     `${preset.level.toFixed(2)} dBm`],
        ['Level sweep', `${preset.level_sweep.toFixed(2)} dB`],
    ];

    // ── Advanced ────────────────────────────────────────────────────────────────
    const advRows = [
        ['Tracking',          preset.tracking],
        ['Tracking output',   preset.tracking_output ? 'Yes' : 'No'],
        ['Trigger trace',     preset.trigger_trace < 255 ? preset.trigger_trace + 1 : 'None'],
        ['Active marker',     preset.active_marker >= 0 ? preset.active_marker + 1 : 'None'],
        ['Refer',             preset.refer >= 0 ? preset.refer + 1 : 'None'],
        ['Normalized trace',  preset.normalized_trace >= 0 ? preset.normalized_trace + 1 : 'None'],
        ['Normalize level',   `${preset.normalize_level.toFixed(2)} dB`],
        ['Unit scale index',  preset.unit_scale_index],
        ['Unit scale',        preset.unit_scale.toFixed(4)],
        ['Decay',             preset.decay],
        ['Attack',            preset.attack],
        ['Noise floor',       preset.noise],
        ['LO drive',          preset.lo_drive],
        ['RX drive',          preset.rx_drive],
        ['Harmonic',          preset.harmonic],
        ['Fast speedup',      preset.fast_speedup],
        ['Faster speedup',    preset.faster_speedup],
        ['Draw line',         preset.draw_line    ? 'Yes' : 'No'],
        ['Lock display',      preset.lock_display ? 'Yes' : 'No'],
        ['Jog jump',          preset.jog_jump],
        ['Listen',            preset.listen       ? 'Yes' : 'No'],
        ['Sweep',             preset.sweep        ? 'Yes' : 'No'],
        ['Pulse',             preset.pulse        ? 'Yes' : 'No'],
        ['Atten step',        preset.atten_step],
        ['Disable correction', preset.disable_correction ? 'Yes' : 'No'],
        ['Linearity step',    preset.linearity_step],
        ['Freq mode',         preset.freq_mode],
        ['Offset delay',      preset.offset_delay > 0 ? `${preset.offset_delay} μs` : 'None'],
        ['Slider position',   preset.slider_position],
        ['Slider span',       formatFreq(preset.slider_span)],
        ['Frequency variable', formatFreq(preset.frequency_var)],
        ['Trigger grid',      preset.trigger_grid],
        ['Level meter',       preset.level_meter  ? 'Yes' : 'No'],
        ['Ultra',             preset.ultra        ? 'Yes' : 'No'],
        ['Increased R',       preset.increased_r  ? 'Yes' : 'No'],
        ['R',                 preset.r],
        ['Exp aver',          preset.exp_aver],
        ['dBuV',              preset.dbuv         ? 'Yes' : 'No'],
        ['Interval',          preset.interval > 0 ? `${preset.interval} ms` : 'None'],
        ['Additional step delay', formatTime(preset.additional_step_delay_us)],
        ['Measure sweep time',    formatTime(preset.measure_sweep_time_us)],
        ['Test',              preset.test],
        ['Test argument',     preset.test_argument],
    ];

    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline';">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>${esc(name)} — tinySA Preset</title>
<style>
*, *::before, *::after { box-sizing: border-box; }

body {
    font-family: var(--vscode-font-family, sans-serif);
    font-size: var(--vscode-font-size, 13px);
    color: var(--vscode-editor-foreground, #d4d4d4);
    background: var(--vscode-editor-background, #1e1e1e);
    margin: 0;
    padding: 16px 20px 32px;
}

.file-header {
    margin-bottom: 20px;
    padding-bottom: 14px;
    border-bottom: 1px solid var(--vscode-panel-border, #454545);
}
.file-header h1 {
    margin: 0 0 4px;
    font-size: 1.25em;
    font-weight: 600;
    color: var(--vscode-editor-foreground, #d4d4d4);
}
.file-header .subtitle {
    font-size: 0.85em;
    color: var(--vscode-descriptionForeground, #888);
}

details { margin-bottom: 8px; }
details > summary {
    cursor: pointer;
    list-style: none;
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 5px 0;
    user-select: none;
}
details > summary::-webkit-details-marker { display: none; }
details > summary::before {
    content: '▶';
    font-size: 0.65em;
    color: var(--vscode-descriptionForeground, #888);
    transition: transform 0.15s;
    display: inline-block;
    width: 1em;
    text-align: center;
    flex-shrink: 0;
}
details[open] > summary::before { transform: rotate(90deg); }
.section-title {
    font-weight: 600;
    font-size: 0.8em;
    text-transform: uppercase;
    letter-spacing: 0.07em;
    color: var(--vscode-sideBarSectionHeader-foreground, #bbb);
}
.section-content {
    padding: 4px 0 8px 18px;
}

p { margin: 0 0 8px; }
p.empty {
    color: var(--vscode-descriptionForeground, #888);
    font-style: italic;
}

table { border-collapse: collapse; margin-bottom: 6px; }
table th, table td {
    padding: 3px 14px 3px 0;
    text-align: left;
    vertical-align: top;
    white-space: nowrap;
}
table tbody tr:hover td,
table tbody tr:hover th {
    background: var(--vscode-list-hoverBackground, rgba(255,255,255,0.05));
}

/* Key-value table */
table.kv th {
    color: var(--vscode-descriptionForeground, #888);
    font-weight: normal;
    min-width: 160px;
}

/* Data table (markers, bands, limits) */
table:not(.kv) thead th {
    border-bottom: 1px solid var(--vscode-panel-border, #454545);
    color: var(--vscode-descriptionForeground, #888);
    font-weight: 600;
    padding-bottom: 4px;
    margin-bottom: 2px;
}
</style>
</head>
<body>
<div class="file-header">
  <h1>tinySA Ultra Preset: ${esc(name || '(unnamed)')}</h1>
  <div class="subtitle">${esc(filename)}</div>
</div>

${section('Overview',       kvTable(overviewRows))}
${section('Frequency',      kvTable(freqRows))}
${section('Level',          kvTable(levelRows))}
${section('Traces',         tracesHtml)}
${section('Markers',        markersHtml)}
${section('Bands',          bandsHtml)}
${section('Limits',         limitsHtml)}
${section('Sweep & Trigger', kvTable(sweepRows))}
${section('Signal Path',    kvTable(signalRows))}
${section('Modulation',     kvTable(modRows))}
${section('Advanced',       kvTable(advRows), true)}

</body>
</html>`;
}

// ── Error page ─────────────────────────────────────────────────────────────────

function getErrorHtml(message, filename) {
    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline';">
<style>
*, *::before, *::after { box-sizing: border-box; }
body {
    font-family: var(--vscode-font-family, sans-serif);
    font-size: var(--vscode-font-size, 13px);
    color: var(--vscode-editor-foreground, #d4d4d4);
    background: var(--vscode-editor-background, #1e1e1e);
    padding: 20px;
}
h1 { color: var(--vscode-editorError-foreground, #f88); margin-top: 0; }
pre {
    background: var(--vscode-textBlockQuote-background, #2a2a2a);
    border-left: 3px solid var(--vscode-editorError-foreground, #f88);
    padding: 10px 14px;
    white-space: pre-wrap;
    word-break: break-word;
    border-radius: 2px;
}
.subtitle { color: var(--vscode-descriptionForeground, #888); margin-bottom: 12px; }
</style>
</head>
<body>
<h1>Failed to parse preset file</h1>
<div class="subtitle">${esc(filename)}</div>
<pre>${esc(message)}</pre>
</body>
</html>`;
}

// ── VS Code custom editor ──────────────────────────────────────────────────────

class PresetDocument {
    constructor(uri, preset, error) {
        this.uri    = uri;
        this.preset = preset;
        this.error  = error;
    }
    dispose() {}
}

class PresetViewerProvider {
    async openCustomDocument(uri, _openContext, _token) {
        let preset = null;
        let error = null;
        try {
            const data = await vscode.workspace.fs.readFile(uri);
            preset = parsePreset(data);
        } catch (e) {
            error = e.message;
        }
        return new PresetDocument(uri, preset, error);
    }

    resolveCustomEditor(document, webviewPanel, _token) {
        webviewPanel.webview.options = { enableScripts: false };
        const filename = document.uri.path.split('/').pop() || document.uri.fsPath.split(/[\\/]/).pop();
        if (document.preset) {
            webviewPanel.webview.html = getWebviewHtml(document.preset, filename);
        } else {
            webviewPanel.webview.html = getErrorHtml(document.error || 'Unknown error', filename);
        }
    }
}

// ── Extension entry points ─────────────────────────────────────────────────────

function activate(context) {
    const provider = new PresetViewerProvider();
    const registration = vscode.window.registerCustomEditorProvider(
        'tinysa4preset-viewer.presetViewer',
        provider,
        { webviewOptions: { retainContextWhenHidden: true } }
    );
    context.subscriptions.push(registration);
}

function deactivate() {}

module.exports = { activate, deactivate };
