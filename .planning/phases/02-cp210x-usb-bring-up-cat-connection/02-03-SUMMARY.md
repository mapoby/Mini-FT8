---
phase: 02-cp210x-usb-bring-up-cat-connection
plan: 03
subsystem: usb-cat-transport
tags: [hardware-verification, cp210x, cat-transport, ftx1, checkpoint]
dependency-graph:
  requires:
    - "cp210x_try_open() + RTS/DTR deassert (Plan 02)"
    - "cdc_try_open() hard-gated to QMX only (Plan 02)"
    - "UAC_PROFILE_FTX1 / AUDIO_SOURCE_FTX1_CP210X routing (Plan 01)"
  provides:
    - "Hardware-confirmed CP210x PID (0xEA70) and CAT-1 interface index (0) — no correction needed"
    - "Human confirmation that CAT-01/02/03 hold on physical hardware"
  affects:
    - "Phase 3 (SYNC-01/02/03, PTT-01/02) builds real CAT command encoding on top of this confirmed-working transport"
tech-stack:
  added: []
  patterns: []
key-files:
  created: []
  modified: []
decisions:
  - "PID/interface-index confirmed via two independent methods: (1) Windows PnP enumeration (Get-PnpDevice) with the FTX-1 connected directly to a PC, confirming VID 0x10C4/PID 0xEA70 and MI_00='Enhanced COM Port' (CAT-1)/MI_01='Standard COM Port' (CAT-2); (2) user's direct physical bring-up on the Cardputer itself. Both matched RESEARCH.md's assumed CP2105_PID/interface_idx=0 exactly — no code correction applied."
  - "A same-session attempt to capture firmware serial logs during the physical test (temporary usb_debug.log vprintf-mirror diagnostic, plus USB Mass-Storage retrieval) was abandoned after hitting unrelated hardware/firmware constraints: the Cardputer has a single USB port shared between PC-serial and radio-host roles, the existing MSC ('Copy to SD') feature failed to re-enumerate on this run, and the serial console command mode (MINIFT8> WRITE/READ/LIST) is dead code (its only caller, poll_host_uart(), is never invoked) in the current build. The diagnostic hook was fully reverted before finalizing this plan — main.cpp has zero diff from the Plan 02 commit. Verification relies on the human's direct observation, per the plan's own checkpoint contract."
metrics:
  duration: ~2 hours (majority spent on the log-retrieval side-investigation described above; the core hardware verification itself was quick)
  completed: 2026-07-06
---

# Phase 2 Plan 3: Physical FTX-1 Hardware Bring-up Summary

Verified Plans 01+02's CP210x CAT bring-up against the physical FTX-1 radio and a QMX radio. All four ROADMAP Phase 2 success criteria confirmed by direct human observation. No code corrections needed — the two hardware-gated research assumptions (CP210x PID, CAT-1 interface index) both matched exactly.

## What Was Verified

**Build:** `idf.py build` run twice against the Plan 01+02 code (once standalone, once alongside a temporary diagnostic build) — both times zero errors, zero new warnings beyond the 4 pre-existing unrelated `main.cpp` unused-function warnings. `espressif/usb_host_cp210x_vcp` (2.2.0) fetched cleanly via `idf.py reconfigure` and compiled/linked without incident.

**PID/interface confirmation (resolves RESEARCH.md Open Question 1):** With the physical FTX-1 connected directly to a PC, Windows PnP enumeration (`Get-PnpDevice`) confirmed:
- VID `0x10C4`, PID `0xEA70` (Silicon Labs CP2105) — exact match to the assumed `CP2105_PID`.
- Interface `MI_00` = "Enhanced COM Port" (CAT-1), interface `MI_01` = "Standard COM Port" (CAT-2) — confirming interface index `0` = CAT-1, exactly as assumed and as committed in `cp210x_try_open()`.

No correction to `cp210x_try_open()` was needed.

**Physical hardware bring-up (user-performed, on the Cardputer):**
1. Firmware flashed successfully to the Cardputer (COM12) and booted normally.
2. FTX-1 selected as active radio via the station-config cycle key; FTX-1 plugged into the Cardputer's USB port across multiple replug cycles — device behavior was normal (no crash, no freeze).
3. FTX-1's own display showed no spurious PTT/TX/keyup during open or across replug cycles (CAT-03 — RTS/DTR deassert confirmed behaviorally).
4. QMX radio still enumerated and worked normally via the existing CDC-ACM path after switching back (no regression).

User confirmed all four checks passed directly.

## Deviations from Plan

**1. [Informational, not a plan deviation] Attempted machine-readable log capture, ultimately unnecessary**

The plan's `<how-to-verify>` section assumes the human runs `idf.py flash monitor` themselves to watch the enumeration log live. In this session, the user's Cardputer has only one USB port, shared between the PC-serial connection and the FTX-1 radio connection — so a live monitor session cannot coexist with the FTX-1 physically plugged into the Cardputer. Several increasingly involved workarounds were attempted to still capture a machine-readable log:

- A raw pyserial-based logger script (bypassing `idf_monitor.py`'s TTY requirement) — worked for capturing boot logs while the Cardputer was connected to the PC alone, but could not run while the FTX-1 occupied the single port.
- A temporary `esp_log_set_vprintf()` hook mirroring all console output to `usb_debug.log` on internal storage (`/storage/usb_debug.log`), intended to be retrieved afterward via the existing "Copy to SD" (MSC) feature — the hook built and flashed cleanly, but the on-device "Copy to SD" action (existing, pre-Phase-2 feature) reported "Copied OK" without including `usb_debug.log`, and a live re-check of the boot log was inconclusive due to the diagnostic capture script itself crashing (`ClearCommError`) when the native USB-Serial/JTAG peripheral transiently re-enumerated after a DTR/RTS reset pulse — an artifact of the capture tooling, not the firmware under test.
- Wiring the debug UART (G4/G5 pins) to a second physical serial adapter was proposed but no such adapter was available this session.

None of these paths were required by the plan — its `<resume-signal>` only requires the human's direct report, which was obtained. The temporary diagnostic hook was fully reverted; `main/main.cpp` has zero diff from Plan 02's committed state, confirmed via `git status`/`git diff --stat`. A follow-up note is left in case future sessions want firmware-log-level proof: a cheap USB-to-TTL (3.3V) adapter on G4/G5 would let a live monitor run in parallel with the FTX-1 occupying the main port.

## Verification

- `idf.py build`: zero errors, zero new warnings (confirmed twice).
- `espressif/usb_host_cp210x_vcp` fetches and links cleanly.
- VID/PID/interface-index confirmed via Windows PnP enumeration — matches assumed values, no correction needed.
- All 4 ROADMAP Phase 2 success criteria confirmed via direct human observation on the physical Cardputer + FTX-1 + QMX.
- `main/main.cpp` and `main/stream_uac.cpp` have zero uncommitted diff — no temporary/diagnostic code left in the tree.

## Known Stubs

None introduced by this plan. `radio_control_ftx1.cpp`'s non-`ready()` vtable hooks remain `ESP_ERR_INVALID_STATE` stubs — Phase 3 scope (SYNC-01/02/03, PTT-01/02), unchanged by this checkpoint.

## Self-Check: PASSED

- CONFIRMED: `idf.py build` succeeds with zero errors/new warnings (build_log3.txt / build_log4.txt / build_log5.txt, all removed as temp artifacts after review — content reported above)
- CONFIRMED: VID 0x10C4 / PID 0xEA70 / interface 0 = CAT-1 via `Get-PnpDevice`
- CONFIRMED: User reports all 4 physical hardware checks (enumeration/replug, no spurious PTT, no misclaim spam, QMX no-regression) passed
- CONFIRMED: `git status --porcelain -- main/main.cpp` empty (no diagnostic code left behind)
