#include <cmath>
#include <cstdio>
#include <limits>

#include "kh1_tone_map.h"

static bool expect_equal(const char* label, int actual, int expected) {
    if (actual == expected) {
        return true;
    }
    std::printf("FAIL %s: got FO%02d expected FO%02d\n", label, actual, expected);
    return false;
}

static bool test_ft8_mapping() {
    static const int expected[] = {0, 6, 13, 19, 25, 31, 38, 44};
    const float base_hz = 1500.0f;
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "FT8 tone %d", i);
        const float tone_hz = base_hz + 6.25f * (float)i;
        ok &= expect_equal(label, kh1_fo_from_tone_hz(tone_hz, base_hz), expected[i]);
    }
    return ok;
}

static bool test_ft4_mapping() {
    static const int expected[] = {0, 21, 42, 62};
    const float base_hz = 1500.0f;
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "FT4 tone %d", i);
        const float tone_hz = base_hz + 20.8333f * (float)i;
        const int fo = kh1_fo_from_tone_hz(tone_hz, base_hz);
        ok &= expect_equal(label, fo, expected[i]);
        if (fo == 99) {
            std::printf("FAIL %s: emitted reserved FO99\n", label);
            ok = false;
        }
    }
    return ok;
}

static bool test_clamping() {
    bool ok = true;
    ok &= expect_equal("negative delta", kh1_fo_from_delta_hz(-5.0f), 0);
    ok &= expect_equal("large delta", kh1_fo_from_delta_hz(140.0f), 98);
    ok &= expect_equal("reserved edge", kh1_fo_from_delta_hz(98.6f), 98);
    ok &= expect_equal("nan delta", kh1_fo_from_delta_hz(std::numeric_limits<float>::quiet_NaN()), 0);
    return ok;
}

int main() {
    bool ok = true;
    ok &= test_ft8_mapping();
    ok &= test_ft4_mapping();
    ok &= test_clamping();
    if (ok) {
        std::printf("KH1 tone map test passed\n");
        return 0;
    }
    return 1;
}
