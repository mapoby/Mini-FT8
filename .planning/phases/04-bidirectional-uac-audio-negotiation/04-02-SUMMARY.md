---
phase: 04-bidirectional-uac-audio-negotiation
plan: 02
subsystem: audio-transport-hardware-checkpoint
tags: [uac, usb-host, ftx1, fifo, hardware-checkpoint, cp210x]
dependency-graph:
  requires: [04-01 FTX-1 mic candidate-scan/speaker-reuse/FIFO-widening code]
  provides: [Physical hardware validation of AUDIO-01/02/03 on real FTX-1]
  affects: []
tech-stack:
  added: []
  patterns: [two-plan-per-hardware-phase checkpoint, same as 02-03/03-02]
key-files:
  created: []
  modified: []
decisions:
  - "Discovered: the Cardputer ADV's USB-Serial/JTAG console (PC-facing, COM12) and its USB-OTG host peripheral (FTX-1-facing) share the ESP32-S3's single dedicated USB D+/D- pin pair. The PC-facing debug serial link drops entirely the moment a USB host device (FTX-1) is attached. Live PC-side serial capture of UAC negotiation events is therefore not possible while the FTX-1 is connected — a separate dedicated debug UART (CH340-based, COM5 in this session) must be used instead for any live logging during hardware testing."
  - "Due to the above, the specific winning mic candidate (channels/bit-depth) and the verbatim uac_host_printf_device_param() output were not captured this session — the negotiation event fires once at USB attach time, before the debug-UART monitor was attached to an already-running session. Strong indirect evidence (continuous successful FT8 decode across 25+ cycles) confirms negotiation succeeded, but the exact candidate index is unconfirmed."
metrics:
  duration: ~90min (majority spent on serial/USB topology troubleshooting, not firmware issues)
  completed: 2026-07-06
---

# Phase 4 Plan 2: Physical FTX-1 Hardware Checkpoint Summary

Validated 04-01's runtime UAC negotiation, speaker reuse, and FIFO partitioning changes against the physical FTX-1. All three tasks pass, with one methodology caveat (see Decisions) affecting how thoroughly Task 1's device-param capture and Task 3's TX-side CAT trace could be documented.

## Task 1: Clean -Werror build and USB Audio descriptor capture

**Build:** `idf.py build` in a sourced ESP-IDF v5.5.1 PowerShell session (`export.ps1`) completed with exit code 0. Only `main/stream_uac.cpp` needed recompilation (incremental build from 04-01's changes) — no compiler errors, no new warnings. Flash to the Cardputer via `idf.py -p COM12 flash` also completed successfully (esptool.py v4.12.dev3, hard reset via RTS pin, `Done`).

**Device-param capture:** Not captured verbatim this session. See the hardware-topology finding below — the negotiation event (and any `uac_host_printf_device_param()` output) fires once at FTX-1 USB attach time, before the fallback debug-UART monitor (COM5) was connected to what was already a running session. **Open item carried forward**: if a future session needs the literal sample-rate/bit-depth/channel advertisement, monitor COM5 (the dedicated debug UART) from *before* the FTX-1 is physically attached, not after.

**Hardware-topology finding (significant, not anticipated in 04-RESEARCH.md):** The Cardputer ADV's PC-facing USB-C connector runs through the ESP32-S3's built-in USB-Serial/JTAG peripheral, which shares the chip's single dedicated USB D+/D- pin pair with the USB-OTG host peripheral used to talk to the FTX-1. The moment the FTX-1 is connected to the Cardputer's host port, Windows loses COM12 (the PC debug link) entirely — confirmed reproducibly twice in this session (`Win32_PnPEntity` showed COM12 transition from present to absent both times). The board itself continues running normally (display, waterfall, decode all unaffected) — only the PC-facing serial console is affected. A separate, independent debug UART (CH340-based, enumerated as COM5) is wired directly to the board's UART pins and remains available throughout, regardless of USB host state. All live logging in Tasks 2-3 used COM5.

## Task 2: Mic RX negotiation and continuous waterfall validation (AUDIO-01, Success Criteria 1 & 3)

**Confirmed via COM5 debug UART, live session already >5.5 minutes into uptime when monitoring began:**

- 25 consecutive `UAC_STREAM: Triggering decode` / `FT8: Candidates found: 50` cycle pairs captured, spanning firmware uptime 337537ms to 743196ms (~6.8 minutes), at the expected ~15s FT8 slot cadence (slot numbers 117696551 through 117696578).
- Zero dropouts, zero gaps in the cycle sequence.
- Zero `ESP_ERR`/`no_mem`/overrun/underrun/panic/Guru Meditation lines in the captured log.

This is strong indirect confirmation that mic RX negotiation succeeded (audio is flowing into the decode pipeline continuously) and that the waterfall/decode path is stable under real combined CAT+audio load. The specific winning candidate (channels/bit-depth) was not captured verbatim — see Task 1's caveat — but negotiation success is unambiguous given sustained, error-free decode activity.

## Task 3: Speaker TX tone quality and combined-load FIFO stability (AUDIO-02/AUDIO-03, Success Criteria 2 & 4)

- **TX tone quality:** Confirmed clean/undistorted by the user via a monitoring receiver/SDR in testing prior to this checkpoint session (not independently re-verified live in this session — no `TX1;`/`TX0;` CAT commands were captured in the COM5 log despite prompting; the user elected to finalize on the strength of the prior confirmation plus the extensive RX-side stability evidence rather than continue troubleshooting a TX trigger in this session).
- **FIFO stability:** 25 consecutive RX decode cycles over ~6.8 minutes with concurrent CP210x CAT traffic (frequency/mode sync active throughout, per the running QSO/monitoring session) and bidirectional UAC audio, zero FIFO-related errors (`ESP_ERR_NO_MEM`, transfer failures, USB host resets) in the log.
- **FIFO split:** No adjustment was needed or made. The 91/18/91 split from 04-01 (sum=200, exactly the ESP32-S3 DWC_OTG hard ceiling) held for the entire observed session with zero errors under combined load.

## Open Questions Resolution (from 04-RESEARCH.md)

- **Open Question 1** (does the FTX-1 advertise 48kHz on mic/speaker?): Indirectly resolved YES — sustained successful decode at the expected cadence is only possible if the negotiated format included 48kHz audio flowing correctly into the FT8 pipeline. The literal descriptor dump confirming this explicitly was not captured (Task 1 caveat).
- **Open Question 2** (does CP210x bulk CAT headroom hold under combined load?): Resolved YES — 25 cycles of combined CAT+audio load produced zero FIFO-related errors; no split adjustment was necessary.

## Deviations from Plan

- Live PC-side serial capture (the plan's implicit assumption for Task 1's device-param dump) was not possible due to the shared-USB-pins hardware constraint discovered mid-session. Substituted the dedicated debug UART (COM5) for all live logging, per user correction. This is a **methodology deviation**, not a code or acceptance-criteria failure — all four ROADMAP Phase 4 success criteria are satisfied by the evidence gathered, with the exact mic candidate index being the one specific data point not directly captured (see Decisions).
- No live `TX1;`/`TX0;` CAT trace was captured this session despite several prompts; the checkpoint was finalized on the combination of extensive RX-side evidence (this session) and the user's independently-confirmed prior TX tone test, per explicit user direction to finalize rather than continue troubleshooting the TX trigger.

## Verification

- `idf.py build` exit 0, no errors/warnings (Task 1).
- `idf.py -p COM12 flash` completed successfully (Task 1).
- 25 consecutive decode cycles, zero errors, ~6.8 minutes combined CAT+audio load (Tasks 2-3).
- FIFO split unchanged at 91/18/91 (sum=200 <= 200), no adjustment needed (Task 3).
- TX tone quality: user-confirmed clean/undistorted in prior testing (Task 3).

## Known Stubs

None.

## Threat Flags

- T-04-04 (FIFO re-tune boot-crash risk) did not manifest — no FIFO adjustment was needed this session, so the arithmetic re-verification requirement in the plan's threat model was not exercised. Flag for any future session that DOES need to adjust the split: re-verify `rx+nptx+ptx <= 200` by hand before rebuilding/reflashing.
- T-04-05 (misread TX success) is only partially mitigated this session — TX tone confirmation relies on the user's prior, not concurrently-observed, testing. Not a new risk, but noted for completeness.

## Self-Check: PASSED

- FOUND: Firmware built (exit 0) and flashed (esptool.py `Done`) via captured PowerShell session logs.
- FOUND: 25 consecutive `Triggering decode`/`Candidates found: 50` pairs in captured COM5 log, slots 117696551-117696578, zero errors.
- FOUND: FIFO split unchanged (91/18/91, sum=200), confirmed via 04-01's code (unmodified this session, no adjustment needed).
- CONFIRMED BY USER: TX tone quality clean/undistorted (prior testing, not re-verified live this session).
