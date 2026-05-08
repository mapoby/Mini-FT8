// Single-tone Q15 NCO for UAC OUT FT8 audio synthesis.
//
// Design: 64-bit phase accumulator + 256-entry quarter-wave Q15 sine LUT
// + 10-bit linear interpolation. Output is 24-bit signed PCM, packed
// 3 bytes/channel little-endian, mono-duplicated to L+R for stereo
// USB UAC streaming. Integer-only render path.
//
// Memory footprint:
//   .rodata / .bss : 514 B (257 × int16_t quarter-wave LUT)
//   per-instance   : 16 B  (phase + inc, both uint64_t)
//
// Adapted from ~/mcu-ft8/Core/Src/dds_two_tone_q15.c — single-tone
// variant (FT8 transmits one tone at a time so the second NCO from
// the original two-tone API is dropped).
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Output sample rate. UAC stream is 48 kHz; if that ever changes the
// phase increment math has to be recomputed.
#define DDS_FS_HZ 48000

// FT8 modulation parameters (8-CPFSK at 6.25 Hz spacing, 79 symbols
// of 160 ms each at 48 kHz = 7680 samples per symbol).
#define DDS_FT8_NUM_SYMBOLS    79u
#define DDS_FT8_SYMBOL_SAMPLES 7680u

// Initialize the quarter-wave Q15 sine LUT. Call once at boot.
void dds_init(void);

// Single-tone NCO control. Used by the test bench (fixed 1.5 kHz
// validation). Phase is preserved across calls (continuous-phase FSK
// suitable for ad-hoc symbol transitions if you really want to drive
// it that way). Safe to call from any task.
void dds_set_freq_hz(double f_hz);
void dds_reset_phase(void);

// Begin an FT8 8-CPFSK transmission. Pre-computes phase increments for
// all 79 symbols (base_hz + symbols[i] * 6.25 Hz) and seeds the NCO
// with the first symbol's frequency at phase 0. While "active", every
// dds_render_24bit_stereo call walks the message at sample-precise
// symbol boundaries with continuous phase across them. After the 79
// symbols (~12.64 s), render outputs silence.
//
// The `symbols` pointer is read once and copied internally — caller
// does NOT need to keep the array alive after this call returns.
//
// This matches the working mcu-ft8 single-message FT8 path on QMX
// (commit 2eb3c67 "Implement FT8 8-CPFSK modulation with symbol
// tracking"), where wall-clock symbol scheduling at the application
// layer was replaced by sample-counted scheduling at the audio fill
// layer to eliminate symbol-duration jitter.
void dds_ft8_begin(double base_hz, const uint8_t* symbols);

// End an FT8 transmission early or signal completion. Subsequent
// dds_render_24bit_stereo calls revert to single-tone mode using the
// last freq set via dds_set_freq_hz (which is 0 unless reset).
void dds_ft8_end(void);

// Render `frames` stereo frames as 24-bit packed little-endian samples
// (3 bytes per channel × 2 channels = 6 bytes per frame). `out` must
// point to at least frames*6 bytes. Mono synthesis is duplicated to
// L and R. Full-scale Q15 -> Q1.23 (peaks at ±8388352, ~0.003% under
// ±full-scale, negligible). Integer math in the hot loop.
void dds_render_24bit_stereo(uint8_t* out, unsigned frames);

#ifdef __cplusplus
}
#endif
