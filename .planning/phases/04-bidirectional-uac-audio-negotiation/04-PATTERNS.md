# Phase 4: Bidirectional UAC Audio Negotiation - Pattern Map

**Mapped:** 2026-07-06
**Files analyzed:** 1 (modified, no new files)
**Analogs found:** 3 / 3 (all analogs are in-file — this phase edits the same file its patterns are copied from)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `main/stream_uac.cpp` — mic candidate-scan branch for `UAC_PROFILE_FTX1` (~line 594, inside `uac_lib_task()` `RX_CONNECTED` handler) | driver/event-handler (USB host callback branch) | event-driven | `UAC_PROFILE_GENERIC_USB` candidate-scan branch, same function, lines 589-593 | exact (same construct, same loop, only candidate values/order differ) |
| `main/stream_uac.cpp` — speaker guard widening (~line 688, inside `uac_lib_task()` `TX_CONNECTED` handler) | driver/event-handler (USB host callback branch) | event-driven | `UAC_PROFILE_QMX` speaker pre-open block, same function, lines 687-768 | exact (guard-only change; pre-open body reused verbatim) |
| `main/stream_uac.cpp` — FIFO partition guard widening (~line 413, inside `usb_lib_task()`) | config/init (USB host install-time partitioning) | batch (one-time boot-time config) | `UAC_PROFILE_QMX` custom FIFO branch, same function, lines 413-426 | exact (guard-only change; numeric split reused as starting point per D-06/D-07) |

No `stream_uac.h` changes expected — `UAC_PROFILE_FTX1` enum already exists (added Phase 2). No new files are created this phase.

## Pattern Assignments

### `main/stream_uac.cpp` — Mic (UAC IN) candidate-scan branch (event-driven, driver-callback)

**Analog:** `UAC_PROFILE_GENERIC_USB` branch, same `if/else` chain, `main/stream_uac.cpp:580-596`

**Current code (verified this session, exact line numbers):**
```cpp
// main/stream_uac.cpp:580-596 (uac_lib_task, RX_CONNECTED handler)
uac_host_stream_config_t candidates[4];
int candidate_count = 0;
auto add_candidate = [&](uint8_t ch, uint8_t bits) {
    candidates[candidate_count].channels = ch;
    candidates[candidate_count].bit_resolution = bits;
    candidates[candidate_count].sample_freq = UAC_SAMPLE_RATE;
    candidates[candidate_count].flags = 0;
    candidate_count++;
};
if (s_profile == UAC_PROFILE_GENERIC_USB) {
    add_candidate(1, 24);
    add_candidate(1, 16);
    add_candidate(2, 24);
    add_candidate(2, 16);
} else {
    add_candidate(UAC_CHANNELS, UAC_BIT_RESOLUTION);   // QMX (and FTX1 today): strict single candidate
}
```

**Pattern to copy:** Add a new `else if (s_profile == UAC_PROFILE_FTX1) { ... }` arm between the `GENERIC_USB` arm and the final `else` (which then becomes QMX-only), using the exact same `add_candidate(ch, bits)` lambda already in scope — no new helper needed. Per D-02 the try-order is:
```cpp
} else if (s_profile == UAC_PROFILE_FTX1) {
    // D-02 try-order: QMX-parity-shaped formats first, degrade toward
    // GENERIC_USB's mono/16-bit fallback only if richer formats aren't advertised.
    add_candidate(2, 24);
    add_candidate(2, 16);
    add_candidate(1, 24);
    add_candidate(1, 16);
} else {
    add_candidate(UAC_CHANNELS, UAC_BIT_RESOLUTION);   // QMX only, after FTX1 carved out
}
```
`candidates[4]` must be resized to `candidates[4]` → still 4 is enough (FTX-1 also uses exactly 4 candidates, matching GENERIC_USB's array size — no resize needed since both branches populate at most 4 entries and the array is never used by two profiles simultaneously).

**Downstream loop (no changes required, D-03):** `main/stream_uac.cpp:598-615` — the `for (int i = 0; i < candidate_count; ++i)` loop, `s_format` assignment, and success/failure logging are already generic over `candidates[]`/`candidate_count`; nothing here references `s_profile` except in log strings.

**Failure-path branch (Claude's Discretion per CONTEXT.md, not locked):** `main/stream_uac.cpp:617-629`
```cpp
if (!started) {
    if (s_profile == UAC_PROFILE_GENERIC_USB) {
        ESP_LOGE(TAG, "No 48k UAC mic format for profile=%s", profile_name(s_profile));
        snprintf(s_status_string, sizeof(s_status_string), "No 48k UAC mic format");
    } else {
        ESP_LOGE(TAG, "Failed to start stream for profile=%s", profile_name(s_profile));
        snprintf(s_status_string, sizeof(s_status_string), "Format not supported");
    }
    uac_host_device_close(handle);
    continue;
}
```
Since `UAC_PROFILE_FTX1` now also multi-candidate-scans, consider widening the `s_profile == UAC_PROFILE_GENERIC_USB` check here to `s_profile == UAC_PROFILE_GENERIC_USB || s_profile == UAC_PROFILE_FTX1` so the "No 48k UAC mic format" message (more accurate for a scanning profile) applies to FTX-1 too. This mirrors the same widening principle used in Patterns 2 and 3 below and is left as an implementation choice for the planner/executor.

**Note on placement relative to `cp210x_try_open()`:** `main/stream_uac.cpp:565-574` already special-cases `UAC_PROFILE_FTX1` to open the CP210x CAT-1 interface unconditionally before the candidate scan runs, with an explicit comment noting today's strict-candidate approach reliably fails on real hardware. That comment becomes stale once D-01/D-02 land (multi-candidate scan may now succeed) — the executor should update/remove that comment text, but the `cp210x_try_open()` call itself (unconditional, independent of mic negotiation outcome) is correct and must stay unconditional per D-09 (CAT must not depend on audio negotiation success).

---

### `main/stream_uac.cpp` — Speaker (UAC OUT) guard widening (event-driven, driver-callback)

**Analog:** `UAC_PROFILE_QMX`-only speaker pre-open block, `main/stream_uac.cpp:687-768`

**Current code (verified this session):**
```cpp
// main/stream_uac.cpp:687-694
} else if (evt.driver.event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
    if (s_profile != UAC_PROFILE_QMX) {
        ESP_LOGI(TAG, "Speaker connected ignored profile=%s addr=%u iface=%u",
                 profile_name(s_profile),
                 (unsigned)evt.driver.addr,
                 (unsigned)evt.driver.iface_num);
        continue;
    }
    // ... rest of pre-open/claim logic, lines 696-768, UNCHANGED ...
```

**Pattern to copy:** Widen exactly this one guard condition to:
```cpp
if (s_profile != UAC_PROFILE_QMX && s_profile != UAC_PROFILE_FTX1) {
```
Everything from line 696 (`s_spk_addr = evt.driver.addr;`) through line 768 (closing the pre-open `if (!s_spk_handle)` block) is copied verbatim — no changes. This includes:
- The fixed `scfg = {2, 24, 48000, FLAG_STREAM_SUSPEND_AFTER_START}` at lines 728-732 (D-04: single fixed candidate, matches `dds_render_24bit_stereo()` exactly — do not parameterize).
- The pump-buffer + `spk_writer_task` allocation at lines 742-765 (unchanged, reused as-is).
- Failure handling on `uac_host_device_open()`/`uac_host_device_start()` failure (lines 711-716, 733-739) already degrades gracefully by leaving `s_spk_handle = NULL` — this is the exact mechanism D-08/D-09's RX-only fallback relies on; no new fallback code needed (Pitfall 3 in RESEARCH.md: this is a one-shot pre-open with no retry, which is intentional existing behavior, not a gap to fill).

---

### `main/stream_uac.cpp` — FIFO partition guard widening (config/init, boot-time)

**Analog:** `UAC_PROFILE_QMX`-only custom FIFO branch, `main/stream_uac.cpp:413-426`

**Current code (verified this session):**
```cpp
// main/stream_uac.cpp:413-426 (usb_lib_task)
if (s_profile == UAC_PROFILE_QMX) {
    // Custom FIFO partitioning enables simultaneous bidirectional ISO
    // streaming with QDX or QMX hardware. ESP32-S3 has 200 FIFO lines.
    // Built-in Kconfig biases do not cover 24-bit/48k/stereo MPS=300
    // in both directions at once. This split reserves 364 B for RX,
    // 364 B for PTX, and 72 B for non-periodic OUT (CDC CAT bulk-OUT).
    host_config.fifo_settings_custom.rx_fifo_lines   = 91;   // 364 B
    host_config.fifo_settings_custom.nptx_fifo_lines = 18;   // 72 B
    host_config.fifo_settings_custom.ptx_fifo_lines  = 91;   // 364 B
    ESP_LOGI(TAG, "USB FIFO profile=%s policy=qmx-custom rx=91 nptx=18 ptx=91",
             profile_name(s_profile));
} else {
    ESP_LOGI(TAG, "USB FIFO profile=%s policy=default", profile_name(s_profile));
}
```

**Pattern to copy:** Widen the condition to `if (s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1)`, keep the 91/18/91 numeric split as the D-06 starting point (sum = 200, the hard ESP32-S3 DWC_OTG ceiling — see below), and update the log line to reflect that either profile can reach this branch (`profile_name(s_profile)` already handles this — no format-string change needed, only the guard).

**Hard constraint (must be respected by any future numeric change, not just this initial widening):** `rx_fifo_lines + nptx_fifo_lines + ptx_fifo_lines <= 200` for ESP32-S3, enforced via `HAL_ASSERT` in `usb_dwc_hal_set_fifo_config()` (`components/hal/usb_dwc_hal.c`) at `usb_host_install()` time — an over-budget sum crashes the boot, it does not fail softly. The 91/18/91 split already consumes the full 200-line budget with zero headroom (D-06/D-07: use as starting point, validate/adjust empirically in the hardware-checkpoint plan, and if any value increases, an equal-or-greater decrease elsewhere is required in the same change).

---

## Shared Patterns

### Profile-gated single-task-pair branching
**Source:** `main/stream_uac.cpp` (`usb_lib_task()` + `uac_lib_task()`, both already handle `UAC_PROFILE_GENERIC_USB`/`UAC_PROFILE_QMX`/`UAC_PROFILE_FTX1` via `s_profile` comparisons)
**Apply to:** All three edits above.
**Rule:** Never add a second USB-host task pair or a second candidate-scan loop for FTX-1 — every change in this phase is a guard/condition widening or a new `else if` arm inside the existing single task pair. This is a confirmed anti-pattern per `.planning/research/ARCHITECTURE.md` and reiterated in 04-RESEARCH.md's Anti-Patterns section.

### `profile_name(s_profile)` for logging
**Source:** used throughout `stream_uac.cpp` (e.g. lines 422-423, 601-603, 689-692)
**Apply to:** Any new log lines added for the FTX-1 branches — follow the existing `ESP_LOGI(TAG, "...profile=%s...", profile_name(s_profile))` convention rather than hardcoding profile strings.

### Downstream format-agnostic consumers require no changes
**Source:** `uac_pcm_to_ft8_samples()` (resampler, referenced in RESEARCH.md, not modified this phase) and `dds_render_24bit_stereo()` (`main/dds_q15.cpp`/`.h`, fixed-format TX renderer, not modified this phase per D-04/D-05)
**Apply to:** Confirms no other file in the repo needs edits for this phase — `s_format` is already read generically downstream for RX, and TX is fixed-format by design.

## No Analog Found

None — all three edit sites have a directly reusable in-file analog (`GENERIC_USB` mic branch, `QMX` speaker branch, `QMX` FIFO branch). This phase is a pure mechanical extension of existing patterns per RESEARCH.md's own framing ("not new architecture").

## Metadata

**Analog search scope:** `main/stream_uac.cpp` (single file; confirmed via CONTEXT.md/RESEARCH.md that no other file requires changes this phase)
**Files scanned:** 1 (`main/stream_uac.cpp`, read directly at lines 395-434 and 560-780 this session to verify exact current line numbers/content against RESEARCH.md's citations)
**Pattern extraction date:** 2026-07-06
