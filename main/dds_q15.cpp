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

// FT8 CPFSK schedule. Active when s_ft8_active = true; render walks the
// message at sample-precise symbol boundaries. Pre-computed incs
// avoid per-symbol float math in the render path.
static uint64_t s_ft8_incs[DDS_FT8_NUM_SYMBOLS];
static uint32_t s_ft8_sample_counter = 0;
static volatile bool s_ft8_active = false;

static inline uint64_t inc_from_hz(double f_hz) {
    long double num = static_cast<long double>(f_hz)
                    * static_cast<long double>(1ULL << 63) * 2.0L;
    long double den = static_cast<long double>(DDS_FS_HZ);
    return static_cast<uint64_t>(llroundl(num / den));
}

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
    s_inc.store(inc_from_hz(f_hz), std::memory_order_relaxed);
}

void dds_reset_phase(void) {
    s_phase = 0;
}

void dds_ft8_begin(double base_hz, const uint8_t* symbols) {
    // Pre-compute symbol -> phase-increment map. base + 6.25 * tone for
    // each of the 79 FT8 symbols. Caller's array can go out of scope
    // after this returns; we've copied everything into our static buf.
    for (unsigned i = 0; i < DDS_FT8_NUM_SYMBOLS; ++i) {
        double f = base_hz + 6.25 * static_cast<double>(symbols[i]);
        s_ft8_incs[i] = inc_from_hz(f);
    }
    // Continuous-phase from this point: phase=0 at the very first
    // sample of the message; subsequent symbol changes are smooth
    // because we never reset phase mid-render.
    s_phase = 0;
    s_ft8_sample_counter = 0;
    s_inc.store(s_ft8_incs[0], std::memory_order_relaxed);
    s_ft8_active = true;
}

void dds_ft8_end(void) {
    s_ft8_active = false;
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

// Pack one Q15 sample into a stereo 24-bit frame at full scale.
// Q15 << 8 lands in Q1.23 range. Mono duplicated to L+R.
// QDX/QMX manual specifies full-scale audio input; the -6 dB
// convention from mcu-ft8 was a two-tone-summing headroom hack
// that doesn't apply to single-tone CPFSK.
static inline void emit_stereo_frame(uint8_t* out, int16_t s_q15) {
    int32_t v24 = static_cast<int32_t>(s_q15) << 8;
    uint8_t b0 = static_cast<uint8_t>(v24 & 0xFF);
    uint8_t b1 = static_cast<uint8_t>((v24 >> 8) & 0xFF);
    uint8_t b2 = static_cast<uint8_t>((v24 >> 16) & 0xFF);
    out[0] = b0; out[1] = b1; out[2] = b2;   // L
    out[3] = b0; out[4] = b1; out[5] = b2;   // R
}

void dds_render_24bit_stereo(uint8_t* out, unsigned frames) {
    uint64_t phase = s_phase;

    if (s_ft8_active) {
        // CPFSK mode: walk symbols at sample-precise boundaries. Loop
        // is bounded by symbol size so per-sample div/mod is amortized.
        const uint32_t total_samples =
            DDS_FT8_NUM_SYMBOLS * DDS_FT8_SYMBOL_SAMPLES;
        uint32_t counter = s_ft8_sample_counter;

        while (frames > 0) {
            // Past end of message — emit silence for the rest.
            if (counter >= total_samples) {
                while (frames-- > 0) {
                    out[0] = out[1] = out[2] = 0;
                    out[3] = out[4] = out[5] = 0;
                    out += 6;
                }
                break;
            }

            // Slice this iteration to the smaller of: frames remaining
            // in caller's request, samples left in current symbol.
            uint32_t sym_idx     = counter / DDS_FT8_SYMBOL_SAMPLES;
            uint32_t pos_in_sym  = counter - sym_idx * DDS_FT8_SYMBOL_SAMPLES;
            uint32_t left_in_sym = DDS_FT8_SYMBOL_SAMPLES - pos_in_sym;
            unsigned n = (frames < left_in_sym) ? frames
                                                : static_cast<unsigned>(left_in_sym);
            uint64_t inc = s_ft8_incs[sym_idx];

            // Mirror the picked inc to s_inc so external observers see
            // the "current" tone (not strictly necessary; cheap).
            s_inc.store(inc, std::memory_order_relaxed);

            for (unsigned i = 0; i < n; ++i) {
                emit_stereo_frame(out, sin_q15_from_phase(phase));
                out += 6;
                phase += inc;
            }
            counter += n;
            frames  -= n;
        }
        s_ft8_sample_counter = counter;
        s_phase = phase;
        return;
    }

    // Single-tone mode (test bench / default). Snapshot inc once per
    // call; symbol-rate updates here have block-granular jitter, but
    // the test bench doesn't care.
    uint64_t inc = s_inc.load(std::memory_order_relaxed);
    for (unsigned n = 0; n < frames; ++n) {
        emit_stereo_frame(out, sin_q15_from_phase(phase));
        out += 6;
        phase += inc;
    }
    s_phase = phase;
}
