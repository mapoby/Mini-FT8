---
phase: 01-backend-vtable-plumbing
plan: 01
subsystem: radio-control
tags: [esp-idf, vtable, radio-backend, station-config, ftx1]

# Dependency graph
requires: []
provides:
  - "FTX-1 selectable as a fourth radio type in station configuration"
  - "radio_control_ftx1.cpp stub backend implementing the full radio_control_ops_t vtable"
  - "RADIO_CONTROL_FTX1 dispatch enum value and RadioType::FTX1 = 6 persisted enum value"
  - "All 12 enumeration/registration sites threaded (dispatch, name, persistence, menu cycle, build)"
affects: ["02-cp210x-usb-bringup", "03-cat-command-implementation", "04-bidirectional-uac-audio"]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Vtable backend registration: file-static const radio_control_ops_t k_ops + single accessor function, mirrored exactly across QMX/QDX/KH1/FTX1"

key-files:
  created:
    - main/radio_control_ftx1.cpp
  modified:
    - main/radio_control_backend.h
    - main/radio_control.h
    - main/radio_control.cpp
    - main/CMakeLists.txt
    - main/station_types.h
    - main/main.cpp

key-decisions:
  - "RADIO_CONTROL_FTX1 = 3 and RadioType::FTX1 = 6 locked per RESEARCH.md Open Question 2 (persisted on-disk values, must never be renumbered)"
  - "get_radio_profile_binding() uses AUDIO_SOURCE_QMX_UAC as an explicit Phase-1 placeholder for FTX-1, flagged in-code for revisit in Phase 4"
  - "CoreRadioType (core_api.h) intentionally left unchanged — zero callers today, per RESEARCH.md Pitfall 3"

patterns-established:
  - "New radio backends mirror radio_control_qdx.cpp's minimal (non-KH1-lifecycle) shape when no shared hardware resource is involved"

requirements-completed: [PLUMB-01, PLUMB-02]

# Metrics
duration: 55min
completed: 2026-07-05
---

# Phase 1 Plan 1: Backend Vtable Plumbing Summary

**FTX-1 added as a fourth radio backend with a fully stubbed 8-function radio_control_ops_t vtable, selectable and persistent through station configuration, with zero regression to QMX/QDX/KH1.**

## Performance

- **Duration:** 55 min
- **Started:** 2026-07-04T23:07:14Z
- **Completed:** 2026-07-05
- **Tasks:** 3 (2 auto + 1 checkpoint:human-verify)
- **Files modified:** 6 (1 created, 6 modified — CMakeLists.txt counted in both)

## Accomplishments
- New `radio_control_ftx1.cpp` stub backend compiles clean under `-Werror`, with `ready()` returning `false` and all 7 other hooks returning `ESP_ERR_INVALID_STATE`
- FTX-1 threaded through all 12 enumeration/registration sites (dispatch switch, backend name, persisted enum, `canonical_radio_type`, saved-int parsing, string-token parsing, profile binding, display name, menu cycle, build SRCS) with no silent QMX-downgrade gaps
- Physical hardware verification completed by user: build clean, menu cycles QMX -> QDX -> KH1-USBC -> KH1-MIC -> FTX-1 -> QMX, FTX-1 persists across reboot as `radio=6`, no crash, no regression

## Task Commits

Each task was committed atomically:

1. **Task 1: Create FTX-1 stub backend and wire it into dispatch + build** - `e6d00dd` (feat)
2. **Task 2: Thread FTX-1 through the persisted enum and all 6 main.cpp radio-enumeration sites** - `c714083` (feat)
3. **Task 3: Flash and verify FTX-1 is selectable without crash or regression** - human-verify checkpoint, approved by user (no separate commit; see deviation below for the post-checkpoint fix commit)

**Plan metadata:** (this commit) `docs(01-01): complete plan`

## Files Created/Modified
- `main/radio_control_ftx1.cpp` - New stub backend: 8-op vtable, `ready()` returns `false`, all other hooks return `ESP_ERR_INVALID_STATE`
- `main/radio_control_backend.h` - Declares `radio_control_ftx1_get_ops()`, grouped with plain accessors
- `main/radio_control.h` - Adds `RADIO_CONTROL_FTX1 = 3` to the dispatch enum
- `main/radio_control.cpp` - Adds `RADIO_CONTROL_FTX1` case to `current_ops()` and `radio_control_backend_name()`
- `main/CMakeLists.txt` - Registers `radio_control_ftx1.cpp` in SRCS (no REQUIRES change)
- `main/station_types.h` - Adds `RadioType::FTX1 = 6` (persisted `radio=` value)
- `main/main.cpp` - Adds FTX1 to `canonical_radio_type()`, `radio_type_from_saved_int()`, `parse_radio_config_value()`, `get_radio_profile_binding()`, `radio_name()`, and the menu cycle switch (`'3'` handler)

## Decisions Made
- Locked `RADIO_CONTROL_FTX1 = 3` and `RadioType::FTX1 = 6` per RESEARCH.md's resolved Open Question 2 — both are now stable, persisted/dispatch values that must never be renumbered.
- `get_radio_profile_binding()` maps FTX-1 to `AUDIO_SOURCE_QMX_UAC` as an explicit Phase-1 placeholder (commented in code), to be revisited in Phase 4 once the FTX-1's real USB Audio descriptor is validated against hardware.
- Left `CoreRadioType` (`core_api.h`/`core_api.cpp`) unchanged — it has zero callers today (`core_cmd_set_radio` is unreferenced), so extending it in Phase 1 would be speculative; flagged for revisit only if it gains callers.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Unused `TAG` variable warning in `radio_control_ftx1.cpp`**
- **Found during:** Post-checkpoint rebuild (after Task 3 human verification), performed at the orchestrator level
- **Issue:** `idf.py build` under `-Werror` surfaced `radio_control_ftx1.cpp:9:20: 'TAG' defined but not used [-Wunused-variable]`. This was not caught during the original Task 1 acceptance-criteria audit because the ESP-IDF toolchain was unavailable in the executor's sandbox at that time, so the audit was performed by manual code/grep review instead of an actual compiler run.
- **Fix:** Added `ESP_LOGI(TAG, "FTX-1 backend selected (stub, not yet implemented)");` as the first line of `ftx1_ready()`, giving `TAG` a real use consistent with the logging convention used by the QMX/QDX/KH1 backends.
- **Files modified:** `main/radio_control_ftx1.cpp`
- **Verification:** A subsequent full `idf.py build` (run by the orchestrator) confirmed zero errors and zero warnings project-wide.
- **Committed in:** `c11021a` (applied directly by the orchestrator, not through this executor session)

---

**Total deviations:** 1 auto-fixed (Rule 1 - Bug)
**Impact on plan:** Necessary correctness fix for the `-Werror` build; no scope creep, no behavior change (still a pure stub, `ready()` still returns `false`).

## Issues Encountered

The executor's sandbox had no ESP-IDF toolchain installed (`idf.py` not on `PATH`, no `IDF_PATH`), so `idf.py build` could not be run directly during Task 1/2 execution. Verification for those tasks was performed via manual code/grep audit against every acceptance-criteria item in the plan. The orchestrator subsequently ran the real build after the checkpoint, which surfaced one warning (see deviation above) that the manual audit could not have caught; it was fixed and reverified clean.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 2 (CP210x USB Bring-up & CAT Connection) can begin. Known forward-flags for later phases:
- `get_radio_profile_binding()` uses `AUDIO_SOURCE_QMX_UAC` as a placeholder for FTX-1 — revisit in Phase 4 (AUDIO-01/02/03).
- `CoreRadioType` (`core_api.h`) was intentionally left without an FTX-1 value (zero callers today) — revisit if `core_cmd_set_radio` gains callers.
- `radio_control_ftx1.cpp` stubs must be replaced with real CP210x CAT I/O in Phases 2-3; `ready()` stays `false` until Phase 2 USB bring-up.

No blockers.

---
*Phase: 01-backend-vtable-plumbing*
*Completed: 2026-07-05*

## Self-Check: PASSED
All created/modified files and referenced commits (e6d00dd, c714083, c11021a) verified present.
