#include "radio_control_backend.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
// NOTE: do NOT include "stream_uac.h" in Phase 1 -- no CAT/UAC I/O yet (Phase 2 dependency, CAT-01)

static const char* TAG = "RADIO_FTX1";

static bool ftx1_ready(void) {
    ESP_LOGI(TAG, "FTX-1 backend selected (stub, not yet implemented)");
    return false;  // stub pending Phase 2 hardware bring-up
}

static esp_err_t ftx1_on_audio_start(void) {
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t ftx1_sync_frequency_mode(int freq_hz) {
    (void)freq_hz;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t ftx1_begin_tx(int freq_hz, int tx_base_hz) {
    (void)freq_hz;
    (void)tx_base_hz;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t ftx1_set_tone_hz(float tone_hz) {
    (void)tone_hz;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t ftx1_end_tx(void) {
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t ftx1_set_tune(bool enable, int freq_hz, int tone_hz) {
    (void)enable;
    (void)freq_hz;
    (void)tone_hz;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t ftx1_set_time(int hour, int minute, int second) {
    (void)hour;
    (void)minute;
    (void)second;
    return ESP_ERR_INVALID_STATE;
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
