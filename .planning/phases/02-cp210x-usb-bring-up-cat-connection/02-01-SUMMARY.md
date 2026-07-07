---
phase: 02-cp210x-usb-bring-up-cat-connection
plan: 01
subsystem: usb-cat-routing
tags: [usb-host, cp210x, audio-source, radio-profile-routing, ftx1]
dependency-graph:
  requires: []
  provides:
    - "UAC_PROFILE_FTX1 enum value (main/stream_uac.h)"
    - "AUDIO_SOURCE_FTX1_CP210X enum value (main/audio_source.h)"
    - "espressif/usb_host_cp210x_vcp declared build dependency"
    - "FTX-1 routed to its own audio backend/profile end to end"
  affects:
    - "main/stream_uac.cpp (Plan 02: cp210x_try_open(), CAT-02 hard-gate on cdc_try_open())"
    - "main/radio_control_ftx1.cpp (Plan 02/03: ready()/CAT wiring)"
tech-stack:
  added:
    - "espressif/usb_host_cp210x_vcp ^2.2.0 (idf_component.yml, CMakeLists.txt REQUIRES)"
  patterns:
    - "Sibling enum extension: new profile/backend values appended without renumbering existing values"
    - "Backend->profile mapping branch in audio_source_start() (else-if chain)"
key-files:
  created: []
  modified:
    - "main/idf_component.yml"
    - "main/CMakeLists.txt"
    - "main/stream_uac.h"
    - "main/audio_source.h"
    - "main/audio_source.cpp"
    - "main/main.cpp"
decisions:
  - "No new adapter/handle type introduced -- FTX-1's future cdc_acm_dev_hdl_t (opened via cp210x_vcp_open() in Plan 02) will flow through the same cat_cdc_ready()/cat_cdc_send() functions QMX already uses"
  - "backend_is_uac() treats AUDIO_SOURCE_FTX1_CP210X as UAC-family so audio_source_start() dispatches it through uac_start_with_profile(); the resulting mic-negotiation failure is expected/benign per 02-RESEARCH.md Open Question 2 and is not addressed until Phase 4"
metrics:
  duration: 15min
  completed: 2026-07-05
---

# Phase 2 Plan 1: CP210x Dependency and FTX-1 Enum/Routing Foundation Summary

Added the `espressif/usb_host_cp210x_vcp` build dependency and introduced dedicated `UAC_PROFILE_FTX1` / `AUDIO_SOURCE_FTX1_CP210X` enum values, then fixed the Phase-1 `get_radio_profile_binding()` placeholder so selecting FTX-1 no longer aliases the QMX audio profile.

## What Was Built

**Task 1 — Dependency and enum values** (commit `dbbc80c`):
- `main/idf_component.yml`: added `espressif/usb_host_cp210x_vcp: ^2.2.0` immediately after the existing `usb_host_cdc_acm` entry, with the comment `# CP210x CAT for FTX-1 (CAT-1 / Enhanced COM only)`. No existing dependency versions changed.
- `main/CMakeLists.txt`: appended `usb_host_cp210x_vcp` to the `REQUIRES` list after `usb_host_cdc_acm`. `SRCS` unchanged (component is header-consumed, same pattern as `usb_host_cdc_acm`).
- `main/stream_uac.h`: appended `UAC_PROFILE_FTX1 = 2` to `uac_stream_profile_t`.
- `main/audio_source.h`: appended `AUDIO_SOURCE_FTX1_CP210X = 3` to `audio_source_backend_t`.
- No existing enum integer values were renumbered.

**Task 2 — Routing fix** (commit `1fbaa5e`):
- `main/audio_source.cpp` `backend_is_uac()`: now also returns `true` for `AUDIO_SOURCE_FTX1_CP210X`.
- `main/audio_source.cpp` `audio_source_backend_name()`: added `case AUDIO_SOURCE_FTX1_CP210X: return "ftx1_cp210x";`.
- `main/audio_source.cpp` `audio_source_start()`: added an `else if (s_backend == AUDIO_SOURCE_FTX1_CP210X)` branch mapping to `UAC_PROFILE_FTX1`.
- `main/main.cpp` `get_radio_profile_binding()`: `RadioType::FTX1` case now returns `{AUDIO_SOURCE_FTX1_CP210X, RADIO_CONTROL_FTX1}`, and the Phase-1 placeholder comment referencing `AUDIO_SOURCE_QMX_UAC` was removed. `RADIO_CONTROL_FTX1` was already correct from Phase 1 and is unchanged.

## Deviations from Plan

None - plan executed exactly as written. Both tasks matched the exact diffs given in 02-PATTERNS.md.

## Verification

Ran the plan's automated grep checks for both tasks; all passed:
- `usb_host_cp210x_vcp` present in `main/idf_component.yml` and `main/CMakeLists.txt`.
- `UAC_PROFILE_FTX1 = 2` present in `main/stream_uac.h`.
- `AUDIO_SOURCE_FTX1_CP210X = 3` present in `main/audio_source.h`.
- `AUDIO_SOURCE_FTX1_CP210X` and `UAC_PROFILE_FTX1` present in `main/audio_source.cpp`.
- `get_radio_profile_binding()`'s `RadioType::FTX1` case returns `AUDIO_SOURCE_FTX1_CP210X` and no longer contains `AUDIO_SOURCE_QMX_UAC`.

**Build verification not run in this session.** The plan's own `<verification>` section states full build verification happens after Plan 02 (the port-open code compiling against these enums). The ESP-IDF toolchain was not sourced/confirmed on PATH in this executor's Bash sessions; per the task instructions, code review + the grep-based acceptance checks above were used instead. The orchestrator or a subsequent session with `C:\Espressif\esp-idf-v5.5.1\export.ps1` sourced should run `idf.py build` to confirm compilation, particularly since `usb_host_cp210x_vcp` has not yet been fetched into `managed_components/` (will happen automatically on `idf.py reconfigure`/`build`).

## Known Stubs

None introduced by this plan. `main/radio_control_ftx1.cpp`'s CAT-related stubs (`ready()`, etc.) are explicitly out of scope for this plan (Plan 02/03 territory per 02-PATTERNS.md) and were not touched.

## Self-Check: PASSED

- FOUND: main/idf_component.yml (contains `usb_host_cp210x_vcp`)
- FOUND: main/CMakeLists.txt (contains `usb_host_cp210x_vcp` in REQUIRES)
- FOUND: main/stream_uac.h (contains `UAC_PROFILE_FTX1 = 2`)
- FOUND: main/audio_source.h (contains `AUDIO_SOURCE_FTX1_CP210X = 3`)
- FOUND: main/audio_source.cpp (contains `AUDIO_SOURCE_FTX1_CP210X`, `UAC_PROFILE_FTX1`)
- FOUND: main/main.cpp (`RadioType::FTX1` case returns `AUDIO_SOURCE_FTX1_CP210X`)
- FOUND commit dbbc80c
- FOUND commit 1fbaa5e
