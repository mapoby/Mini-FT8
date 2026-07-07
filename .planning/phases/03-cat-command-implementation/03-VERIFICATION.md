---
phase: 03-cat-command-implementation
verified: 2026-07-06T00:00:00Z
status: passed
score: 6/6 must-haves verified
overrides_applied: 0
---

# Phase 3: CAT Command Implementation Verification Report

**Phase Goal:** Firmware controls the FTX-1's frequency, mode, and PTT entirely via CAT commands, with correct sequencing and no RTS/DTR hardware toggling.
**Verified:** 2026-07-06
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | CP210x CAT-1 port configured to 38400 8N1 immediately after open, before any CAT command sent | VERIFIED | `main/stream_uac.cpp:319-326` — `cdc_acm_line_coding_t{.dwDTERate=38400,.bCharFormat=0,.bParityType=0,.bDataBits=8}` passed to `cdc_acm_host_line_coding_set(handle, &line_coding)`, placed after RTS/DTR deassert (line 313) and before `s_cdc_handle = handle;` (328) / `g_cdc_initial_sync_pending = true;` (331) |
| 2 | Firmware sends `FA` + 9-digit zero-padded Hz + `;` (not 11-digit) | VERIFIED | `main/radio_control_ftx1.cpp:36` `snprintf(fa, sizeof(fa), "FA%09d;", freq_hz);` (sync) and line 73 (restore). No `%011d` present anywhere in file (grep confirms) |
| 3 | `MD0C;` sent once at sync time only, never from begin_tx/end_tx | VERIFIED | `main/radio_control_ftx1.cpp:42` — only occurrence of `"MD0C;"` in the file, inside `ftx1_sync_frequency_mode`. `ftx1_begin_tx` (49-59), `ftx1_end_tx` (77-88), `ftx1_restore_rx_state` (66-75) contain no MD0C; send |
| 4 | TX keys via `TX1;` only, unkeys via `TX0;` only; no RTS/DTR calls in CAT layer | VERIFIED | `main/radio_control_ftx1.cpp:53,97` send `"TX1;"`; line 82 sends `"TX0;"`. `grep -c cdc_acm_host_set_control_line_state main/radio_control_ftx1.cpp` = 0 (only used in stream_uac.cpp's CAT-03 deassert, unrelated to PTT) |
| 5 | end_tx drains audio via `uac_tx_end()` before `TX0;`, then restores RX-frequency-only (no MD0C; on restore path) | VERIFIED | `main/radio_control_ftx1.cpp:80` `uac_tx_end();` precedes line 82's `TX0;` send; line 87 calls `ftx1_restore_rx_state()` which (66-75) sends only `FA%09d;`, no MD0C; |
| 6 | freq_hz clamped to 30000..470000000 before FA formatting | VERIFIED | `main/radio_control_ftx1.cpp:28-32` — range check with `ESP_LOGW` and clamp assignment before `s_rx_freq_hz = freq_hz;` (33) and `snprintf` (36) |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `main/stream_uac.cpp` | CP210x CAT-1 line-coding config (38400 8N1) in `cp210x_try_open()` | VERIFIED | Present at lines 317-326, correctly positioned |
| `main/radio_control_ftx1.cpp` | Real FTX-1 CAT command implementation (FA/MD/TX) replacing Phase 1 stubs | VERIFIED | All non-`ready()` ops implemented with real CAT bytes; `ESP_ERR_INVALID_STATE` only remains as the `ftx1_send_cmd` guard (line 18), matching plan spec; `ftx1_set_time` correctly returns `ESP_ERR_NOT_SUPPORTED` |
| `.planning/phases/03-cat-command-implementation/03-02-SUMMARY.md` | Recorded hardware verification results and CAT-1 PTT-scoping resolution | VERIFIED | Present, records pass on all 4 ROADMAP criteria plus CAT-1 PTT resolution (no CAT-2 needed) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `radio_control_ftx1.cpp` | `cat_cdc_send` (stream_uac.cpp) | `ftx1_send_cmd` wrapper | WIRED | `ftx1_send_cmd` (line 17-20) guards on `cat_cdc_ready()` then calls `cat_cdc_send(...)` |
| `radio_control_ftx1.cpp: ftx1_end_tx` | `uac_tx_end` (stream_uac.cpp) | drain-before-unkey ordering | WIRED | Line 80 `uac_tx_end();` precedes line 82 `TX0;` send; `uac_tx_end` defined at `stream_uac.cpp:1115` |
| `stream_uac.cpp: cp210x_try_open` | `cdc_acm_host_line_coding_set` (usb_host_cp210x_vcp) | one-shot config at open time | WIRED | Line 325 call, result logged, non-blocking on failure (matches plan's log-only requirement) |
| `stream_uac.cpp` (uac_lib_task) | `cp210x_try_open()` for FTX-1 profile | gap-closure Task 3 (commit db2186b) | WIRED | Line 565-573: unconditional call right after `uac_host_printf_device_param(handle)`, not gated by mic-negotiation `started` flag or the `!started` early-continue |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|--------------|--------|----------|
| SYNC-01 | 03-01 | FA 9-digit VFO-A frequency set via CAT | SATISFIED | Code truth #2 + hardware-confirmed in 03-02-SUMMARY.md (FTX-1 VFO-A followed to 7.074000 MHz) |
| SYNC-02 | 03-01 | MD0C; DATA-U mode set once at sync, not resent per TX | SATISFIED | Code truth #3 + hardware-confirmed (DATA-U displayed, no repeat MD0C; per TX cycle) |
| SYNC-03 | 03-01 | Post-TX restore of RX dial frequency/mode | SATISFIED | Code truth #5 + hardware-confirmed (FA-only restore, DATA-U remained displayed) |
| PTT-01 | 03-01 | TX1;/TX0; keying, no RTS/DTR toggling | SATISFIED | Code truth #4 + hardware-confirmed (PTT indicator engaged on TX1; no RTS/DTR) — resolves the phase's highest-risk open question (CAT-1 alone keys the radio) |
| PTT-02 | 03-01 | mode-set before PTT-key, drain-before-unkey | SATISFIED | Code truth #5 + hardware-confirmed ("FTX-1 TX stop" log after drain) |

No orphaned requirements: all 5 IDs declared in both plans' frontmatter (`requirements: [SYNC-01, SYNC-02, SYNC-03, PTT-01, PTT-02]`) match exactly the 5 IDs mapped to Phase 3 in REQUIREMENTS.md's traceability table.

**Note:** REQUIREMENTS.md and ROADMAP.md still show these 5 requirement rows as "Pending" and Phase 3 progress as "0/2 plans complete / Not started" — this is stale bookkeeping (not yet updated post-completion) and is a documentation-sync gap, not a code gap. Recommend the orchestrator update ROADMAP.md's progress table and REQUIREMENTS.md's traceability status column to "Complete" for SYNC-01/02/03, PTT-01/02 as part of closing this phase.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `main/stream_uac.cpp` | 313-326 | Line-coding/control-line setup failures (`line_err`, `lc_err`) are logged only, never abort the open or block `s_cdc_handle` assignment (03-REVIEW.md CR-01) | Warning | If CP2105 rejects the 38400 8N1 request, `cat_cdc_ready()` still reports true and every CAT command frames incorrectly with no detection/recovery. Does not block Phase 3's goal (hardware checkpoint confirmed line-coding succeeded and commands worked on the physical unit — 03-02-SUMMARY.md), but is a real latent robustness gap flagged by code review |
| `main/radio_control_ftx1.cpp` | 66-75 | `ftx1_restore_rx_state()` sends `FA%09d;` from `s_rx_freq_hz` with no clamp/guard if called before any sync (`s_rx_freq_hz` defaults to 0) — 03-REVIEW.md WR-04 | Warning | Edge case (end_tx/set_tune(false) called before first sync) would send `FA000000000;`. Not exercised in the hardware checkpoint's happy-path flow |
| `main/stream_uac.cpp` | 589-596 | FTX-1 mic UAC negotiation always fails (24-bit-only candidate vs. real 16-bit hardware) — 03-REVIEW.md WR-02 | Info | Explicitly out of scope for Phase 3 (audio is Phase 4's concern per ROADMAP); flagged here for completeness, not a Phase 3 gap |

No TBD/FIXME/XXX debt markers found in the two files modified by this phase.

### Human Verification Required

None. All four ROADMAP Phase 3 success criteria were verified against physical hardware during 03-02 (documented in 03-02-SUMMARY.md as human-observed pass/fail with explicit "all yes" confirmation), and this verifier independently confirmed the underlying code matches those claims.

### Gaps Summary

No gaps blocking phase-goal achievement. All 6 derived observable truths verified in code, all 4 key links wired, all 5 requirement IDs satisfied with hardware confirmation recorded in 03-02-SUMMARY.md. Three warning/info-level code-review findings (CR-01 line-coding failure not acted on, WR-04 zero-frequency restore edge case, WR-02 FTX-1 mic negotiation failure) remain open in 03-REVIEW.md but do not block the Phase 3 goal — CR-01 and WR-04 are latent robustness gaps in edge cases not exercised during hardware validation; WR-02 (audio) is explicitly Phase 4 scope. Recommend these be tracked as follow-up items rather than blocking phase closure.

ROADMAP.md's progress table and REQUIREMENTS.md's traceability table are stale (show Phase 3 as not started / requirements pending) — a documentation-sync task, not a code gap.

---

_Verified: 2026-07-06_
_Verifier: Claude (gsd-verifier)_
