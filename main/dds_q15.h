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

// Initialize the quarter-wave Q15 sine LUT. Call once at boot.
void dds_init(void);

// Set NCO frequency. Phase is preserved across calls (continuous-phase
// FSK suitable for FT8 symbol transitions). Safe to call from any
// task — uses an atomic 64-bit store for the increment.
void dds_set_freq_hz(double f_hz);

// Reset phase accumulator to zero (e.g., at the start of a TX slot).
void dds_reset_phase(void);

// Render `frames` stereo frames as 24-bit packed little-endian samples
// (3 bytes per channel × 2 channels = 6 bytes per frame). `out` must
// point to at least frames*6 bytes. Mono synthesis is duplicated to
// L and R. ISR-safe — integer math only in the hot loop.
void dds_render_24bit_stereo(uint8_t* out, unsigned frames);

#ifdef __cplusplus
}
#endif
