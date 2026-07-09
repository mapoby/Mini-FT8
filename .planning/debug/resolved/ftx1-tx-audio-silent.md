---
status: resolved
resolved: 2026-07-08
resolution_summary: |
  Root cause was NOT mute/volume (that was a red herring investigated first): main/main.cpp's
  tx_start() only routed TX through the sample-counted USB-audio path (uac_tx_begin_cpfsk) for
  RadioType::QDX. FTX-1 was never added to that condition, so it fell through to the per-symbol CAT
  tone path (tx_send_ta -> radio_control_set_tone_hz), whose FTX-1 backend implementation
  (ftx1_set_tone_hz) is a no-op stub. CAT PTT/keying worked correctly (radio visibly transmitted,
  firmware logged clean 79-tone cycles), but zero PCM audio was ever generated or written to the
  FTX-1's UAC-OUT endpoint. Fixed by adding RadioType::FTX1 to the QDX-only condition in tx_start()
  (main/main.cpp ~line 3407), so FTX-1 now drives uac_tx_begin_cpfsk() -> uac_tx_start_writer() ->
  the speaker writer task exactly like QDX's already-working audio path.
  A secondary, harmless fix (round 1) was kept as defense-in-depth: uac_tx_start_writer() now reads
  back and logs FTX-1 speaker mute/volume state after resume, and raises volume to 100 if reported
  lower (first hardware confirmation showed vol=73 by default, not muted).
  Confirmed on real hardware: "UAC OUT CPFSK start" now fires every TX cycle, thousands of audio
  packets sent with zero errors per cycle across 5+ consecutive beacon cycles, user visually confirmed
  audio level on the FTX-1's own meter, AND external stations reported receiving the beacon via
  PSKReporter — full external RF confirmation, not just USB/firmware-side.
trigger: |
  User: "i can't see audio level when beacon is trasmitted on radio" — clarified this means the
  FTX-1's own front-panel/ALC audio-input meter shows no deflection at all while the Cardputer is
  transmitting a beacon CQ, even though the radio visibly keys up (PTT/CAT confirmed working) and
  the firmware logs 79 tones sent successfully.
created: 2026-07-08
updated: 2026-07-08
---
# ftx1-tx-audio-silent

## Symptoms

- Expected: while beacon CQ is transmitting, the FTX-1's own meter should show audio level
  corresponding to the FT8 tone being sent (single-tone FSK, should read a steady, non-zero level).
- Actual: FTX-1's meter shows no deflection at all during TX — appears completely silent to the
  radio, despite CAT PTT/keying and the firmware's tx_tick()/uac_host_device_write() logging success
  with zero write errors ("FTX-1 TX start" ... "tx_tick: TX complete, all 79 tones sent" ... "FTX-1 TX
  stop", no spk write err lines).
- This is the first real-audio verification since the half-duplex hang fix
  (.planning/debug/resolved/ftx1-beacon-arm-status-hang.md) unblocked TX from ever completing a full
  cycle on hardware. STATE.md already flagged the earlier 04-02 "TX tone confirmed clean" claim as
  doubtful — likely a PTT-only visual check, not actual verified audio content, since no audio could
  reach the radio before this session's fixes (speaker never opened successfully until 05-01's 24-bit
  rejection fix, and TX never fired at all until f138027 and today's hang fix).
- Timeline: first hardware attempt where beacon TX ran to completion with the speaker actually open.
- Reproduction: 100% so far — every beacon TX cycle in this session's hardware test (multiple
  consecutive cycles observed) showed the same "keys up but meter reads nothing" behavior per user.

## Evidence

- source: main/dds_q15.cpp, code reading (no hardware instrumentation yet)
  content: |
    dds_render_16bit_stereo() (used for FTX-1's negotiated 16-bit speaker format) packs
    sin_q15_from_phase() output directly as full-scale int16 PCM (both stereo channels identical,
    little-endian) via emit_stereo_frame_16(). sin_q15_from_phase() returns values across the full
    int16_t range (-32768..32767) driven by a proper quarter-wave sine table with linear
    interpolation — this is not a stub/silent renderer, it should produce a strong single-tone signal
    at whatever frequency dds_cpfsk_begin()/s_inc set.
  interpretation: |
    The DDS layer itself is very unlikely to be the cause — it's a straightforward, apparently-correct
    full-scale tone generator, effectively unchanged in logic between the working 24-bit path (used by
    QMX previously) and the new 16-bit path added for the FTX-1's codec.

- source: main/stream_uac.cpp, code reading + grep
  content: |
    grep for "volume|mute|feature_unit|set_cur" across main/stream_uac.cpp returns zero matches
    outside of an unrelated mutex variable name. No call anywhere to
    uac_host_device_set_mute()/uac_host_device_set_volume()/uac_host_device_set_volume_db(), despite
    the vendored managed_components/espressif__usb_host_uac driver publicly exposing all three
    (include/usb/uac_host.h lines ~399-466). The speaker open path (spk_open_and_claim(), around line
    ~700-745) only opens, claims the interface, and (for FTX-1) leaves it suspended until
    uac_tx_start_writer() calls uac_host_device_resume(). No volume/mute control of any kind is issued
    at any point in the speaker lifecycle.
  interpretation: |
    Leading hypothesis: the FTX-1's USB audio codec (C-Media-style chip, VID:0x0d8c PID:0x0016) may
    power up with its output feature unit muted or at zero/minimum volume by default (common for
    cheap UAC codecs — many require an explicit host-side SET_CUR unmute/volume command before
    producing any output, since the USB Audio spec does not mandate a specific default state). Since
    this firmware never issues that command for either 24-bit (QMX/original) or 16-bit (FTX-1) paths,
    a device that happens to default unmuted (or has no mute/volume control at all) would work by
    accident, while one that defaults muted would be permanently silent regardless of correct PCM
    data — which matches the observed symptom exactly.

- source: managed_components/espressif__usb_host_uac/include/usb/uac_host.h, doc-comment reading
  content: |
    uac_host_device_get_mute/get_volume/set_mute/set_volume all document
    ESP_ERR_INVALID_STATE "if the device is not ready or active" as a possible return. spk_open_and_
    claim() opens the speaker with FLAG_STREAM_SUSPEND_AFTER_START (no URBs queued, stream not
    actively running) and only calls uac_host_device_resume() later, inside uac_tx_start_writer(),
    right before the writer task starts pumping PCM.
  interpretation: |
    Calling get/set_mute/volume immediately after spk_open_and_claim() (as originally planned) risks
    hitting the device while it's still suspended, which could return ESP_ERR_INVALID_STATE and give
    a false negative on the "does this device support mute/volume" question. Moved the diagnostic +
    fix to fire immediately after uac_host_device_resume() succeeds in uac_tx_start_writer(), when the
    stream is guaranteed active, so the control transfer has the best chance of hitting real device
    state.

## Hypotheses considered

- hypothesis: FTX-1 speaker feature unit defaults to muted/zero-volume and firmware never sends an
    unmute/set-volume command, so output stays silent despite correct PCM data reaching a fully open,
    resumed streaming endpoint.
  status: leading candidate — diagnostic + fix implemented, awaiting hardware confirmation
- hypothesis: Wrong USB Audio terminal/alt-setting selected — the 16-bit candidate format may map to
    a different (non-TX-routed) output path on the FTX-1's internal audio codec than whatever terminal
    is physically wired to the transmitter modulator, independent of software mute state.
  status: unexplored, becomes primary if get_mute/get_volume both return ESP_ERR_NOT_SUPPORTED (no
    feature unit at all) or return unmuted/full-volume yet the meter is still silent after the fix
- hypothesis: PCM data is correct and unmuted, but level is simply too low for the radio's ALC/meter
    to register (gain/attenuation issue, not full silence).
  status: unlikely per user's description ("no audio level... at all", not "very low") but worth
    distinguishing from true silence during hardware verification

## Current Focus

hypothesis: FTX-1 speaker feature unit defaults muted/zero-volume; firmware never unmutes/sets volume.
test: Implemented in main/stream_uac.cpp's uac_tx_start_writer(), immediately after
  uac_host_device_resume() succeeds, gated to `s_profile == UAC_PROFILE_FTX1` only (no-op for
  QMX/QDX/KH1). Calls uac_host_device_get_mute()/uac_host_device_get_volume(), logs both results
  (ESP_LOGI "FTX-1 spk state: get_mute=... get_volume=..."), then:
  - if get_mute succeeded and reports muted -> uac_host_device_set_mute(handle, false), logged
  - if get_volume succeeded and reports <100 -> uac_host_device_set_volume(handle, 100), logged
  ESP_ERR_NOT_SUPPORTED from either get call is handled gracefully (no set attempted, just logged) —
  a valid outcome meaning the FTX-1's codec has no mute/volume feature unit, which would eliminate
  this hypothesis and promote the terminal/routing hypothesis to primary.
  Build verified clean (idf.py build succeeded, only stream_uac.cpp.obj recompiled).
expecting: get_mute() returns true and/or get_volume() returns 0 (or a very low value), confirming
  the device powers up muted/silent. After the explicit unmute+set_volume fix, the FTX-1's meter
  should show a steady non-zero level during the next beacon TX cycle.
next_action: |
  HARDWARE VERIFICATION REQUIRED (checkpoint) — cannot proceed further without physical radio access:
  1. Detach FTX-1 from Cardputer (COM12 must be free for flashing).
  2. Flash: `idf.py -p COM12 flash` (or use the already-built MiniFT8_Merged_Auto.bin) from a
     PowerShell session with `Remove-Item Env:\MSYSTEM` run first if using export.ps1 in this repo's
     dev environment — MSYSTEM being set breaks idf_tools.py's Python-dependency check.
  3. Reattach FTX-1 to Cardputer.
  4. Start/attach COM5 debug UART capture (CH340 adapter).
  5. Run a beacon TX cycle; watch the FTX-1's own front-panel/ALC meter during transmission.
  6. Report back: (a) what the meter showed (silent / non-zero), and (b) the exact COM5 log line(s)
     starting "FTX-1 spk state: get_mute=... get_volume=..." plus any following unmute/set_volume
     result lines.

## Hardware Verification Round 1 (mute/volume fix)

- Flashed the mute/volume diagnostic fix, re-enabled beacon, ran 2 full TX cycles on hardware.
- Result: user confirmed "beacon trasmitted, no audio on ftx-1" — still completely silent.
- Critically: grep of the COM5 capture across both TX cycles found ZERO occurrences of the expected
  "FTX-1 spk state: get_mute=..." diagnostic log line, and ZERO occurrences of "UAC OUT CPFSK start"
  (the ESP_LOGI at the end of uac_tx_begin_cpfsk() on success) or any "UAC OUT ..." / "CPFSK" log at
  all. This means uac_tx_start_writer() — and therefore the entire mute/volume diagnostic block — was
  never even reached. The fix from round 1 was correct but dead code for FTX-1's actual TX path.

## Eliminated

- hypothesis: FTX-1 speaker feature unit defaults to muted/zero-volume, blocking otherwise-correct
    audio.
  reason: Cannot be true as stated — uac_tx_start_writer() (which contains the only mute/volume
    control code, and is also the ONLY place that starts the speaker writer task pumping PCM via
    dds_render_16bit_stereo()/uac_host_device_write()) is never called at all during an FTX-1 TX
    cycle. The real defect is upstream of any mute/volume question — see Root Cause below.

## Root Cause (confirmed via static analysis, hardware-verification pending)

- file: main/main.cpp, function tx_start(), ~line 3407 (pre-fix)
  finding: |
    `if (g_tx_cat_ok && canonical_radio_type(g_radio) == RadioType::QDX) { ... uac_tx_begin_cpfsk(...) ... }`
    only routes TX through the USB-audio path for QDX. FTX-1 (a distinct RadioType, canonical_radio_type
    never collapses it into QDX) falls through to the "else" per-symbol CAT path, which calls
    tx_send_ta() -> radio_control_set_tone_hz() every symbol. main/radio_control_ftx1.cpp's
    `ftx1_set_tone_hz()` is a no-op stub ((void)tone_hz; ...). So for the entire duration of an FTX-1
    TX, radio_control_begin_tx()/radio_control_end_tx() correctly key/unkey the radio via CAT (which is
    why the radio visibly transmits and the log shows a clean 79-tones-sent cycle), but literally no
    PCM audio is ever generated or written to the FTX-1's UAC-OUT endpoint — uac_tx_begin_cpfsk() /
    uac_tx_start_writer() / the speaker writer task's dds_render_16bit_stereo() call are never invoked.
    This is not a mute/volume/gain issue; it's a complete absence of the audio-generation call for this
    radio type. FTX-1 needs the same "sample-counted UAC OUT" treatment as QDX (both are USB-audio-fed
    transceivers, unlike QMX/KH1 which use CAT frequency-shift keying with no audio involved).
  fix: |
    main/main.cpp tx_start(): changed the QDX-only condition to
    `canonical_radio_type(g_radio) == RadioType::QDX || canonical_radio_type(g_radio) == RadioType::FTX1`
    so FTX-1 now also drives uac_tx_begin_cpfsk() -> uac_tx_start_writer() -> the speaker writer task,
    exactly mirroring QDX's existing, working sample-counted audio path. tx_tick()'s per-tone
    tx_send_ta() call is untouched (harmless no-op for FTX-1 via the stub, same as it already is
    effectively redundant/harmless for QDX which also calls it in parallel with its audio path).
  build: idf.py build succeeded cleanly.
  status: not yet verified on hardware.

## Fix Applied (round 1 — mute/volume diagnostic, kept as defense-in-depth)

- file: main/stream_uac.cpp
  function: uac_tx_start_writer()
  change: |
    After uac_host_device_resume(s_spk_handle) succeeds, added an `if (s_profile ==
    UAC_PROFILE_FTX1)` block that reads back mute/volume state via uac_host_device_get_mute() /
    uac_host_device_get_volume(), logs both, then unmutes (set_mute(false)) if reported muted and
    raises volume to 100 (set_volume(100)) if reported below 100. Fully gated to the FTX-1 profile;
    QMX/QDX/KH1 code paths are untouched.
  build: idf.py build succeeded cleanly (only stream_uac.cpp.obj recompiled, no warnings introduced).
  status: harmless, kept in tree — will now actually execute once round 2's fix routes FTX-1 through
    uac_tx_start_writer(), giving genuine defense-in-depth against a real muted-by-default codec on
    top of the round 2 fix.

## Fix Applied (round 2 — the actual root cause fix)

- file: main/main.cpp
  function: tx_start()
  change: |
    Extended the QDX-only USB-audio-TX condition to also cover RadioType::FTX1 (see Root Cause above
    for full detail). Updated the adjacent comment and the "QDX UAC OUT start failed" warning message
    (now generic "UAC OUT start failed") to reflect that this path now serves two radio types.
  status: not yet verified on hardware — next_action below.

## Current Focus

hypothesis: FTX-1 TX audio was never routed to the USB-audio path at all (tx_start() gated the
  sample-counted UAC OUT call to QDX only); fixed by adding FTX-1 to that condition.
test: Flash round 2's fix, run a beacon TX cycle, check for: (a) "UAC OUT CPFSK start base=... count=79
  ..." log line (proof uac_tx_begin_cpfsk() now runs), (b) the round-1 "FTX-1 spk state: get_mute=...
  get_volume=..." diagnostic line (proof uac_tx_start_writer() is reached), (c) actual non-zero
  deflection on the FTX-1's own meter during TX.
expecting: All three of the above. If the meter is still silent despite (a) and (b) confirming the
  audio pipeline is now actually running, the terminal/routing hypothesis (wrong USB Audio terminal
  wired to a non-TX path) becomes primary and needs descriptor-level investigation.
next_action: |
  HARDWARE VERIFICATION REQUIRED (checkpoint):
  1. Detach FTX-1 from Cardputer (COM12 must be free for flashing).
  2. Build + flash: `idf.py build` then `idf.py -p COM12 flash`.
  3. Reattach FTX-1, start/confirm COM5 debug UART capture is running.
  4. Re-enable beacon (STATUS screen) and exit to RX to trigger a beacon TX cycle.
  5. Watch the FTX-1's own front-panel/ALC meter during transmission.
  6. Report: (a) meter behavior, (b) presence/absence of "UAC OUT CPFSK start" and "FTX-1 spk state"
     log lines on COM5.
