---
phase: 03-cat-command-implementation
plan: 01
subsystem: radio-control
tags: [ftx1, cat, cp210x, usb]
requires: []
provides: [ftx1-cat-commands, cp210x-line-coding]
affects: [radio_control_ftx1, stream_uac]
tech-stack:
  added: []
  patterns:
    - "CAT-1 line coding (38400 8N1) configured once at port-open time, log-only on failure"
    - "FTX-1 CAT command encoding: 9-digit FA, single MD0C; at sync only, TX1;/TX0; PTT"
key-files:
  created: []
  modified:
    - main/stream_uac.cpp
    - main/radio_control_ftx1.cpp
decisions:
  - "MD0C; issued only in ftx1_sync_frequency_mode; end_tx restore path is frequency-only (no defensive mode re-assert this phase, per HARDEN-01 deferral)"
  - "freq_hz clamped to [30000, 470000000] with ESP_LOGW on out-of-range input before FA formatting"
metrics:
  duration: 20min
  completed: 2026-07-06
---

# Phase 3 Plan 1: FTX-1 CAT Command Implementation Summary

CP210x CAT-1 transport now configures 38400 8N1 line coding at open time, and the FTX-1 radio backend sends real Yaesu-dialect CAT commands (FA/MD/TX) in place of the Phase 1 stubs.

## What Was Built

1. **`main/stream_uac.cpp`** — `cp210x_try_open()` now calls `cdc_acm_host_line_coding_set()` with `dwDTERate=38400, bCharFormat=0, bParityType=0, bDataBits=8` immediately after the existing RTS/DTR deassert call and before `s_cdc_handle`/`g_cdc_initial_sync_pending` are set. Failure is logged only (matches the adjacent RTS/DTR convention) — does not abort the open.

2. **`main/radio_control_ftx1.cpp`** — replaced all Phase 1 stub bodies (except `ftx1_ready()`, unchanged) with real CAT command encoding:
   - `ftx1_send_cmd()`: thin wrapper over `cat_cdc_ready()`/`cat_cdc_send()`, copied from the QMX/QDX pattern.
   - `ftx1_sync_frequency_mode()`: clamps `freq_hz` to `[30000, 470000000]` (ESP_LOGW on out-of-range), caches it in `s_rx_freq_hz`, sends `FA<9-digit>;` then `MD0C;` (DATA-U). This is the only place in the file that sends `MD0C;`.
   - `ftx1_begin_tx()`: sends `TX1;` only — no FA/MD resend (SYNC-02).
   - `ftx1_set_tone_hz()`: no-op, returns `ESP_OK` (tone rides UAC OUT in Phase 4).
   - `ftx1_restore_rx_state()`: helper sending `FA<9-digit s_rx_freq_hz>;` only, no `MD0C;` — frequency-only restore (SYNC-03).
   - `ftx1_end_tx()`: no-ops if not TX-active; otherwise calls `uac_tx_end()` to drain audio, then sends `TX0;` (drain-before-unkey per PTT-02), then calls `ftx1_restore_rx_state()`.
   - `ftx1_set_tune()`: disable branch delegates to `ftx1_end_tx()`; enable branch calls `ftx1_sync_frequency_mode()` then sends `TX1;`.
   - `ftx1_on_audio_start()`: logs init, returns `ESP_OK`.
   - `ftx1_set_time()`: returns `ESP_ERR_NOT_SUPPORTED` (no DT-command wiring this phase).
   - No `cdc_acm_host_set_control_line_state()` calls anywhere in this file (PTT-01 — no RTS/DTR toggling from the CAT layer).
   - `k_ops` vtable initializer and `radio_control_ftx1_get_ops()` unchanged from the Phase 1 stub.

## Verification

- `grep -c "FA%09d;" main/radio_control_ftx1.cpp` → 2 (sync + restore); no `FA%011d` present.
- `grep -c "MD0C;" main/radio_control_ftx1.cpp` → 1 (sync path only; comments rephrased to avoid the literal substring so the count stays accurate).
- `TX1;` appears in `ftx1_begin_tx` and `ftx1_set_tune`; `TX0;` appears once in `ftx1_end_tx`.
- `uac_tx_end()` call (line 80) precedes the `TX0;` send (line 82) in `ftx1_end_tx` — drain-before-unkey ordering confirmed.
- No `cdc_acm_host_set_control_line_state` call in `radio_control_ftx1.cpp`.
- `cdc_acm_host_line_coding_set(handle, &line_coding)` present in `stream_uac.cpp`, positioned before `g_cdc_initial_sync_pending = true;` inside `cp210x_try_open()`.
- `k_ops` initializer block diff-verified unchanged from the Phase 1 stub.
- Build verification (`idf.py build -Werror`) deferred to plan 03-02 per plan instructions — ESP-IDF toolchain not on PATH in this executor's Bash sessions.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Comment text triggered false positives in the `MD0C;` grep acceptance check**
- **Found during:** Task 2, self-verification of acceptance criteria
- **Issue:** Explanatory comments literally contained the substring `MD0C;` (e.g. "MD0C; (DATA-U) is set once here...", "defensive MD0C; re-assert..."), pushing `grep -c "MD0C;"` to 3 instead of the required 1, even though the command is only sent from code once.
- **Fix:** Reworded both comments to say "DATA-U mode"/"defensive DATA-U re-assert" instead of the literal `MD0C;` token, preserving the intended explanation without affecting the grep-based acceptance check.
- **Files modified:** main/radio_control_ftx1.cpp
- **Commit:** 93be267 (included in Task 2's commit, not separately committed)

No other deviations — plan executed as written.

## Known Stubs

None. All non-`ready()` FTX-1 ops now issue real CAT commands or return an explicit, documented non-support code (`set_time` → `ESP_ERR_NOT_SUPPORTED`, unchanged from plan spec).

## Threat Flags

None — this plan's changes stay within the `<threat_model>` already defined in 03-01-PLAN.md (T-03-01 clamp, T-03-02 line-coding, T-03-03 accepted/deferred inbound-parsing). No new network/auth/file-access surface was introduced.

## Self-Check: PASSED

- FOUND: main/stream_uac.cpp (line-coding block present, verified via grep)
- FOUND: main/radio_control_ftx1.cpp (all task-2 functions present, verified via grep)
- FOUND: commit ede5ccf (Task 1)
- FOUND: commit 93be267 (Task 2)
