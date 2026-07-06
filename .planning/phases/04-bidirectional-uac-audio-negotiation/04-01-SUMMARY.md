---
phase: 04-bidirectional-uac-audio-negotiation
plan: 01
subsystem: audio-transport
tags: [uac, usb-host, ftx1, fifo, mic-negotiation, speaker-preopen]
dependency-graph:
  requires: [Phase 02 CP210x CAT bring-up, Phase 03 CAT command implementation]
  provides: [FTX-1 mic candidate-scan, FTX-1 speaker pre-open reuse, FTX-1 custom FIFO partition]
  affects: [main/stream_uac.cpp uac_lib_task(), main/stream_uac.cpp usb_lib_task()]
tech-stack:
  added: []
  patterns: [profile-gated branching inside existing candidate-scan/pre-open/FIFO-partition code, no new tasks or subsystems]
key-files:
  created: []
  modified:
    - main/stream_uac.cpp
decisions:
  - "D-01/D-02: FTX-1 mic negotiation scans 4 real candidates in order 2ch/24-bit -> 2ch/16-bit -> 1ch/24-bit -> 1ch/16-bit, all 48kHz"
  - "D-03: negotiated format still recorded in s_format; no downstream resampler changes"
  - "D-04/D-05: FTX-1 speaker reuses QMX's single-candidate (2ch/24-bit/48kHz) pre-open path unmodified; no fallback pre-built"
  - "D-06/D-07: FTX-1 gets QMX's 91/18/91 FIFO split as analytical starting point (sum=200, hard ceiling); empirical adjustment for CP210x bulk CAT deferred to 04-02 hardware checkpoint"
metrics:
  duration: ~25min
  completed: 2026-07-06
---

# Phase 4 Plan 1: FTX-1 Mic Candidate-Scan, Speaker Reuse, FIFO Widening Summary

Mechanical extension of `main/stream_uac.cpp`'s existing `UAC_PROFILE_GENERIC_USB` candidate-scan and `UAC_PROFILE_QMX` speaker-preopen/FIFO-partition patterns to also cover `UAC_PROFILE_FTX1`, per CONTEXT.md's locked decisions D-01 through D-09.

## What Was Built

**Task 1 — Mic candidate-scan branch (D-01/D-02).** Added an `else if (s_profile == UAC_PROFILE_FTX1)` arm in `uac_lib_task()`'s `RX_CONNECTED` handler, between the existing `GENERIC_USB` arm and the now-QMX-only final `else`. The arm calls `add_candidate(2, 24); add_candidate(2, 16); add_candidate(1, 24); add_candidate(1, 16);` in exactly that order — QMX-parity-shaped formats first, degrading toward GENERIC_USB's mono/16-bit fallback. The `add_candidate` lambda, `candidates[4]` array, and the downstream negotiation for-loop are untouched (already generic over whatever list was populated). Widened the failure-path message check so a failed FTX-1 scan logs "No 48k UAC mic format" (accurate for a scanning profile) instead of the generic "Format not supported". Corrected the stale comment at the `cp210x_try_open()` FTX-1 dispatch that previously claimed negotiation "reliably fails on real hardware" — no longer accurate once multi-candidate scanning lands; the unconditional call itself is unchanged.

**Task 2 — Speaker (TX_CONNECTED) guard widening (D-04/D-05).** Widened the single guard `if (s_profile != UAC_PROFILE_QMX)` to `if (s_profile != UAC_PROFILE_QMX && s_profile != UAC_PROFILE_FTX1)`. Everything downstream — `s_spk_addr`/`s_spk_iface` capture, `uac_host_device_open()`, the fixed `scfg = {2, 24, 48000, FLAG_STREAM_SUSPEND_AFTER_START}`, pump-buffer allocation, `spk_writer_task` creation — is byte-for-byte unchanged. No second candidate or FTX-1-specific pre-open path was added, per D-05 (any fallback is a hardware-validation finding for 04-02, not something to pre-build).

**Task 3 — FIFO partition guard widening + budget arithmetic (D-06/D-07/AUDIO-03).** Widened `usb_lib_task()`'s guard `if (s_profile == UAC_PROFILE_QMX)` to `if (s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1)`. Kept `rx_fifo_lines=91`, `nptx_fifo_lines=18`, `ptx_fifo_lines=91` unchanged (sum=200, exactly the ESP32-S3 DWC_OTG hard ceiling, verified against `usb_dwc_hal.c` in 04-RESEARCH.md). Expanded the comment block to note the split now also applies to FTX-1, that the sum-<=200 constraint is non-negotiable (HAL_ASSERT at `usb_host_install()` time, not a soft failure), and that CP210x bulk CAT traffic sharing `nptx_fifo_lines` is the item flagged for empirical validation in the 04-02 hardware-checkpoint plan (D-07).

## Deviations from Plan

None — plan executed exactly as written. All three tasks matched 04-PATTERNS.md's exact diffs; no Rule 1-4 auto-fixes were needed since this was a pure mechanical guard-widening exercise with no new logic paths.

## Verification

- `grep -n "else if (s_profile == UAC_PROFILE_FTX1)" main/stream_uac.cpp` — one occurrence in the RX_CONNECTED candidate-selection if-chain (a second, pre-existing occurrence at line ~904 is unrelated `uac_on_block_processed()` code from Phase 2/3, not part of this plan).
- `grep -c "add_candidate(2, 24)" main/stream_uac.cpp` — 2 occurrences (the new FTX-1 arm's first candidate, plus GENERIC_USB's `add_candidate(2, 24)` at position 3 in its own list — both expected).
- `grep -n "s_profile != UAC_PROFILE_QMX && s_profile != UAC_PROFILE_FTX1" main/stream_uac.cpp` — one occurrence, inside `UAC_HOST_DRIVER_EVENT_TX_CONNECTED`.
- `grep -n "s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1" main/stream_uac.cpp` — occurs at the FIFO guard (line 413) and at a pre-existing, unrelated CDC-ACM driver-install guard (line 450, added in Phase 2, untouched by this plan).
- Manual arithmetic: 91 + 18 + 91 = 200 <= 200 (ESP32-S3 DWC_OTG ceiling) — unchanged from QMX, confirmed correct for the FTX-1 starting point.
- `cp210x_try_open();` at the FTX-1 dispatch remains unconditional, positioned before the candidate-scan block, not inside any `if (started)`/`if (!started)` branch — confirmed by direct read after edits.

## Build Verification Not Run

**`idf.py build` was NOT run in this executor session.** The ESP-IDF v5.5.1 toolchain is installed at `C:\Espressif\esp-idf-v5.5.1` but is not on PATH in Bash tool sessions — the same caveat documented in STATE.md for Phases 2 and 3 (`02-01`, `02-02` build verification also deferred for this reason). This is an **open item**, not silently skipped: a clean `-Werror` build of `main/stream_uac.cpp` with these three edits must be confirmed by the orchestrator or a manually-sourced ESP-IDF session before or during the 04-02 hardware-checkpoint plan. All edits were verified by direct code read and grep-based structural checks only, consistent with 04-RESEARCH.md's Environment Availability table.

## Known Stubs

None — this plan modifies existing profile-gated branches only; no new stub data paths, placeholder UI, or hardcoded empty values were introduced.

## Threat Flags

None — all three edits stay within the trust boundaries already documented in the plan's `<threat_model>` (T-04-01 FIFO DoS, T-04-02 malformed-descriptor tampering, T-04-03 speaker pre-open one-shot design); no new network endpoints, auth paths, or schema changes were introduced.

## Self-Check: PASSED

- FOUND: main/stream_uac.cpp (modified, confirmed via `git log --oneline -5 -- main/stream_uac.cpp`)
- FOUND: 6d729ea (feat(04-01): add FTX-1 mic candidate-scan branch)
- FOUND: 4be6be1 (feat(04-01): reuse QMX speaker pre-open path for UAC_PROFILE_FTX1)
- FOUND: fae5dba (feat(04-01): apply custom FIFO partition to UAC_PROFILE_FTX1)
