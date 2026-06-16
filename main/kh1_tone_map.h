#pragma once

#include <cmath>

static inline int kh1_fo_from_delta_hz(float delta_hz) {
    if (!std::isfinite(delta_hz) || delta_hz <= 0.0f) return 0;

    int fo = (int)std::floor(delta_hz + 0.5f);
    if (fo < 0) return 0;
    if (fo > 98) return 98;
    return fo;
}

static inline int kh1_fo_from_tone_hz(float tone_hz, float tx_base_hz) {
    return kh1_fo_from_delta_hz(tone_hz - tx_base_hz);
}
