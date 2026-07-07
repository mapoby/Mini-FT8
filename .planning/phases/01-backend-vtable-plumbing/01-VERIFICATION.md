---
phase: 01-backend-vtable-plumbing
verified: 2026-07-05T00:00:00Z
status: passed
score: 5/5 must-haves verified
overrides_applied: 0
---

# Phase 1: Backend Vtable Plumbing Verification Report

**Phase Goal:** The FTX-1 backend exists as a selectable radio type with a complete (stubbed) `radio_control_ops_t` implementation, mirroring the QMX/KH1 backend pattern. No hardware required.
**Verified:** 2026-07-05
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Firmware builds with -Werror with radio_control_ftx1.cpp registered and all 8 ops populated | ✓ VERIFIED | `main/radio_control_ftx1.cpp:54-64` defines `k_ops` with all 8 members (`name`, `ready`, `on_audio_start`, `sync_frequency_mode`, `begin_tx`, `set_tone_hz`, `end_tx`, `set_tune`, `set_time`). `main/CMakeLists.txt:11` registers the file in SRCS. User-confirmed physical build: clean, zero errors/warnings (commit c11021a fixed the one -Werror unused-TAG warning found on real build). |
| 2 | ftx1_ready() returns false (stub, no hardware I/O) | ✓ VERIFIED | `main/radio_control_ftx1.cpp:11-14` — `ftx1_ready` returns `false` unconditionally; other 7 hooks return `ESP_ERR_INVALID_STATE`; no `stream_uac.h` include, no CAT/UAC calls. |
| 3 | User can cycle the station-config radio selector to show 'FTX-1' | ✓ VERIFIED | `main/main.cpp:1637-1639` `radio_name()` returns `"FTX-1"`; menu cycle switch at `main.cpp:5493-5499` reaches `RadioType::FTX1` from `KH1_MIC` then wraps to `QMX`. User-confirmed hardware test: menu cycles QMX→QDX→KH1-USBC→KH1-MIC→FTX-1→QMX, label displays correctly. |
| 4 | Selecting FTX-1 persists as radio=6 in Station.txt and reloads as FTX-1 | ✓ VERIFIED | `main/station_types.h:33` `FTX1 = 6`; `main.cpp:1576-1577` `radio_type_from_saved_int` maps int 6 back to `RadioType::FTX1`; `main.cpp:1609-1610` string-token parser also handles "FTX1"/"FTX-1". User-confirmed hardware test: FTX-1 selection persists across reboot as radio=6. |
| 5 | Selecting FTX-1 does not crash and does not alter the QMX/QDX/KH1 selection paths | ✓ VERIFIED | All edits are additive `case`/`if` branches before existing `default:` fallthroughs (verified in `radio_control.cpp`, `main.cpp` at all 6 sites); no existing case bodies modified except KH1_MIC's cycle target (intentional, per plan). User-confirmed hardware test: no crash, no regression to QMX/QDX/KH1 selection or display. |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `main/radio_control_ftx1.cpp` | Stubbed FTX-1 backend, ready() returns false | ✓ VERIFIED | 69 lines, all 8 ops populated, no UAC/CAT includes |
| `main/radio_control_backend.h` | `radio_control_ftx1_get_ops()` declaration | ✓ VERIFIED | Line 21, grouped with plain accessors (qmx/qdx), before KH1 extended API |
| `main/radio_control.h` | `RADIO_CONTROL_FTX1` dispatch enum value | ✓ VERIFIED | Line 15, `RADIO_CONTROL_FTX1 = 3` |
| `main/radio_control.cpp` | FTX-1 case in current_ops() and radio_control_backend_name() | ✓ VERIFIED | Lines 17-18 and 43-44 |
| `main/station_types.h` | `RadioType::FTX1 = 6` persisted enum value | ✓ VERIFIED | Line 33 |
| `main/main.cpp` | FTX-1 threaded through all 6 radio-enumeration sites | ✓ VERIFIED | 8 occurrences of `RadioType::FTX1`, all 6 sites confirmed (canonical_radio_type, radio_type_from_saved_int, parse_radio_config_value, get_radio_profile_binding, radio_name, menu cycle switch) |
| `main/CMakeLists.txt` | radio_control_ftx1.cpp registered in SRCS | ✓ VERIFIED | Line 11, after radio_control_kh1_cat.cpp |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `radio_control.cpp` | `radio_control_ftx1_get_ops` | current_ops() switch case | ✓ WIRED | Line 17-18 |
| `main.cpp` | `RADIO_CONTROL_FTX1` | get_radio_profile_binding() case | ✓ WIRED | Line 1623-1626, includes Phase-4 placeholder comment |
| `main.cpp` | `RadioType::FTX1` | menu cycle switch | ✓ WIRED | Line 5495, reached from KH1_MIC, wraps to QMX at 5497-5499 |
| `CMakeLists.txt` | `main/radio_control_ftx1.cpp` | SRCS list entry | ✓ WIRED | Line 11 |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| PLUMB-01 | 01-01-PLAN.md | User can select "FTX-1" as active radio in station configuration | ✓ SATISFIED | Menu cycle, radio_name(), persistence all confirmed in code and by user hardware test |
| PLUMB-02 | 01-01-PLAN.md | radio_control_ftx1.cpp implements existing radio_control_ops_t vtable, mirroring QMX/KH1 pattern | ✓ SATISFIED | All 8 vtable members populated per `radio_control_backend.h` struct shape, file mirrors radio_control_qdx.cpp shape |

No orphaned requirements — REQUIREMENTS.md maps only PLUMB-01/PLUMB-02 to Phase 1, both claimed and satisfied.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `main/radio_control_ftx1.cpp` | 12 | "stub, not yet implemented" in log string | ℹ️ Info | Intentional per plan — stub scope explicitly documented in REQUIREMENTS.md as Phase 2-3 follow-up work; not an unresolved debt marker (no TODO/FIXME/XXX/TBD tokens present) |

No blocker or warning anti-patterns found. No TBD/FIXME/XXX markers in any modified file.

### Behavioral Spot-Checks

Step 7b: SKIPPED — this is embedded firmware requiring physical hardware flash; no runnable entry point exists in the dev sandbox. Covered instead by user-confirmed physical hardware test (Task 3 checkpoint), accepted per task instructions.

### Probe Execution

No probes declared or found for this phase (no `scripts/*/tests/probe-*.sh` referenced in PLAN/SUMMARY).

### Human Verification Required

None outstanding. Task 3 (human-verify checkpoint) was completed by the user prior to this verification: build clean under -Werror (after commit c11021a fixed an unused-TAG warning), menu cycles QMX → QDX → KH1-USBC → KH1-MIC → FTX-1 → wraps to QMX, FTX-1 persists across reboot as Station.txt radio=6, no crash, no regression to existing radios. This account is consistent with all static code evidence found during this verification — no inconsistency detected.

### Gaps Summary

None. All 5 observable truths verified, all 7 required artifacts present and correctly wired, all 4 key links confirmed, both requirement IDs satisfied, no anti-pattern blockers, and the human verification checkpoint was completed with results consistent with the code.

---

*Verified: 2026-07-05*
*Verifier: Claude (gsd-verifier)*
