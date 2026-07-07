---
phase: 03-cat-command-implementation
reviewed: 2026-07-06T00:00:00Z
depth: standard
files_reviewed: 2
files_reviewed_list:
  - main/stream_uac.cpp
  - main/radio_control_ftx1.cpp
findings:
  critical: 1
  warning: 5
  info: 3
  total: 9
status: issues_found
---

# Phase 3: Code Review Report

**Reviewed:** 2026-07-06
**Depth:** standard
**Files Reviewed:** 2
**Status:** issues_found

## Summary

Reviewed the FTX-1 CAT command backend (`radio_control_ftx1.cpp`) and the CP210x CAT-1 open/gap-closure changes in `stream_uac.cpp`. The CAT command sequencing itself (FA/MD/TX1/TX0, frequency-only RX restore) is implemented consistently with the existing QDX backend pattern and matches the documented SYNC-02/SYNC-03/PTT-02/CAT-03 invariants. However, the CP210x port bring-up path silently tolerates line-coding/control-line configuration failures and then marks the CAT link "ready" regardless, which risks sending CAT bytes at the wrong baud framing to a physical transmitter with no way to detect or recover. There is also a real (if currently low-probability) cross-core race on the shared CDC-open state, an RX buffer that is provisioned but never actually consumed (no `data_cb`), and an unguarded zero-frequency restore path. None of these are related to the sdkconfig hub-support change, which was out of scope per instructions.

## Critical Issues

### CR-01: CP210x line-coding/control-line failures are logged but not acted on, then CAT link is marked ready anyway

**File:** `main/stream_uac.cpp:301-331`
**Issue:** In `cp210x_try_open()`, after `cp210x_vcp_open()` succeeds, both `cdc_acm_host_set_control_line_state()` and `cdc_acm_host_line_coding_set()` results are only logged (`ESP_LOGI(..., esp_err_to_name(err))`) — neither failure aborts the open, closes the handle, or prevents `s_cdc_handle = handle;` / `g_cdc_initial_sync_pending = true;` from executing on the next lines. If the CP2105 firmware rejects or silently ignores the 38400 8N1 line-coding request (e.g. transient USB control-transfer failure during hub enumeration, which this same gap-closure fix acknowledges is fragile), the port is left running at its power-on-default baud/framing while `cat_cdc_ready()` reports `true` and every subsequent `FA…;`/`MD0C;`/`TX1;`/`TX0;` command is transmitted as garbage bytes to the radio. There is no retry, no re-assert, and no user-visible failure state — the operator will see "connected" while CAT is actually non-functional or, worse, intermittently misinterpreted by the rig.
**Fix:**
```cpp
esp_err_t line_err = cdc_acm_host_set_control_line_state(handle, false, false);
esp_err_t lc_err = cdc_acm_host_line_coding_set(handle, &line_coding);
if (line_err != ESP_OK || lc_err != ESP_OK) {
    ESP_LOGE(TAG, "CP210x CAT-1 setup failed (line=%s coding=%s); aborting open",
             esp_err_to_name(line_err), esp_err_to_name(lc_err));
    cdc_acm_host_close(handle);
    return;   // leave s_cdc_handle NULL so cp210x_try_open() retries on the next call
}
s_cdc_handle = handle;
...
```

## Warnings

### WR-01: Cross-core race on `s_cdc_handle` / `s_cdc_last_attempt_ms` / `s_cdc_iface`

**File:** `main/stream_uac.cpp:283-332, 890-898`
**Issue:** `cp210x_try_open()` (and `cdc_try_open()`) can be invoked from `uac_lib_task` (pinned to core 0, on `UAC_HOST_DRIVER_EVENT_RX_CONNECTED`) and from `uac_on_block_processed()` (called from `stream_uac_task`, pinned to core 1, once mic streaming starts) with no mutex around the shared statics. The throttle check `if (s_cdc_last_attempt_ms != 0 && (now_ms - s_cdc_last_attempt_ms) < 1000) return;` followed later by `s_cdc_last_attempt_ms = now_ms;` is a classic check-then-set race: two cores can both pass the throttle check before either updates the timestamp, causing two concurrent `cp210x_vcp_open()`/`cdc_acm_host_open()` calls against the same interface. Today this is mitigated in practice because the FTX-1 mic negotiation is documented to always fail (see WR-02), so `stream_uac_task` never actually starts and the core-1 call site is dormant — but that is an accident of the current 24-bit-only candidate list, not a guarantee, and the QMX path already exercises both call sites concurrently.
**Fix:** Guard the shared CDC-open state with a lightweight spinlock/mutex (e.g. `portMUX_TYPE` critical section around the read-modify-write of `s_cdc_last_attempt_ms`/`s_cdc_handle`), or restrict the "retry from audio task" call site to post an event to the single owning task instead of calling `cp210x_try_open()`/`cdc_try_open()` directly from a different core.

### WR-02: FTX-1 mic format negotiation is guaranteed to fail with the current candidate list, silently disabling RX decode

**File:** `main/stream_uac.cpp:589-596`
**Issue:** For `s_profile != UAC_PROFILE_GENERIC_USB` (which includes `UAC_PROFILE_FTX1`), the only stream candidate tried is `add_candidate(UAC_CHANNELS, UAC_BIT_RESOLUTION)` (24-bit). The comment added in this same change explicitly states "the FTX-1's mic-only advertises 16-bit while this profile's sole candidate is 24-bit, so negotiation reliably fails on real hardware." The consequence is that `started` stays `false`, `uac_host_device_close(handle)` is called, `s_mic_handle` is never set, and the FT8/FT4 audio pipeline task is never started for FTX-1 — RX decode will never work for this radio even though CAT-1 now opens successfully. If this is intentionally deferred to a later phase, it should be tracked explicitly rather than left as a silent dead end; as shipped, the UI will show CAT connected while decode silently never begins.
**Fix:** Add a 16-bit stereo candidate for `UAC_PROFILE_FTX1` (mirroring the `UAC_PROFILE_GENERIC_USB` fallback list), or explicitly document/track this as a known incomplete-feature gap for the current milestone.

### WR-03: `in_buffer_size = 256` for FTX-1 CAT-1 is provisioned but never consumed (no `data_cb`)

**File:** `main/stream_uac.cpp:292-299`
**Issue:** `dev_cfg.in_buffer_size = 256` with the comment "FTX-1 needs RX for query replies (Phase 3)", but `dev_cfg.data_cb = NULL`. Tracing into `cdc_acm_host.c`'s `in_xfer_cb()`, when `cdc_dev->data.in_cb` is NULL the received bytes are never inspected — the transfer is simply resubmitted with the same buffer/offset, so any reply bytes from the radio are silently overwritten and discarded on every poll cycle. No code path in `radio_control_ftx1.cpp` reads or waits for a CAT reply either. The RX buffer allocation and its comment currently describe functionality that does not exist.
**Fix:** Either register a `data_cb` and add reply-parsing to the FTX-1 backend now, or drop `in_buffer_size` to 0 and update the comment to reflect that query-reply handling is deferred, so the configuration doesn't imply working functionality that isn't there.

### WR-04: `ftx1_restore_rx_state()` can send an invalid all-zero frequency if called before any sync

**File:** `main/radio_control_ftx1.cpp:66-75`
**Issue:** `s_rx_freq_hz` is a static initialized to `0`. `ftx1_restore_rx_state()` (called from `ftx1_end_tx()`) formats it unconditionally: `snprintf(fa, sizeof(fa), "FA%09d;", s_rx_freq_hz);`. Unlike `ftx1_sync_frequency_mode()`, there is no range clamp here. If `end_tx`/`set_tune(false, ...)` is ever reached before `sync_frequency_mode` has run at least once (e.g. a stray TX abort right after CAT connects, or a defensive/error-recovery call site added later), the radio receives `FA000000000;`, an out-of-band frequency command, silently.
**Fix:**
```cpp
static esp_err_t ftx1_restore_rx_state(void) {
    if (s_rx_freq_hz < 30000) {
        ESP_LOGW(TAG, "FTX-1 restore skipped: no valid synced frequency yet");
        return ESP_OK;
    }
    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", s_rx_freq_hz);
    return ftx1_send_cmd(fa, 200);
}
```

### WR-05: CAT command timeout is a repeated magic number with no shared constant

**File:** `main/radio_control_ftx1.cpp:37, 42, 53, 74, 82, 97`
**Issue:** The literal `200` (ms) is repeated six times across every `ftx1_send_cmd(...)` call site as the CAT command timeout. This mirrors the pre-existing QDX backend, but makes it easy for a future edit to change the timeout in some call sites and not others without anyone noticing, and gives no single place to tune CAT latency behavior for the FTX-1 specifically.
**Fix:** `static constexpr uint32_t kCatTimeoutMs = 200;` at file scope and use it at each call site.

## Info

### IN-01: `uac_qmx_detected()` name no longer matches its usage

**File:** `main/stream_uac.cpp:975-977`
**Issue:** `uac_qmx_detected()` checks `s_mic_handle != NULL || s_cdc_handle != NULL` generically for whatever profile is active (QMX, generic USB, or FTX-1), but its name still implies QMX-only detection. This is confusing for future maintainers reading call sites for the FTX-1 path.
**Fix:** Rename to something profile-neutral, e.g. `uac_device_detected()`, updating call sites accordingly.

### IN-02: Duplicated `"FA%09d;"` formatting logic

**File:** `main/radio_control_ftx1.cpp:35-36, 72-73`
**Issue:** The 9-digit frequency command format is duplicated between `ftx1_sync_frequency_mode()` and `ftx1_restore_rx_state()`. Combined with WR-04's missing clamp in the second copy, this duplication is exactly the kind of place where the two copies silently drift.
**Fix:** Extract a small `static void ftx1_format_fa(char* buf, size_t len, int freq_hz)` helper (or have `ftx1_restore_rx_state()` call `ftx1_sync_frequency_mode(s_rx_freq_hz)` directly, re-applying the clamp and — intentionally, per the SYNC-02 comment — re-sending `MD0C;` only if that's acceptable).

### IN-03: `ftx1_begin_tx`/`ftx1_set_tune` silently ignore `freq_hz`/`tx_base_hz`/`tone_hz` parameters

**File:** `main/radio_control_ftx1.cpp:49-59, 90-102`
**Issue:** Both functions cast their positional parameters to `(void)` and never use them, matching the QDX backend's existing pattern. This is consistent with the codebase but worth a one-line comment (as `ftx1_set_tone_hz` already has) explaining that frequency is expected to already be synced via `sync_frequency_mode` before `begin_tx`/`set_tune` is called, so a future reader doesn't mistake this for an oversight.
**Fix:** Add a short comment, e.g. `// freq_hz already applied via sync_frequency_mode(); TX just keys the radio.`

---

_Reviewed: 2026-07-06_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
