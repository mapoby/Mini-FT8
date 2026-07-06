#include "radio_control_backend.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "stream_uac.h"

static const char* TAG = "RADIO_FTX1";
static int s_rx_freq_hz = 0;
static bool s_tx_active = false;

static bool ftx1_ready(void) {
    return cat_cdc_ready();
}

static esp_err_t ftx1_send_cmd(const char* cmd, uint32_t timeout_ms) {
    if (!cat_cdc_ready()) return ESP_ERR_INVALID_STATE;
    return cat_cdc_send(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), timeout_ms);
}

static esp_err_t ftx1_on_audio_start(void) {
    ESP_LOGI(TAG, "FTX-1 CAT backend initialized");
    return ESP_OK;
}

static esp_err_t ftx1_sync_frequency_mode(int freq_hz) {
    if (freq_hz < 30000 || freq_hz > 470000000) {
        ESP_LOGW(TAG, "FTX-1 sync freq=%d out of range, clamping", freq_hz);
        if (freq_hz < 30000) freq_hz = 30000;
        if (freq_hz > 470000000) freq_hz = 470000000;
    }
    s_rx_freq_hz = freq_hz;

    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", freq_hz);
    esp_err_t err = ftx1_send_cmd(fa, 200);
    if (err != ESP_OK) return err;

    // DATA-U mode is set once here at sync time only; never re-asserted
    // on the TX/end_tx path (SYNC-02).
    err = ftx1_send_cmd("MD0C;", 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "FTX-1 sync ok freq=%d mode=DATA-U", freq_hz);
    }
    return err;
}

static esp_err_t ftx1_begin_tx(int freq_hz, int tx_base_hz) {
    (void)freq_hz;
    (void)tx_base_hz;

    esp_err_t err = ftx1_send_cmd("TX1;", 200);
    if (err == ESP_OK) {
        s_tx_active = true;
        ESP_LOGI(TAG, "FTX-1 TX start");
    }
    return err;
}

static esp_err_t ftx1_set_tone_hz(float tone_hz) {
    (void)tone_hz;
    return ESP_OK;   // TX tone rides UAC OUT (Phase 4); no per-symbol CAT tone command
}

static esp_err_t ftx1_restore_rx_state(void) {
    // Frequency-only restore (SYNC-03). DATA-U mode is set once at sync and
    // never changes during a TX cycle, so it needs no restore here. A
    // defensive DATA-U re-assert against possible mode drift was considered
    // (03-RESEARCH.md Assumptions Log A4) but is intentionally deferred out
    // of scope for this phase.
    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", s_rx_freq_hz);
    return ftx1_send_cmd(fa, 200);
}

static esp_err_t ftx1_end_tx(void) {
    if (!s_tx_active) return ESP_OK;

    uac_tx_end();   // PTT-02: drain audio before unkeying

    esp_err_t err = ftx1_send_cmd("TX0;", 200);
    s_tx_active = false;
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "FTX-1 TX stop");
    return ftx1_restore_rx_state();
}

static esp_err_t ftx1_set_tune(bool enable, int freq_hz, int tone_hz) {
    (void)tone_hz;
    if (!enable) return ftx1_end_tx();

    esp_err_t err = ftx1_sync_frequency_mode(freq_hz);
    if (err != ESP_OK) return err;

    err = ftx1_send_cmd("TX1;", 200);
    if (err == ESP_OK) {
        s_tx_active = true;
    }
    return err;
}

static esp_err_t ftx1_set_time(int hour, int minute, int second) {
    (void)hour;
    (void)minute;
    (void)second;
    return ESP_ERR_NOT_SUPPORTED;
}

static const radio_control_ops_t k_ops = {
    .name = "ftx1",
    .ready = ftx1_ready,
    .on_audio_start = ftx1_on_audio_start,
    .sync_frequency_mode = ftx1_sync_frequency_mode,
    .begin_tx = ftx1_begin_tx,
    .set_tone_hz = ftx1_set_tone_hz,
    .end_tx = ftx1_end_tx,
    .set_tune = ftx1_set_tune,
    .set_time = ftx1_set_time,
};

const radio_control_ops_t* radio_control_ftx1_get_ops(void) {
    return &k_ops;
}
