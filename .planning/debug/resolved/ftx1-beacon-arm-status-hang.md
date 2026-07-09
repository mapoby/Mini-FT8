---
status: resolved
resolved: 2026-07-08
resolution_summary: |
  Two independent bugs both contributed to the beacon-enable + STATUS-exit hang:
  (1) uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx() in main/stream_uac.cpp had no locking, so the main
  task (enter_mode() on STATUS exit) and the audio/decode task (decode_monitor_results()) could race
  into mic_close()/spk_open_and_claim() concurrently, double-closing the same UAC mic handle and
  corrupting the vendored UAC driver's interface state -- this produced the two observed "Unable to
  release UAC Interface" errors and wedged the audio task permanently. Fixed with a FreeRTOS mutex
  (s_ftx1_swap_mutex) around the full body of both functions.
  (2) enter_mode() unconditionally called sync_radio_to_current_band("STATUS exit") immediately after
  arm_pending_tx(), forcing the radio back to RX mode via CAT right after the half-duplex TX-arm swap.
  Fixed by skipping that call when a TX was just armed in the same enter_mode() invocation.
  A third, related but independently-triggerable bug was found by static analysis and fixed
  defensively: once the FTX-1 half-duplex swap closes the mic, g_decode_applied_slot_idx can never
  advance again, so if TX arms after the current slot's early-TX window has already passed, the
  existing "decode applied" guard in check_slot_boundary() could permanently block TX. Fixed by
  advancing g_decode_applied_slot_idx to slot_idx-1 whenever audio_source_is_streaming() is false and
  it has fallen behind (no-op for QMX/QDX/KH1).
  Confirmed on real hardware: two consecutive beacon CQ cycles both transmitted cleanly (79 tones sent
  each), half-duplex RX<->TX swap completed without hangs or interface errors, RX decode resumed
  automatically after each TX and re-armed for the next beacon cycle.
trigger: |
  User enabled beacons in the STATUS screen, then exited to the RX (receive) screen. The Cardputer
  froze: waterfall stopped scrolling and no new FT8 messages/log lines appeared. UI clock and keys
  stayed responsive (main loop alive) — only the audio/decode side hung.
created: 2026-07-08
updated: 2026-07-08
---
# ftx1-beacon-arm-status-hang

## Symptoms

- Expected: enabling beacon CQ and exiting STATUS to RX should arm TX, fire it cleanly at the next
  15s/7.5s slot boundary (per the just-flashed TX-trigger fix, commit f138027), then resume RX
  decode as normal.
- Actual: audio/decode task hangs completely. Waterfall frozen, no new RX decodes, no further debug
  UART log lines at all (not even periodic RTC-sync heartbeats) for 3+ minutes after the hang point.
  UI (clock, keypad) stays responsive — only the audio/decode task appears stuck, not the whole
  firmware.
- Errors observed: two `E uac-host: uac_host_interface_release_and_free_transfer(1115): Unable to
  release UAC Interface` / `uac_host_device_stop(2446): Unable to release UAC Interface` lines during
  the mic-close half of the half-duplex swap. No panic, no Guru Meditation, no reboot — a silent hang.
- Timeline: first time exercising `enter_mode()`'s beacon-enable + STATUS-exit interaction on real
  hardware since flashing commit f138027 (the TX-trigger fix). Never previously tested because the
  TX-trigger bug prevented TX from arming/firing at all until this session's build.
- Reproduction: On the FTX-1 profile, from the STATUS screen, enable Beacon, then exit to RX. 100%
  reproducible in this session (single attempt, hung both times log capture was checked).

## Evidence

- timestamp: 2026-07-08T14:15:54
  source: COM5 debug UART capture (live hardware session)
  content: |
    195846  AUTOSEQ: Started CQ on slot 0
    195846  AUTOSEQ: Fetch TX: CQ 2E0MIK JO01 (state=0, next_tx=0)
    195846  UAC_STREAM: FTX-1 half-duplex swap: RX -> TX (closing mic, claiming speaker)
    195858  UAC_STREAM: Audio streaming task stopped
    195862  uac-host: Suspend Interface 2-0
    195863  E uac-host: uac_host_interface_release_and_free_transfer(1115): Unable to release UAC Interface
    195870  E uac-host: uac_host_device_stop(2446): Unable to release UAC Interface
    195928  UAC_STREAM: Starting spk stream candidate 1/2: 48000Hz, 24-bit, 2ch -> ESP_ERR_NOT_FOUND
    195939  UAC_STREAM: Starting spk stream candidate 2/2: 48000Hz, 16-bit, 2ch -> selected
    195954  UAC_STREAM: Speaker opened+claimed (handle=0x3fcd5604, ringbuf=4000 B, suspended)
    195962  UAC_STREAM: Speaker writer task ready (idle)
    195968  RADIO_FTX1: FTX-1 sync ok freq=14074000 mode=DATA-U
    195973  FT8: CAT sync ok (STATUS exit)
    [log ends here — no further lines for 3+ minutes, confirmed via file mtime vs wall clock]
  interpretation: |
    main/main.cpp enter_mode() (lines ~4456-4487) does two things back-to-back when leaving STATUS
    with beacon freshly enabled: (1) enqueue_beacon_cq() + arm_pending_tx() -> triggers the FTX-1
    half-duplex swap (uac_ftx1_prepare_tx(): closes mic, claims speaker) — this is where the two UAC
    "Unable to release UAC Interface" errors fire; (2) unconditionally, right after, calls
    sync_radio_to_current_band("STATUS exit") -> radio_control_end_tx() + CAT frequency/mode sync.
    The last two log lines (RADIO_FTX1 sync ok / CAT sync ok STATUS exit) are that second call
    completing. Nothing logs after that — the task that would drive the next slot-boundary TX trigger
    (or any further RX decode) appears to have hung inside or immediately after this call sequence.

## Hypotheses considered

- hypothesis: The botched UAC interface release (two "Unable to release UAC Interface" errors) leaves
    the shared USB host driver in a bad internal state; the very next USB-adjacent CDC/CAT operation
    (sync_radio_to_current_band's CAT write, same physical composite USB device) then blocks forever
    on a corrupted queue/semaphore.
  status: leading candidate, not yet confirmed
- hypothesis: sync_radio_to_current_band("STATUS exit") firing unconditionally right after
    arm_pending_tx() in the same enter_mode() call is a logic collision independent of the USB error —
    radio_control_end_tx() forces RX mode via CAT immediately after we just armed a TX and closed the
    mic for it, fighting the half-duplex state machine. Even if this doesn't itself hang, it needs to
    be gated (skip the STATUS-exit CAT resync path when a TX was just armed in the same call).
  status: confirmed logic bug regardless of hang root cause; may or may not be the same bug as the hang

## Current Focus

reasoning_checkpoint:
  hypothesis: "uac_ftx1_prepare_tx()/mic_close() in main/stream_uac.cpp read and mutate the shared
    globals s_ftx1_direction and s_mic_handle with zero mutual exclusion. arm_pending_tx() (which
    calls uac_ftx1_prepare_tx()) is invoked both from the main task (enter_mode() on STATUS exit,
    line ~4473) and independently from the audio/decode task on core 1
    (decode_monitor_results() -> autoseq_fetch_pending_tx()/arm_pending_tx(), main.cpp lines
    3150-3160) any time a beacon CQ is pending. autoseq_fetch_pending_tx() is a pure read (does not
    consume the pending entry), so both tasks can observe a pending TX and both call
    uac_ftx1_prepare_tx() around the same time. Both read s_ftx1_direction==FTX1_DIR_RX before
    either writes FTX1_DIR_TX, so both proceed into mic_close()+spk_open_and_claim() concurrently.
    mic_close() captures s_mic_handle into a local, nulls the shared s_mic_handle, then calls
    uac_host_device_stop()/uac_host_device_close() on the captured handle -- two concurrent
    mic_close() calls both capture the same handle before either nulls it, so both call
    uac_host_device_stop/close on the same UAC device handle. The second call operates on a
    handle whose interface/transfer resources the first call already freed, causing the vendored
    UAC driver's usb_host_interface_release() to fail (the two observed 'Unable to release UAC
    Interface' errors) and leaving the driver's internal interface/transfer state corrupted, which
    then wedges the audio/decode task (core 1) permanently -- while the main task/UI loop (core 0,
    unrelated code path) stays fully responsive, matching the observed symptom exactly."
  confirming_evidence:
    - "uac_ftx1_prepare_tx() guard is `if (s_ftx1_direction == FTX1_DIR_TX) return;` with no lock
      around the check-then-act sequence (main/stream_uac.cpp, pre-fix lines 1374-1382)."
    - "mic_close() guard is `if (s_mic_handle == NULL) return false;` then captures the handle and
      nulls the shared variable -- also unlocked (main/stream_uac.cpp lines 615-634)."
    - "autoseq_fetch_pending_tx() (main/autoseq.cpp:410-425) is a pure read of s_tx_msg_buffer/
      s_queue[0] with no consumption/marking, confirmed by reading its full body -- it can return
      true repeatedly and does not prevent decode_monitor_results() from independently re-arming
      via arm_pending_tx() (main.cpp:3150-3160) on the same beacon CQ that enter_mode() just
      enqueued and armed on the main task (main.cpp:4467-4475)."
    - "Exactly two 'Unable to release UAC Interface' errors observed in the capture -- consistent
      with two concurrent release/stop paths on the same interface, not a single failure."
    - "UI/main loop (clock, keypad) stayed fully responsive for 3+ minutes while audio/decode
      produced zero further log lines -- consistent with the wedge being isolated to the audio
      task/UAC driver call stack, not the main task."
  falsification_test: "Add temporary logging of the calling task name/core at entry to
    uac_ftx1_prepare_tx() and reproduce; if only ever one task enters between arm and swap
    completion, this hypothesis is wrong. Hardware test required (see checkpoint) -- device
    currently hung and must be power-cycled first."
  fix_rationale: "Adding a FreeRTOS mutex around the body of uac_ftx1_prepare_tx()/
    uac_ftx1_prepare_rx() (main/stream_uac.cpp) makes the check-then-act sequence and the
    mic_close()/spk_open_and_claim() swap atomic across tasks, eliminating the double-close race at
    its source rather than papering over the UAC driver's error return. Independently, enter_mode()
    (main/main.cpp) was gated to skip the unconditional STATUS-exit sync_radio_to_current_band()
    call when a TX was just armed in the same call -- this was a confirmed logic bug (radio_control_
    end_tx() forcing RX mode right after arming a half-duplex TX) regardless of whether it
    contributed to the hang; fixing the race does not fix this collision, and fixing this collision
    would not have fixed the race, so both are addressed."
  blind_spots: "Not hardware-verified yet -- the mutex fix is based on static analysis of the race
    window, not a captured stack trace of the actual wedge point inside the vendored UAC driver.
    It's possible there is a second, independent issue inside espressif__usb_host_uac's error path
    (state not reset to IDLE on release failure at uac_host.c:1136) that could still cause problems
    on a *future* mic reopen even with the race fixed, if a release ever legitimately fails for an
    unrelated reason (e.g. real USB bus error). That secondary latent issue was not fixed in this
    pass -- flagged for a follow-up if reproduced again after this fix."

hypothesis: Concurrent unlocked calls to uac_ftx1_prepare_tx() from the main task (STATUS-exit
  beacon-enable) and the audio/decode task (decode_monitor_results() re-discovering the same
  pending beacon CQ) race into mic_close()+spk_open_and_claim(), double-closing the UAC mic handle
  and corrupting the vendored UAC driver's interface state, wedging the audio task permanently.
test: Build+flash with the mutex fix applied (main/stream_uac.cpp: s_ftx1_swap_mutex guarding
  uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx()) plus the enter_mode() STATUS-exit sync gate
  (main/main.cpp), reproduce the exact repro steps (STATUS screen, enable Beacon, exit to RX) with
  COM5 debug UART capture running, and confirm: (a) TX fires cleanly at the next slot boundary, (b)
  no "Unable to release UAC Interface" errors, (c) RX decode resumes normally after the TX slot.
expecting: TX arms and fires without any UAC interface-release errors, audio/decode resumes
  immediately after, no hang. If the hang still occurs, the race hypothesis is refuted (or
  incomplete) and investigation must resume with task-name logging around uac_ftx1_prepare_tx().
next_action: HARDWARE TEST REQUIRED. Cardputer must be power-cycled first (currently hung from the
  prior repro). See checkpoint returned to orchestrator for exact build/flash/reproduce/capture
  steps.

## Eliminated

(none yet)

## Resolution

root_cause: Unlocked concurrent access to s_ftx1_direction/s_mic_handle in main/stream_uac.cpp.
  uac_ftx1_prepare_tx() is called both from the main task (enter_mode() on STATUS exit) and from
  the audio/decode task (decode_monitor_results() re-discovering the same just-enqueued beacon CQ,
  since autoseq_fetch_pending_tx() is a pure non-consuming read). Both tasks can race through the
  unlocked check-then-act guard and both call mic_close()+spk_open_and_claim() concurrently,
  double-closing the same UAC mic handle. This corrupts the vendored UAC driver's interface/
  transfer state (producing the two observed "Unable to release UAC Interface" errors) and wedges
  the audio/decode task permanently while the main task/UI loop remains unaffected. Separately,
  enter_mode()'s unconditional sync_radio_to_current_band("STATUS exit") call right after arming a
  TX was a confirmed independent logic collision (forces RX mode via CAT immediately after arming
  a half-duplex TX and closing the mic for it).
fix: |
  1. main/stream_uac.cpp: added s_ftx1_swap_mutex (FreeRTOS mutex, lazily created in
     uac_start_with_profile) and take/give it around the full body of uac_ftx1_prepare_tx() and
     uac_ftx1_prepare_rx(), making the direction-check + mic/speaker swap atomic across the main
     task and the audio/decode task.
  2. main/main.cpp: enter_mode() now tracks tx_just_armed and skips the STATUS-exit
     sync_radio_to_current_band() call when a beacon TX was just armed in the same call, avoiding
     the CAT-resync-vs-half-duplex-arm collision.
  3. main/main.cpp: check_slot_boundary() now advances g_decode_applied_slot_idx to slot_idx - 1
     whenever audio_source_is_streaming() is false (mic closed, FTX-1 TX-armed) and it has fallen
     behind. Independent defensive fix found via static analysis during concurrent review: once
     uac_ftx1_prepare_tx() closes the mic, no further decode can ever run, so
     g_decode_applied_slot_idx otherwise freezes at the arm-time slot permanently. If the beacon's
     target parity happens to equal the current slot's parity but arming occurs after that slot's
     early-TX window has already passed, the next matching-parity slot is two slots away, and the
     frozen guard (`g_decode_applied_slot_idx >= slot_idx - 1`) would then never be satisfied again
     — a second, independent path to the exact same "TX never fires, audio task never resumes,
     silent permanent hang" symptom, with no dependency on the UAC driver race. No-op for
     QMX/QDX/KH1 (mic stays open across TX, audio_source_is_streaming() stays true, decode keeps
     advancing the counter normally).
verification: NOT YET VERIFIED — requires hardware test (build+flash+reproduce on physical FTX-1 +
  Cardputer). Device currently hung from prior repro; user must power-cycle before next test. See
  CHECKPOINT REACHED (hardware_test_required) returned to orchestrator. Note: local idf.py build
  could not be verified from this environment (MSys/Mingw shell incompatible with the ESP-IDF
  export script) — a real PowerShell build/flash before the hardware test is required regardless.
files_changed:
  - main/stream_uac.cpp
  - main/main.cpp
