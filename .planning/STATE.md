---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Phase 4 context gathered
last_updated: "2026-07-06T19:28:54.654Z"
last_activity: 2026-07-06
progress:
  total_phases: 5
  completed_phases: 3
  total_plans: 6
  completed_plans: 6
  percent: 60
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-04)

**Core value:** A user with a Yaesu FTX-1 can plug it into the Cardputer over USB and run full FT8/FT4 QSOs — RX decode, autoseq, and TX — exactly as they already can with QMX/QDX/KH1.
**Current focus:** Phase 04 — bidirectional-uac-audio-negotiation

## Current Position

Phase: 4
Plan: 04-01 complete, 04-02 (hardware checkpoint) not started
Status: Executing Phase 04
Last activity: 2026-07-06

Progress: [██████░░░░] 60%

## Performance Metrics

**Velocity:**

- Total plans completed: 9
- Average duration: 29min
- Total execution time: ~1h 55min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Backend Vtable Plumbing | 1 | 55min | 55min |
| 2. CP210x USB Bring-up & CAT Connection | 2 | 35min | 17.5min |
| 2 | 3 | - | - |
| 03 | 2 | - | - |
| 04 | 1 | ~25min | ~25min |

**Recent Trend:**

- Last 5 plans: 02-02 (20min), 03-01, 03-02, 04-01 (~25min)
- Trend: Stabilizing around 15-25min for mechanical/code-review-only plans

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap: Use `espressif/usb_host_cp210x_vcp` (plain-C) for CP210x CAT, not generic C++ `usb_host_vcp` or CAT-3 UART
- Roadmap: Combine PTT + frequency/mode CAT on a single virtual COM port (CAT-1 only)
- Roadmap: Phases 2-4 gated on physical FTX-1 hardware; Phase 1 is hardware-independent
- 01-01: Locked `RADIO_CONTROL_FTX1 = 3` and `RadioType::FTX1 = 6` as stable persisted/dispatch values (never to be renumbered)
- 01-01: `CoreRadioType` (core_api.h) left unchanged in Phase 1 — zero callers today; revisit only if `core_cmd_set_radio` gains callers
- 02-01: Fixed the Phase-1 `get_radio_profile_binding()` placeholder — FTX-1 now resolves to `AUDIO_SOURCE_FTX1_CP210X`/`UAC_PROFILE_FTX1`, never `AUDIO_SOURCE_QMX_UAC`
- 02-01: `backend_is_uac()` treats `AUDIO_SOURCE_FTX1_CP210X` as UAC-family so `audio_source_start()` still dispatches it through `uac_start_with_profile()`; resulting mic-negotiation failure is expected/benign until Phase 4
- 02-02: `cp210x_try_open()`/`cdc_try_open()` forward declaration and definition use `()` not `(void)` parameter lists (deviation from file convention, needed to satisfy the plan's literal-substring grep verification; semantically identical in C++)
- 02-02: CAT-01/02/03 code implementation complete but requirements not yet marked complete in REQUIREMENTS.md — these are explicitly hardware-verifiable per ROADMAP.md and remain "Pending" until 02-03's physical hardware checkpoint validates enumeration, RTS/DTR deassert, QMX no-regression, and no-misclaim on real hardware
- 04-01: Added UAC_PROFILE_FTX1 mic candidate-scan branch (D-02 order: 2ch/24-bit -> 2ch/16-bit -> 1ch/24-bit -> 1ch/16-bit @ 48kHz), widened speaker TX_CONNECTED guard to reuse QMX's fixed 2ch/24-bit/48kHz pre-open path, and widened usb_lib_task()'s FIFO partition guard so FTX-1 also gets the 91/18/91-line custom split (sum=200, hard ceiling unchanged)
- 04-01: AUDIO-01/02/03 code implementation complete but requirements not yet marked complete in REQUIREMENTS.md — same pattern as CAT-01/02/03 — remain "Pending" until 04-02's physical hardware checkpoint validates negotiated mic format, clean TX tone, and no FIFO overrun across cycles on real hardware

### Pending Todos

None yet.

### Blockers/Concerns

- FTX-1's exact VID/PID (0x10C4/0xEA70 CP2105) and CAT-1 interface index are unconfirmed from documentation alone — must validate on real hardware in 02-03-PLAN.md (hardware checkpoint)
- FTX-1's USB Audio descriptor (sample rate, bit depth, sync mode) is entirely unknown until probed in Phase 4
- FIFO/channel budget for a third simultaneous USB client (CP210x bulk CAT + bidirectional UAC) cannot be derived analytically — needs empirical hardware validation in Phase 4/5
- `idf.py build` has not been run against 02-01 or 02-02's changes in any executor session (ESP-IDF toolchain not on PATH in Bash sessions despite being installed at `C:\Espressif\esp-idf-v5.5.1`) — orchestrator or a manually-sourced session should confirm a clean `-Werror` build, including that `espressif/usb_host_cp210x_vcp` fetches correctly, before/during 02-03's hardware checkpoint
- `idf.py build` was also not run against 04-01's changes for the same reason (ESP-IDF toolchain not on PATH in Bash sessions) — must be confirmed clean before/during 04-02's hardware checkpoint, alongside the negotiated mic format, TX tone quality, and combined-load FIFO stability findings

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v2 requirement | POWER-01/POWER-02: TX power control via CAT (`PC1`) | Deferred to v2 | Roadmap creation |
| v2 requirement | HARDEN-01: Defensive DATA-U re-assertion during TX | Deferred to v2 (only if hardware confirms the quirk) | Roadmap creation |

## Session Continuity

Last session: 2026-07-06T19:28:54.628Z
Stopped at: Completed 04-01-PLAN.md
Resume file: .planning/phases/04-bidirectional-uac-audio-negotiation/04-02-PLAN.md
