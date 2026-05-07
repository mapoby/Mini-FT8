#include "dds_q15.h"

#include <atomic>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 256 entries per quarter (+ 1 endpoint = peak at pi/2)
static constexpr unsigned QTABLE_BITS = 8;
static constexpr unsigned FRAC_BITS   = 10;
static constexpr unsigned QTABLE_SIZE = 1u << QTABLE_BITS;
static constexpr unsigned VISIBLE_BITS = 2 + QTABLE_BITS + FRAC_BITS;  // 20
static constexpr unsigned PHASE_BITS = 64;

static int16_t s_sin_quarter[QTABLE_SIZE + 1];

// Phase accumulator (writer-only) and atomic frequency increment
// (writer reads, tx-task / scheduler updates).
static std::atomic<uint64_t> s_inc{0};
static uint64_t              s_phase = 0;

void dds_init(void) {
    const double step = (M_PI / 2.0) / static_cast<double>(QTABLE_SIZE);
    for (unsigned n = 0; n <= QTABLE_SIZE; ++n) {
        double v = sin(step * static_cast<double>(n));
        int32_t q = static_cast<int32_t>(lround(v * 32767.0));
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        s_sin_quarter[n] = static_cast<int16_t>(q);
    }
}

void dds_set_freq_hz(double f_hz) {
    // inc = round(f_hz / Fs * 2^64)
    long double num = static_cast<long double>(f_hz)
                    * static_cast<long double>(1ULL << 63) * 2.0L;
    long double den = static_cast<long double>(DDS_FS_HZ);
    uint64_t inc = static_cast<uint64_t>(llroundl(num / den));
    s_inc.store(inc, std::memory_order_relaxed);
}

void dds_reset_phase(void) {
    s_phase = 0;
}

// Sample sin(phase) from the quarter-wave LUT with linear interpolation.
// Returns Q1.15 in [-32768, 32767].
static inline int16_t sin_q15_from_phase(uint64_t phase) {
    uint32_t v = static_cast<uint32_t>(phase >> (PHASE_BITS - VISIBLE_BITS));
    uint32_t quad = v >> (QTABLE_BITS + FRAC_BITS);                    // 0..3
    uint32_t xf   = v & ((1u << (QTABLE_BITS + FRAC_BITS)) - 1u);
    uint32_t idx  = xf >> FRAC_BITS;
    uint32_t frac = xf & ((1u << FRAC_BITS) - 1u);

    // Mirror in odd quadrants (Q1, Q3): position = QTABLE_SIZE - position.
    if (quad & 1u) {
        idx  = QTABLE_SIZE - ((xf + ((1u << FRAC_BITS) - 1u)) >> FRAC_BITS);
        frac = (-frac) & ((1u << FRAC_BITS) - 1u);
    }

    int32_t y;
    if (idx >= QTABLE_SIZE) {
        y = s_sin_quarter[QTABLE_SIZE];
    } else {
        int32_t y0  = s_sin_quarter[idx];
        int32_t y1  = s_sin_quarter[idx + 1];
        int32_t dif = y1 - y0;
        int32_t acc = dif * static_cast<int32_t>(frac);
        y = y0 + ((acc + (1 << (FRAC_BITS - 1))) >> FRAC_BITS);
    }

    if (quad >= 2u) y = -y;
    return static_cast<int16_t>(y);
}

void dds_render_24bit_stereo(uint8_t* out, unsigned frames) {
    // Snapshot inc once per render block. Frequency updates between
    // blocks land cleanly (next block uses new freq); within a block
    // we keep one inc to avoid mid-block phase glitches.
    uint64_t inc = s_inc.load(std::memory_order_relaxed);
    uint64_t phase = s_phase;

    for (unsigned n = 0; n < frames; ++n) {
        int16_t s = sin_q15_from_phase(phase);
        // Q1.15 -> 24-bit signed: shift left 8 (full-scale Q15 -> Q1.23).
        // Range: -8388608 .. +8388352 (slightly under +full-scale by 1 LSB
        // because Q15 max is 32767, not 32768). Fine for audio use.
        int32_t v24 = static_cast<int32_t>(s) << 8;
        uint8_t b0 = static_cast<uint8_t>(v24 & 0xFF);
        uint8_t b1 = static_cast<uint8_t>((v24 >> 8) & 0xFF);
        uint8_t b2 = static_cast<uint8_t>((v24 >> 16) & 0xFF);
        out[0] = b0; out[1] = b1; out[2] = b2;   // L
        out[3] = b0; out[4] = b1; out[5] = b2;   // R
        out += 6;
        phase += inc;
    }
    s_phase = phase;
}
