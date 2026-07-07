---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Phase 5 in progress (05-01 QSO checkpoint) - blocked on HCD channel ceiling
last_updated: "2026-07-07T11:30:00.000Z"
last_activity: 2026-07-07
progress:
  total_phases: 5
  completed_phases: 4
  total_plans: 8
  completed_plans: 8
  percent: 80
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-04)

**Core value:** A user with a Yaesu FTX-1 can plug it into the Cardputer over USB and run full FT8/FT4 QSOs — RX decode, autoseq, and TX — exactly as they already can with QMX/QDX/KH1.
**Current focus:** Phase 05 — end-to-end-integration-and-parity-testing

## Current Position

Phase: 5 in progress. Plans 01-01 through 04-02 complete. 05-01 (FT8 QSO checkpoint) in progress,
blocked on a hard hardware finding (see Blockers/Concerns) before a full QSO can be demonstrated.
Status: Executing Phase 05
Last activity: 2026-07-07

Progress: [████████░░] 80%

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
- 04-01: AUDIO-01/02/03 code implementation complete; validated on real hardware in 04-02 and now marked Complete in REQUIREMENTS.md
- 04-02: Physical hardware checkpoint passed — 25 consecutive error-free RX decode cycles (~6.8min) confirmed mic negotiation/waterfall continuity and FIFO stability under combined CAT+audio load; TX tone quality confirmed clean by user in prior testing. FIFO split unchanged (91/18/91, sum=200), no retune needed.
- 04-02 (hardware-topology finding): the Cardputer ADV's PC-facing USB-Serial/JTAG console and its USB-OTG host peripheral share the ESP32-S3's single USB D+/D- pin pair — the PC debug link (COM12) drops entirely whenever a USB host device (the FTX-1) is attached. A separate, independently-wired debug UART (CH340, COM5 in this session) must be used for any live logging while a USB host device is connected. This applies to any future hardware session on this board, not just Phase 4.
- 04-02 (correction, discovered 2026-07-07): the "TX tone confirmed clean by user in prior testing" claim in 04-02-SUMMARY.md is now doubtful. The 05-01 checkpoint discovered the speaker (UAC-OUT) interface had *never* successfully opened on this hardware (`spk pre-start failed at enum: ESP_ERR_NOT_FOUND` on every single boot, root-caused and fixed 2026-07-07 — see below). The Phase 4 TX-tone confirmation almost certainly reflected a PTT/keying check (radio visibly transmitting) rather than actual verified audio content, since no audio could have reached the radio before today's fix.
- 05-01 (2026-07-07, fixed): RTC entry-latency bug — manual UTC time entry via the on-screen editor had no compensation for human read/type latency, pushing FT8 decode timing outside its ~2.5s sync tolerance (observed offsets -1.3 to -1.6s pre-fix, decode rate ~36%). Added `rtc_nudge_seconds()` + `7`/`8` keys in STATUS view to fine-tune the clock ±1s against a live reference without retyping the full string. Confirmed on hardware: offsets collapsed to ~0.00s, decode rate recovered to near-100% (7/5/7 unique messages per cycle). Commit `9f34ae2`. Debug session: `.planning/debug/resolved/ft8-slot-boundary-rtc-timing.md`.
- 05-01 (2026-07-07, fixed): SD card Station.txt overwrite bug — `storage_sync_station_from_sd()` unconditionally overwrote internal Station.txt with the SD card's (possibly stale) copy on every boot, silently discarding any settings saved via `save_station_data()` (radio selection reverting to QMX after every power cycle). Gated the SD import to only fire when internal Station.txt doesn't already exist (preserves the original bootstrap/restore intent, commit `adc548d`). Confirmed on hardware: FTX-1 selection now persists across power cycles with an SD card inserted. Commit `bdd0f66`. Debug session: `.planning/debug/resolved/sd-station-txt-overwrite.md`.
- 05-01 (2026-07-07, fixed): FTX-1 speaker 24-bit rejection — the FTX-1's USB audio codec (C-Media-style chip, VID:0x0d8c PID:0x0016) only supports 16-bit sample resolution; the speaker (TX) path only ever requested a fixed 2ch/24-bit/48000Hz format with no fallback, so it always failed enumeration and no TX audio ever reached the radio (this was Phase 4's D-05 scenario, now hardware-confirmed). Added `dds_render_16bit_stereo()` and a speaker candidate-scan (24-bit then 16-bit fallback for FTX-1; QMX unchanged) mirroring the mic's existing pattern. Confirmed on hardware across 2 boot cycles: "Speaker pre-opened+claimed" now succeeds. Commit `ffe422c`. Debug session: `.planning/debug/resolved/ftx1-speaker-24bit-rejected.md`.
- 05-01 (2026-07-07, investigated, NOT fixed — see Blockers/Concerns): HCD channel exhaustion — fixing the speaker (above) revealed that mic+speaker cannot both be active simultaneously on this hardware topology, a hard ESP32-S3 hardware ceiling. Debug session: `.planning/debug/resolved/ftx1-hcd-channel-exhaustion.md`.

### Pending Todos

None yet.

### Blockers/Concerns

- FTX-1's exact VID/PID (0x10C4/0xEA70 CP2105) and CAT-1 interface index are unconfirmed from documentation alone — must validate on real hardware in 02-03-PLAN.md (hardware checkpoint)
- FTX-1's USB Audio descriptor (sample rate, bit depth, sync mode) is entirely unknown until probed in Phase 4
- FIFO/channel budget for a third simultaneous USB client (CP210x bulk CAT + bidirectional UAC) cannot be derived analytically — needs empirical hardware validation in Phase 4/5
- `idf.py build` has not been run against 02-01 or 02-02's changes in any executor session (ESP-IDF toolchain not on PATH in Bash sessions despite being installed at `C:\Espressif\esp-idf-v5.5.1`) — orchestrator or a manually-sourced session should confirm a clean `-Werror` build, including that `espressif/usb_host_cp210x_vcp` fetches correctly, before/during 02-03's hardware checkpoint
- `idf.py build` for 04-01's changes was confirmed clean (exit 0, no errors/warnings) during 04-02's hardware checkpoint, via a sourced ESP-IDF PowerShell session
- The exact winning mic candidate (channels/bit-depth) was not captured verbatim in 04-02 — the negotiation event fires once at FTX-1 USB attach, before debug-UART monitoring began on an already-running session. Negotiation success is strongly confirmed indirectly (25 consecutive error-free decode cycles), but if the literal candidate index is needed later, monitor COM5 from before the FTX-1 is physically attached.
- **MAJOR (2026-07-07): Mic (RX) and speaker (TX) cannot both be open simultaneously on the physical FTX-1 as currently wired.** The ESP32-S3's USB-OTG host controller has a hardware-fixed 8-channel ceiling (`GHWCFG2.NUMHSTCHNL`, read-only silicon register — not tunable via Kconfig or software). The FTX-1's actual USB topology (internal hub: 2 channels + CP2105 CAT-1: 3 channels + audio codec both directions: 3 channels + an unidentified Yaesu auxiliary device VID:0x26aa/PID:0x0030: 1 channel) requires 9 simultaneously-held channels once mic+speaker are both active — exceeding the ceiling by exactly 1. This was invisible before 2026-07-07's speaker fix, because the speaker never successfully claimed its channel prior to that (an accidental 8-channel fit). No safe software fix exists: VID/PID-based enumeration suppression of the auxiliary device is structurally impossible (its control pipe is allocated by ESP-IDF's `usbh.c` before VID/PID is ever readable during enumeration); port-based suppression would require hardcoding a fragile, unit-specific hub port mapping and was explicitly rejected as fragile. **This blocks any real FT8/FT4 QSO that requires simultaneous RX decode + TX capability — E2E-01/E2E-02 cannot be validated as "full QSO, both directions live" until this is addressed.** Recommended future direction (not attempted, real design work): close the mic's UAC pipe during each TX slot (FT8/FT4 is half-duplex within a single slot — you don't need to decode while transmitting) and reopen it immediately after, avoiding the need for 9 channels simultaneously. Real-time risk against the 15s/7.5s slot timing budget needs prototyping in a dedicated future session. Full channel-by-channel breakdown and research trail: `.planning/debug/resolved/ftx1-hcd-channel-exhaustion.md`.

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v2 requirement | POWER-01/POWER-02: TX power control via CAT (`PC1`) | Deferred to v2 | Roadmap creation |
| v2 requirement | HARDEN-01: Defensive DATA-U re-assertion during TX | Deferred to v2 (only if hardware confirms the quirk) | Roadmap creation |

## Session Continuity

Last session: 2026-07-06T22:00:00.000Z
Stopped at: Phase 4 complete (04-02 hardware checkpoint passed)
Resume file: none — run /gsd-plan-phase 5 to begin Phase 5
