---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Phase 5 in progress - 05-01 (FT8) hardware-verified complete with a real logged QSO; 05-02 (FT4) in progress, TX/RX/half-duplex confirmed working, full FT4 QSO completion not yet explicitly confirmed
last_updated: "2026-07-09T00:00:00.000Z"
last_activity: 2026-07-09
progress:
  total_phases: 5
  completed_phases: 4
  total_plans: 8
  completed_plans: 8
  percent: 85
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-04)

**Core value:** A user with a Yaesu FTX-1 can plug it into the Cardputer over USB and run full FT8/FT4 QSOs — RX decode, autoseq, and TX — exactly as they already can with QMX/QDX/KH1.
**Current focus:** Phase 05 — end-to-end-integration-and-parity-testing

## Current Position

Phase: 5 in progress. Plans 01-01 through 04-02 complete. 05-01 (FT8 QSO checkpoint) in progress.
The HCD channel-exhaustion blocker is RESOLVED (2026-07-07, half-duplex mic/speaker swap implementation).
TX trigger bug FIXED (2026-07-07, commit f138027) — added g_decode_applied_slot_idx update in arm_pending_tx()
to allow TX to fire when mic is closed for half-duplex.
Beacon-enable + STATUS-exit hang FIXED (2026-07-08, not yet committed) — race between main task and
audio/decode task in uac_ftx1_prepare_tx()/rx() (now mutex-guarded), unconditional STATUS-exit CAT
resync fighting the half-duplex TX arm (now skipped when TX just armed), and a defensive fix for
g_decode_applied_slot_idx freezing once the mic closes. Hardware-verified: two consecutive beacon CQ
cycles transmitted cleanly (79 tones each), RX resumed automatically after each. Debug session:
.planning/debug/resolved/ftx1-beacon-arm-status-hang.md.
TX audio silent bug FIXED (2026-07-08, not yet committed) — main.cpp's tx_start() only routed TX
through the sample-counted USB-audio path (uac_tx_begin_cpfsk) for RadioType::QDX; FTX-1 was never
added to that condition and fell through to a per-symbol CAT tone path whose FTX-1 backend
(ftx1_set_tone_hz) is a no-op stub. CAT PTT/keying worked, but zero audio ever reached the radio.
Fixed by adding RadioType::FTX1 to the QDX condition. A secondary defense-in-depth fix also added
(uac_tx_start_writer() now reads/logs FTX-1 speaker mute/volume and raises volume to 100 if below).
Hardware-verified: "UAC OUT CPFSK start" now fires every cycle, thousands of packets/cycle with zero
errors, user confirmed audio level visible on the FTX-1's own meter, AND external stations reported
receiving the beacon via PSKReporter (full RF confirmation). Debug session:
.planning/debug/resolved/ftx1-tx-audio-silent.md.
E2E-01 (full FT8 QSO) HARDWARE-VERIFIED COMPLETE (2026-07-08): beacon CQ answered by DL6EL, full
autoseq exchange (report -> RR73 -> mutual 73) completed and logged ("AUTOSEQ: Logged QSO: DL6EL
grid=JN57 rst_sent=10 rst_rcvd=16"), confirmed by both sides (DL6EL's own "73" decoded back).
A subsequent "5x full QSOs" user-run final regression test also completed with no errors reported.
This is the first real, complete, externally-confirmed FT8 QSO over the FTX-1 this milestone.
FT4 mode (E2E-02) IN PROGRESS (2026-07-08/09): switched via Menu key '6' (g_protocol_pending_ft4,
takes effect on reboot). Confirmed on hardware: correct FT4 parameters in TX (spacing=20.8333Hz,
count=105 symbols, 288-sample/7.5s-slot blocks), half-duplex mic/speaker swap completing well
within FT4's tighter 7.5s window with no hangs, autoseq processing decodes and generating replies
correctly (picked up YO4NF's reply to CQ, was replying to SV7BAY with a report when session paused).
A full FT4 QSO completion (mutual RR73/73, ADIF log entry) was NOT yet explicitly observed/confirmed
before the session ended for the day — this is the next thing to verify to close out Phase 5.
Three new UI features added and hardware-confirmed working (user request, not spec'd in ROADMAP):
(1) SNR display on RX list (small red/blue badge, blue if snr>0) and QSO log defaulting to the
call/R-SNR/S-SNR view; (2) manual coarse clock sync — '0' key on STATUS snaps the RTC to the nearest
FT8/FT4 slot boundary (rounds down, for human keypress latency); (3) QSO log combined view
(DD/MM HH:MM CALL +RR -SS in one line, replacing the old two-view toggle). See
.planning/quick/20260708-snr-display/, .planning/quick/20260708-manual-slot-sync/,
.planning/quick/20260708-qso-log-combined-view/.
MAJOR SIDE INCIDENT (2026-07-08/09, RESOLVED): internal FATFS storage partition became corrupted
(lost/orphaned clusters — 0 bytes free but empty directory listing when inspected via MSC/USB-drive
mode from a PC), most likely from an abrupt idf.py-flash hard-reset interrupting a filesystem write
during one of the day's many flash cycles. Symptom: every setting (radio selection, callsign, grid,
bands) reverted to default on every reboot, persistently. Fixed by reformatting the partition via
Windows (Format-Volume, FAT) while the device was in MSC mode — no firmware/code change needed.
Confirmed on hardware: Station.txt round-trips cleanly again, FTX-1 selection persists. Any local
QSO logs (.adi/.txt) on that partition were already inaccessible in the corrupted state before the
reformat, so nothing additionally recoverable was lost. Debug session:
.planning/debug/resolved/fatfs-station-txt-persistent-failure.md. Practical lesson for future
sessions: consider whether frequent flash-triggered hard resets during heavy iterative UI/feature
work pose an ongoing corruption risk to this partition — not investigated further this session.
IMPORTANT — NOTHING FROM TODAY IS COMMITTED YET. `git status` shows main/main.cpp,
main/stream_uac.cpp, components/ui/ui.cpp, components/ui/include/ui.h all modified in the working
tree, uncommitted. Last commit is still 5271f63 (TX trigger bug fix docs). A commit was never
requested by the user today — next session should either request one explicitly or be reminded
this is pending before further work risks losing track of what's committed vs. not.
Status: Executing Phase 05
Last activity: 2026-07-09

Progress: [████████▌░] 85%

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
- 05-01 (2026-07-07, investigated, NOT fixed — see Blockers/Concerns): HCD channel exhaustion — fixing the speaker (above) revealed that mic+speaker cannot both be active simultaneously on this hardware topology, a hard ESP32-S3 hardware ceiling. First debug session's "structurally impossible to reject the aux device" conclusion was later corrected (see next entry). Debug session: `.planning/debug/resolved/ftx1-hcd-channel-exhaustion.md`.
- 05-01 (2026-07-07, partial fix + conclusive hardware measurement): Found and hardware-verified a real, working fix for one contributing channel — ESP-IDF's public `usb_host_enum_filter_cb_t` API can reject the FTX-1's unused Yaesu auxiliary device (VID:0x26aa PID:0x0030) during enumeration, freeing its default control-pipe channel (confirmed via ESP-IDF's own `ENUM: Canceled enumeration` log line, not just this project's log). Kept in the tree (`main/stream_uac.cpp` `ftx1_enum_filter_cb()`, `sdkconfig` `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y`) — it is correct and harmless. However, direct instrumentation of the actual compiled HCD source (`managed_components/espressif__usb/src/hcd_dwc.c` — discovered this project compiles its own vendored copy of the `usb` component, not the global ESP-IDF install) measured `allocated=8 total=8` at the exact moment the mic's pipe-alloc fails: hub + CDC(CAT) + audio-control + speaker alone already consume the full 8-channel hardware ceiling (read from the ESP32-S3's GHWCFG2 silicon register) with zero margin, even with the aux device's channel already freed. A settle-delay hypothesis (mic negotiation racing the aux device's async cleanup) was also tested and disproven — the confirmed channel-free completes 150ms+ before the mic's failing attempt regardless. Conclusion at that point: simultaneous mic+speaker+CAT is a measured, zero-margin hardware wall on this exact USB topology.
- 05-01 (2026-07-07, RESOLVED): Implemented and hardware-verified the half-duplex mic/speaker redesign. Since mic and speaker are symmetric siblings on the FTX-1's audio device (same descriptor shape, 1 streaming pipe each) and FT8/FT4 never need simultaneous RX-decode + TX, they are now time-multiplexed for the FTX-1 profile only: `uac_ftx1_prepare_tx()`/`uac_ftx1_prepare_rx()` (main/stream_uac.cpp, declared in stream_uac.h) close one UAC stream and pre-claim (suspended, not yet streaming) the other, wired into `arm_pending_tx()` (single point of truth for every TX-scheduling path) and both TX-completion paths in main.cpp (`check_slot_boundary()`'s normal path and `tx_tick()`'s cancel path). No-op for QMX/QDX/KH1/generic-USB. Confirmed on real hardware across 9+ consecutive RX->TX->RX cycles (repeated CQ beaconing over several minutes): zero channel-exhaustion errors, real TX audio reaching the radio every cycle (79 tones sent), RX decode resuming cleanly afterward each time. One cosmetic ERROR-level log ("Unable to release UAC Interface" from the vendored `espressif__usb_host_uac` driver) appears on every mic-close but does not correlate with any actual leak — flagged as low-priority, not blocking. Debug session: `.planning/debug/resolved/ftx1-hcd-channel-exhaustion-halfduplex.md`.
- 05-01 (2026-07-07, FIXED, commit f138027): TX trigger bug — beacon CQ and all other TX paths were arming correctly (speaker swap completed, g_qso_xmit set, g_pending_tx valid) but `check_slot_boundary()` refused to actually fire TX because `g_decode_applied_slot_idx` was stale. Root cause: the guard `g_decode_applied_slot_idx >= slot_idx - 1` prevents TX from firing on stale state, but once `uac_ftx1_prepare_tx()` closes the mic for half-duplex swap, no further decodes happen to advance the index. Fix: added `g_decode_applied_slot_idx = rtc_now_ms() / g_protocol->slot_time_ms;` in `arm_pending_tx()` after the half-duplex swap, signaling that current state is valid even without a decode (beacon CQ doesn't need a decode to fire). Code ready for build/flash/hardware verification. Debug session: user interaction logs show beacon firing but TX never starting, waterfall stopped (mic closed), speaker suspended indefinitely.
- 05-01 (2026-07-08, FIXED, uncommitted): Beacon-enable + STATUS-exit hang — two independent bugs. (1)
  `uac_ftx1_prepare_tx()`/`uac_ftx1_prepare_rx()` (main/stream_uac.cpp) had no locking; the main task
  (`enter_mode()` on STATUS exit) and the audio/decode task (`decode_monitor_results()`) could race
  into `mic_close()`/`spk_open_and_claim()` concurrently, double-closing the same UAC mic handle and
  corrupting the vendored UAC driver's interface state, hanging the audio task permanently. Fixed with
  a FreeRTOS mutex (`s_ftx1_swap_mutex`) around both functions. (2) `enter_mode()` unconditionally
  called `sync_radio_to_current_band("STATUS exit")` immediately after `arm_pending_tx()`, forcing the
  radio back to RX mode via CAT right after the half-duplex TX-arm swap; fixed by skipping that call
  when a TX was just armed in the same invocation. A third defensive fix: `check_slot_boundary()` now
  advances `g_decode_applied_slot_idx` to `slot_idx-1` whenever `audio_source_is_streaming()` is false
  and it's fallen behind (no-op for QMX/QDX/KH1), preventing a second independent path to the same
  "TX never fires" symptom once the mic is closed for half-duplex. Hardware-verified: two consecutive
  beacon CQ cycles transmitted cleanly (79 tones each), RX resumed automatically after each. Debug
  session: `.planning/debug/resolved/ftx1-beacon-arm-status-hang.md`.
- 05-01 (2026-07-08, FIXED, uncommitted): TX audio silent bug — `tx_start()` (main/main.cpp) only
  routed TX through the sample-counted USB-audio path (`uac_tx_begin_cpfsk`) for `RadioType::QDX`;
  FTX-1 was never added to that condition and fell through to a per-symbol CAT tone path whose FTX-1
  backend (`ftx1_set_tone_hz`) is a no-op stub. CAT PTT/keying worked (radio visibly transmitted,
  clean-looking logs), but zero audio ever reached the radio — this was the real explanation for the
  Phase 4 "TX tone confirmed clean" claim already flagged as doubtful. Fixed by adding
  `RadioType::FTX1` to the QDX condition. A secondary defense-in-depth fix also added:
  `uac_tx_start_writer()` now reads/logs FTX-1 speaker mute/volume state post-resume and raises volume
  to 100 if reported lower (first hardware read showed vol=73 by default, not muted). Hardware-verified:
  "UAC OUT CPFSK start" now fires every cycle, thousands of packets/cycle with zero errors, user
  confirmed audio level visible on the FTX-1's own meter, AND external stations reported receiving the
  beacon via PSKReporter (full RF confirmation). Debug session:
  `.planning/debug/resolved/ftx1-tx-audio-silent.md`.
- 05-01/05-02 (2026-07-08, RESOLVED): Both above fixes hardware-verified together via a complete,
  real, logged FT8 QSO with DL6EL (report exchange -> RR73 -> mutual 73, confirmed both directions),
  plus a user-run "5x full QSOs" final regression test with no errors reported. FT4 mode subsequently
  confirmed working at the protocol level (correct TX parameters, half-duplex swap within the tighter
  7.5s window, autoseq processing decodes/replies) but a full FT4 QSO completion was not explicitly
  observed before the session ended.
- 2026-07-08/09 (RESOLVED, no code fix — infrastructure issue): Internal FATFS storage corruption —
  see "MAJOR SIDE INCIDENT" in Current Position above and
  `.planning/debug/resolved/fatfs-station-txt-persistent-failure.md`. Fixed via partition reformat
  (Windows Format-Volume through MSC/USB-drive mode), not a firmware change.

### Pending Todos

- WiFi connectivity for NTP sync and QRZ.com log upload — future milestone, needs feasibility check against existing USB Host + audio heap/HCD-channel budget first. See `.planning/todos/pending/20260708-wifi-ntp-qrz-upload.md`.
- FTX-1 Connect button (STATUS key '2') should force VFO mode before frequency sync — CAT `FA` command appears ignored if radio is in Memory-channel mode. Needs correct Yaesu CAT VFO-select command identified against the FTX-1's own CAT reference + hardware verification, not a guess. See `.planning/todos/pending/20260708-ftx1-connect-force-vfo-mode.md`.

### Blockers/Concerns

- FTX-1's exact VID/PID (0x10C4/0xEA70 CP2105) and CAT-1 interface index are unconfirmed from documentation alone — must validate on real hardware in 02-03-PLAN.md (hardware checkpoint)
- FTX-1's USB Audio descriptor (sample rate, bit depth, sync mode) is entirely unknown until probed in Phase 4
- FIFO/channel budget for a third simultaneous USB client (CP210x bulk CAT + bidirectional UAC) cannot be derived analytically — needs empirical hardware validation in Phase 4/5
- `idf.py build` has not been run against 02-01 or 02-02's changes in any executor session (ESP-IDF toolchain not on PATH in Bash sessions despite being installed at `C:\Espressif\esp-idf-v5.5.1`) — orchestrator or a manually-sourced session should confirm a clean `-Werror` build, including that `espressif/usb_host_cp210x_vcp` fetches correctly, before/during 02-03's hardware checkpoint
- `idf.py build` for 04-01's changes was confirmed clean (exit 0, no errors/warnings) during 04-02's hardware checkpoint, via a sourced ESP-IDF PowerShell session
- The exact winning mic candidate (channels/bit-depth) was not captured verbatim in 04-02 — the negotiation event fires once at FTX-1 USB attach, before debug-UART monitoring began on an already-running session. Negotiation success is strongly confirmed indirectly (25 consecutive error-free decode cycles), but if the literal candidate index is needed later, monitor COM5 from before the FTX-1 is physically attached.
- **MAJOR (2026-07-07, updated with conclusive hardware measurement): Mic (RX) and speaker (TX) cannot both be open simultaneously on the physical FTX-1 as currently wired.** The ESP32-S3's USB-OTG host controller has a hardware-fixed 8-channel ceiling (`GHWCFG2.NUMHSTCHNL`, read-only silicon register — not tunable via Kconfig or software, confirmed by direct instrumentation reading `chan_num_total` at runtime = 8). A working fix WAS found and hardware-verified for one contributing channel: ESP-IDF's public `usb_host_enum_filter_cb_t` mechanism (`main/stream_uac.cpp` `ftx1_enum_filter_cb()`, `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y`) correctly rejects the FTX-1's unused Yaesu auxiliary device (VID:0x26aa/PID:0x0030) during enumeration and frees its channel (confirmed via ESP-IDF's own `ENUM: Canceled enumeration` log). This fix is kept in the tree — it is correct and harmless — but direct instrumentation of the actual channel allocator (`managed_components/espressif__usb/src/hcd_dwc.c`, temporarily) measured `allocated=8 total=8` at the exact moment the mic's pipe-alloc fails, **even with the aux device's channel already freed**: hub (2, required for the FTX-1's internal 3-device USB topology) + CDC/CAT-1 (3) + audio device control+speaker (2) = 8, with zero margin, before the mic is even attempted. A timing-race hypothesis (aux cleanup not yet complete) was also tested and disproven via a deliberate settle delay that made no difference. **This is now a measured, zero-margin hardware wall, not an unexplored channel to keep hunting for — no further software-only channel-freeing avenue remains among the load-bearing devices (hub/CDC/audio-control are all required; only the aux device, already reclaimed, was expendable). This blocks any real FT8/FT4 QSO that requires simultaneous RX decode + TX capability — E2E-01/E2E-02 cannot be validated as "full QSO, both directions live" until this is addressed.** Recommended future direction (not attempted, real design work): close the mic's UAC pipe during each TX slot (FT8/FT4 is half-duplex within a single slot — you don't need to decode while transmitting) and reopen it immediately after, avoiding the need for 9 channels simultaneously. Real-time risk against the 15s/7.5s slot timing budget needs prototyping in a dedicated future session. Full channel-by-channel breakdown and research trail: `.planning/debug/resolved/ftx1-hcd-channel-exhaustion.md` (original) and `.planning/debug/ftx1-hcd-channel-exhaustion.md` (this session's conclusive measurement, not yet moved to resolved/).

## Deferred Items

Items acknowledged and carried forward from previous milestone close:

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| v2 requirement | POWER-01/POWER-02: TX power control via CAT (`PC1`) | Deferred to v2 | Roadmap creation |
| v2 requirement | HARDEN-01: Defensive DATA-U re-assertion during TX | Deferred to v2 (only if hardware confirms the quirk) | Roadmap creation |

## Session Continuity

Last session: 2026-07-09T00:00:00.000Z
Stopped at: Phase 5 — E2E-01 (FT8) hardware-verified complete with a real logged QSO (DL6EL) plus a
5x-QSO regression pass; E2E-02 (FT4) in progress, protocol-level behavior confirmed working
(correct TX params, half-duplex swap, autoseq decode/reply) but no full FT4 QSO completion observed
yet. Also fixed today: the beacon-arm/STATUS-exit hang, the TX-audio-never-reached-radio bug, and an
unrelated internal-storage corruption incident (reformatted, resolved). Added three small UI
features (SNR display, manual slot sync, combined QSO log view) at user request.
UNCOMMITTED: main/main.cpp, main/stream_uac.cpp, components/ui/ui.cpp,
components/ui/include/ui.h all have working-tree changes from today, none committed. Last commit
is still 5271f63. Next session should confirm with the user whether to commit before continuing.
Resume file: none — next step is completing/confirming a full FT4 QSO (E2E-02), then Phase 5 can be
marked complete and the milestone closed out (ROADMAP.md/REQUIREMENTS.md updates, commit).
