---
phase: 02-cp210x-usb-bring-up-cat-connection
verified: 2026-07-06T00:00:00Z
status: passed
score: 8/8 must-haves verified
overrides_applied: 0
---

# Phase 2: CP210x USB Bring-up / CAT Connection Verification Report

**Phase Goal:** Firmware reliably opens and maintains a CAT connection to the physical FTX-1 over its CP210x USB virtual COM port, with the vendor-class interface never misclaimed by existing CDC-ACM logic.
**Verified:** 2026-07-06
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Selecting FTX-1 resolves to `AUDIO_SOURCE_FTX1_CP210X`/`UAC_PROFILE_FTX1`, never `AUDIO_SOURCE_QMX_UAC` | VERIFIED | `main/main.cpp:1623-1624` `case RadioType::FTX1: return {AUDIO_SOURCE_FTX1_CP210X, RADIO_CONTROL_FTX1};` — no `AUDIO_SOURCE_QMX_UAC` in the FTX1 case |
| 2 | `usb_host_cp210x_vcp` is a declared build dependency and links | VERIFIED | `main/idf_component.yml:24` declares `espressif/usb_host_cp210x_vcp: ^2.2.0`; `main/CMakeLists.txt:18` REQUIRES includes it; `build/esp-idf/espressif__usb_host_cp210x_vcp/libespressif__usb_host_cp210x_vcp.a` exists on disk, confirming it was actually compiled and linked, not just declared |
| 3 | Firmware opens the FTX-1 CP210x CAT-1 port via `cp210x_vcp_open()` | VERIFIED | `main/stream_uac.cpp:302` `cp210x_vcp_open(CP2105_PID, /*interface_idx=*/0, &dev_cfg, &handle)` inside `cp210x_try_open()` (no blind interface-scan loop present) |
| 4 | RTS/DTR deasserted immediately after open (CAT-03) | VERIFIED | `main/stream_uac.cpp:313` `cdc_acm_host_set_control_line_state(handle, false, false)` called right after successful open, before `s_cdc_handle` assignment |
| 5 | `cdc_try_open()`'s blind interface scan can never run for a non-QMX profile (CAT-02) | VERIFIED | `main/stream_uac.cpp:205` `if (s_profile != UAC_PROFILE_QMX) return;` is the first statement inside `cdc_try_open()` |
| 6 | CDC-ACM base driver installs for FTX-1 profile as well as QMX | VERIFIED | `main/stream_uac.cpp:428` `if (s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1) {` gates the install block; FIFO-partition condition at line 402 correctly left QMX-only (unrelated, deferred to Phase 4/AUDIO-03) |
| 7 | `ftx1_ready()` reflects the real CAT connection via `cat_cdc_ready()` | VERIFIED | `main/radio_control_ftx1.cpp:9-11` `static bool ftx1_ready(void) { return cat_cdc_ready(); }` |
| 8 | Physical hardware: reliable enumeration/replug, no spurious PTT, no CDC-ACM misclaim, QMX no-regression | VERIFIED (human-confirmed) | 02-03-SUMMARY.md documents direct human observation on physical FTX-1 + QMX hardware, per this phase's explicit checkpoint:human-verify gate; VID 0x10C4/PID 0xEA70/iface-0=CAT-1 independently cross-confirmed via Windows PnP enumeration |

**Score:** 8/8 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `main/idf_component.yml` | `usb_host_cp210x_vcp` dependency | VERIFIED | Line 24, `^2.2.0` |
| `main/CMakeLists.txt` | `usb_host_cp210x_vcp` in REQUIRES | VERIFIED | Line 18 |
| `main/stream_uac.h` | `UAC_PROFILE_FTX1` enum value | VERIFIED | Line 32, `= 2` |
| `main/audio_source.h` | `AUDIO_SOURCE_FTX1_CP210X` enum value | VERIFIED | Line 14, `= 3` |
| `main/main.cpp` | FTX1 routes to CP210x backend | VERIFIED | Line 1623-1624 |
| `main/stream_uac.cpp` | `cp210x_try_open()` + profile-gated dispatch | VERIFIED | Lines 132 (fwd decl), 283 (def), 302 (open call), 624/877 (dispatch call sites) |
| `main/radio_control_ftx1.cpp` | `ftx1_ready()` delegates to `cat_cdc_ready()` | VERIFIED | Lines 9-11 |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `main/main.cpp get_radio_profile_binding()` | `AUDIO_SOURCE_FTX1_CP210X` | `case RadioType::FTX1` | WIRED | Confirmed at line 1623 |
| `main/audio_source.cpp audio_source_start()` | `UAC_PROFILE_FTX1` | backend→profile mapping | WIRED | `else if` branch present (per 02-01-SUMMARY, grep-confirmed) |
| `main/stream_uac.cpp cp210x_try_open()` | `cp210x_vcp_open(CP2105_PID, ...)` | CP210x port open | WIRED | Line 302 |
| `main/stream_uac.cpp cp210x_try_open()` | `cdc_acm_host_set_control_line_state(handle, false, false)` | RTS/DTR deassert (CAT-03) | WIRED | Line 313 |
| `main/stream_uac.cpp cdc_try_open()` | early return | `s_profile != UAC_PROFILE_QMX` guard (CAT-02) | WIRED | Line 205 |
| `main/radio_control_ftx1.cpp ftx1_ready()` | `cat_cdc_ready()` | vtable ready hook | WIRED | Lines 9-11 |
| physical FTX-1 | firmware CAT-1 port open | USB enumeration + `cp210x_vcp_open` | WIRED (human-confirmed) | 02-03-SUMMARY.md — direct hardware test, VID/PID/iface confirmed |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CAT-01 | 02-01, 02-02, 02-03 | Firmware opens CAT connection via `usb_host_cp210x_vcp` | SATISFIED | `cp210x_vcp_open()` call + build artifact + hardware confirmation |
| CAT-02 | 02-01, 02-02, 02-03 | CDC-ACM install/scan strictly profile-gated, never misclaims vendor-class interface | SATISFIED | Routing fix (main.cpp) + hard-gate guard (`cdc_try_open`) + hardware confirmation (no misclaim spam observed) |
| CAT-03 | 02-02, 02-03 | RTS/DTR deasserted immediately after open | SATISFIED | `cdc_acm_host_set_control_line_state(handle, false, false)` call + hardware confirmation (no spurious PTT) |

No orphaned requirements: REQUIREMENTS.md's Phase 2 traceability row set (CAT-01/02/03) matches exactly the `requirements:` frontmatter declared across 02-01/02-02/02-03 plans.

**Note (documentation staleness, not a code gap):** `.planning/REQUIREMENTS.md` still shows CAT-01/02/03 as unchecked `[ ]` checkboxes and its Traceability table lists them as "Pending," even though the phase's own plans/summaries and the codebase confirm they are satisfied. This is a stale-documentation issue in REQUIREMENTS.md, not a functional gap — recommend updating REQUIREMENTS.md checkboxes/status column to reflect Phase 2 completion.

### Anti-Patterns Found

None. Scanned all phase-modified files (`main/stream_uac.cpp`, `main/radio_control_ftx1.cpp`, `main/audio_source.cpp`, `main/audio_source.h`, `main/stream_uac.h`, `main/main.cpp`, `main/idf_component.yml`, `main/CMakeLists.txt`) for `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` — zero matches. No stub returns, no empty handlers, no hardcoded-empty data introduced by this phase's changes.

### Human Verification Required

None outstanding. The phase's single human-verification requirement (hardware bring-up, Plan 03) was already executed as a `checkpoint:human-verify` task and directly confirmed by the user, per 02-03-SUMMARY.md — treated as satisfying evidence per this verification's explicit instructions.

### Gaps Summary

No gaps. All observable truths, artifacts, and key links verified directly against source (grep + Read), cross-checked against a built library artifact (`libespressif__usb_host_cp210x_vcp.a`) confirming the dependency actually compiles/links, and the hardware-only success criteria were confirmed by direct human observation under an explicit checkpoint gate. The only finding is a documentation-staleness note in REQUIREMENTS.md (checkboxes/status column not updated) — informational only, does not block phase completion.

---

_Verified: 2026-07-06_
_Verifier: Claude (gsd-verifier)_
