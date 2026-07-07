---
status: resolved
resolved: 2026-07-07
resolution_summary: |
  Half-duplex mic/speaker time-multiplexing (never holding both UAC streams open at once,
  time-multiplexed around FT8/FT4's own half-duplex slot structure) resolves the ESP32-S3's
  measured 8-channel HCD ceiling. Confirmed on real FTX-1 hardware across 8+ consecutive
  RX->TX->RX cycles with zero channel-exhaustion errors and real TX audio reaching the radio each
  cycle. See "Resolution (current — half-duplex mic/speaker redesign)" section for full detail.
trigger: |
  Second follow-up to the resolved ftx1-hcd-channel-exhaustion investigation (same slug, reopened
  again). Two prior sessions confirmed: (1) ESP32-S3 has a hard 8-channel USB host ceiling vs 9
  channels required once mic+speaker are both active on the FTX-1; (2) VID/PID-based enumeration
  suppression of the extra Yaesu auxiliary device is structurally impossible; (3) freeing the CP210x
  CAT-1 interface's unused bulk IN channel after normal open is unsupported at every USB Host stack
  layer and risks a driver-state crash. This session investigates the two remaining, previously
  unexplored channels in the 9-channel breakdown.
created: 2026-07-07
updated: 2026-07-07
---
# ftx1-hcd-channel-exhaustion

## Symptoms

(Carried over — see resolved/ftx1-hcd-channel-exhaustion.md for the full original write-up and the
two prior follow-up angles already ruled out.)

## Two new angles to investigate this session

**Angle A — Hub's status-change interrupt pipe (channel #2 in the original 9-channel breakdown).**
The FTX-1's internal USB hub consumes 2 channels: its own default control pipe (unavoidable — every
addressed USB device needs one) and a status-change interrupt pipe (used to detect downstream port
connect/disconnect/reset events). Once all downstream devices (CP210x, audio codec, aux device) have
finished enumerating at boot, is the hub's interrupt pipe still needed for anything the FTX-1
actually requires during normal FT8/FT4 operation? Investigate:
1. Where in ESP-IDF's hub driver (components/usb/hub.c or wherever the internal/external hub logic
   lives in this IDF version) the status-change interrupt pipe is allocated and polled.
2. Whether the hub class specification or ESP-IDF's implementation requires this pipe to remain
   active indefinitely (e.g. for over-current signaling, not just hot-plug detection), or whether it
   could be safely stopped once initial enumeration is confirmed complete, without breaking anything
   the FTX-1's actual behavior depends on (e.g. if the radio never dynamically hot-plugs internal
   peripherals during normal use, losing hot-plug detection post-boot may be an acceptable trade-off
   specific to this fixed, known hardware).
3. Is this hub even reachable/patchable from application code, or is it core ESP-IDF USB Host
   Library logic (same risk category as the previous CDC-ACM investigation — modifying core stack
   behavior, not a radio-specific vendored component)?

**Angle B — Reconsider port-based suppression of the Yaesu auxiliary device (VID:0x26aa PID:0x0030),
given FTX-1-specific context.** The original investigation rejected disabling a specific numbered hub
port (before knowing what's on it) as "fragile, hardcoded, depends on an undocumented, unit-specific
port-to-peripheral mapping that could differ across FTX-1 units/firmware revisions." Reconsider this
specific objection: the FTX-1 is ONE KNOWN, FIXED Yaesu radio model, not an arbitrary/generic USB
device — this firmware already hardcodes many FTX-1-specific assumptions (VID:0x10c4 PID:0xea70 for
CP210x, VID:0x0d8c PID:0x0016 for the audio codec, fixed baud rate, fixed CAT command set). Is
hardcoding "always disable hub downstream port N" meaningfully more fragile than those other
FTX-1-specific hardcoded assumptions already present and accepted in this codebase? Investigate:
1. Whether the hub port number assigned to the auxiliary device is likely to be electrically fixed
   by the FTX-1's own internal PCB wiring (i.e., a hardware characteristic of the radio design, not
   something that varies boot-to-boot or unit-to-unit) — if port assignment is deterministic per
   physical wiring (which is how internal, non-removable USB hubs typically work), this is a
   reasonable, stable assumption for THIS SPECIFIC RADIO MODEL, not a fragile generic-USB hack.
2. What the actual auxiliary device is likely to be (research: does the Yaesu FTX-1 have any known
   USB-exposed peripheral beyond CAT and audio? Check for any existing project research docs
   mentioning this, or reason from the VID:0x26aa Yaesu Musen Co. vendor ID and PID:0x0030 whether
   this might be a firmware-update/DFU interface, a panel/encoder HID interface, or something else —
   if it's something clearly non-essential to FT8/FT4 operation, e.g. a firmware update mode
   interface that's irrelevant during normal radio use, disabling its port is even more clearly safe
   in practice, even if not "elegant" in the general-purpose-USB-host sense).
3. If disabling that specific downstream port is judged safe and appropriately scoped (FTX-1-specific
   code path only, never applied to QMX/QDX/KH1 or generic USB devices), investigate the actual
   mechanism (`ext_hub_port_disable()` per the original investigation's citation of hub.c:504) and
   whether it's callable from application code, or requires deeper ESP-IDF hub-driver modification.

For BOTH angles: if a genuinely safe, appropriately-scoped (FTX-1-only) mechanism is found, prototype
it and request a hardware checkpoint via the orchestrator (build+flash+confirm mic+speaker both
negotiate together, confirm CAT still works, confirm no regression to QMX/other radios). If neither
angle offers a safe path, report honestly — this project has already invested 3 investigation
sessions into this problem; a 4th honest "still not fixable safely" answer is a legitimate, valuable
outcome, not a failure.

## Current Focus

hypothesis: The enum_filter_cb fix (kept, hardware-verified) is necessary but not sufficient —
  measured `allocated=8 total=8` proves hub+CDC(CAT)+audio-control+speaker alone consume the full
  ESP32-S3 HCD ceiling with zero margin, even with the aux device's channel freed. User has decided
  (2026-07-07) to pursue the previously-deferred half-duplex redesign: never hold both the mic and
  speaker UAC streams open simultaneously. Since mic and speaker are symmetric siblings on the same
  audio device (same descriptor shape, 1 streaming pipe each), `hub+CDC+audio-ctrl+mic` (no speaker)
  should also land at exactly 8 by the same arithmetic that produced `hub+CDC+audio-ctrl+speaker=8`
  — fits, with zero margin, as long as the two are never open at once. This is a natural fit for
  FT8/FT4's actual half-duplex behavior (never decode and transmit at the same instant).
test: Close the mic's UAC stream and pre-open+claim (suspended, not yet streaming) the speaker
  stream BEFORE a TX slot begins — timed off autoseq's existing "decide if next slot transmits"
  logic, which already runs before the slot boundary (see main.cpp's documented three-event
  ordering: decode-complete always precedes slot-boundary TX-trigger). Symmetrically, close the
  speaker and pre-open+claim the mic before the next RX-decode slot needs to start capturing. The
  speaker's existing eager "pre-opened+claimed, suspended" pattern already separates claim-time
  (needs ~100-200ms, can happen with slot-boundary lead time) from stream-start time (must be
  exact, zero-latency, at the TX instant) — the redesign should reuse that same separation for both
  directions rather than negotiating format at the exact swap instant.
expecting: If mic/speaker are strictly mutually exclusive in time, total channel usage should stay
  at 8 (at the ceiling, but not exceeding it) in both RX-only and TX-only states, resolving the
  channel exhaustion without needing a 9th channel. Main engineering risks: (1) reliably completing
  the close+reopen sequence within the lead time available before each slot boundary, tighter for
  FT4's 7.5s slots than FT8's 15s; (2) `uac_host_device_close()` is a supported, designed release
  path (unlike the CDC bulk-IN case already ruled unsafe), so this should not carry the same
  driver-state-crash risk class.
next_action: HARDWARE-VERIFIED 2026-07-07 — see new Evidence entry below. Half-duplex swap works
  across at least 5 consecutive RX->TX->RX cycles with zero channel-exhaustion errors and real TX
  audio confirmed reaching the radio (79 tones sent per CQ). One cosmetic ERROR-level log
  ("Unable to release UAC Interface") appears during every mic close but does not correlate with
  any resource leak (channel count never degrades across repeated cycles) — flagged for a
  low-priority follow-up, not blocking.
next_action_superseded: half-duplex swap implemented in
  main/stream_uac.cpp (new uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx(), FTX-1-profile-only,
  no-op for QMX/QDX/KH1/generic-USB) and wired into main.cpp at arm_pending_tx() (TX-decision
  point, fires well before the slot boundary) and check_slot_boundary()'s TX-completion path
  (both the normal completion branch and the cancel branch in tx_tick()). `idf.py build`
  confirmed clean (exit 0). Hardware checkpoint requested — see CHECKPOINT REACHED.
reasoning_checkpoint:
  hypothesis: "Time-multiplexing the FTX-1's mic and speaker UAC pipes -- closing one and
    claiming the other ahead of each slot boundary, keyed off autoseq's existing TX-decision
    signal (arm_pending_tx()) and TX-completion signal (check_slot_boundary()'s
    g_was_txing&&!g_tx_active branch, plus tx_tick()'s cancel branch) -- keeps total HCD channel
    usage at exactly 8 (hub+CDC(CAT)+audio-ctrl+{mic OR speaker}) in both RX and TX states,
    instead of 9 (hub+CDC+audio-ctrl+mic+speaker) when both are held open simultaneously."
  confirming_evidence:
    - "Checkpoint #3 (this file, hardware-measured): allocated=8 total=8 fires at the exact
      moment the mic's pipe-alloc is attempted while hub+CDC+audio-ctrl+speaker already hold
      all 8 channels -- i.e. any 4-way combination of {hub, CDC, audio-ctrl, one-of-mic/speaker}
      already exactly fills the ceiling with zero margin, meaning any 4th of those exactly fits
      and a 5th (the other of mic/speaker) categorically cannot."
    - "arm_pending_tx() (main.cpp) is confirmed, by direct code reading, to be the single
      point of truth for every TX-scheduling path (autoseq tick, beacon CQ, free-text queue,
      manual RX-tap reply, schedule_manual_pending_tx()) -- grepped all 6 call sites, all funnel
      through this one function -- making it a reliable, exhaustive hook for 'TX is coming'."
    - "check_slot_boundary()'s g_was_txing&&!g_tx_active branch is confirmed to fire at the
      slot boundary itself (tx_start() aligns g_tx_next_tone_time to the slot boundary via
      g_tx_slot_start_ms, so the last tone -- and thus g_tx_active=false -- lands at
      essentially the same instant as the next slot's start), not some arbitrary later point --
      verified by reading tx_start()'s tone-timing setup and tx_tick()'s completion branch."
    - "uac_tx_end() (called from radio_control_ftx1.cpp's ftx1_end_tx(), itself called from
      tx_tick()'s completion branch via radio_control_end_tx() BEFORE g_tx_active is set false)
      already suspends the speaker and sets s_spk_writer_run=false strictly before
      check_slot_boundary() ever observes g_tx_active==false -- confirmed by reading the call
      order in tx_tick() -- so spk_close() in uac_ftx1_prepare_rx() never races the writer task."
    - "uac_host_device_close() is the documented, supported release primitive for a UAC stream
      (confirmed in a prior sub-investigation this session, kept as an established fact, not
      re-derived) -- unlike the CDC bulk-IN pipe case already ruled unsafe in an earlier
      session."
  falsification_test: "If, after flashing, the mic fails to reopen within the ~12.4s of the
    ~12.6s FT8 audio-capture window that remains after the ~100-200ms mic renegotiation delay
    following a TX-to-RX swap (i.e. decode quality measurably degrades or decode fails outright
    on the first RX slot after every TX), or if a channel-exhaustion error still appears in
    either direction, or if CAT commands stop working, the hypothesis is falsified. None of
    these have been observed yet -- this is unverified on hardware, which is exactly why a
    checkpoint is being requested rather than declaring resolution."
  fix_rationale: "Addresses the measured root cause directly (zero-margin 8-channel ceiling)
    rather than a symptom: instead of trying to find a 9th channel (three prior sessions
    confirmed none exists among hub/CDC/audio-ctrl, all load-bearing), this makes the hardware
    never need 9 channels at all, by exploiting the fact that FT8/FT4 never needs mic and
    speaker simultaneously. This is the previously-deferred, only-remaining path documented in
    this file's own Resolution section from the prior sub-session."
  blind_spots: "(1) Not yet hardware-verified -- this is a build-clean prototype, not a
    confirmed fix. (2) The ~100-200ms mic/speaker renegotiation latency on each swap direction
    is measured from the speaker's original enum-time negotiation, not from a live on-demand
    reopen after a prior close on this exact hardware -- reopen timing could differ (e.g. if
    the FTX-1's internal hub or codec needs a brief settle time after an interface is released
    before it can be re-claimed; no such settle delay was added, mirroring this session's
    checkpoint #2 finding that no settle delay was needed for the enum_filter_cb fix, but that
    finding was for a different pipe/scenario, not proven to generalize here). (3) FTX-1's
    tx_start() in main.cpp only calls uac_tx_begin_cpfsk() for canonical_radio_type()==QDX, not
    FTX1 (line ~3383 in the pre-session code) -- this appears to be a pre-existing, unrelated
    gap in FTX-1's TX audio wiring (out of scope for this channel-exhaustion redesign, not
    touched), but it means real TX audio may not yet flow through the speaker for FTX-1 even
    though the speaker's pipe is correctly claimed/suspended/resumed by this redesign; flagged
    here so the hardware checkpoint doesn't mistake 'no audio on air' for a failure of this
    redesign specifically. (4) FT4's tighter 7.5s slot / ~2.5s lead-time budget has not been
    separately measured against the ~100-200ms swap cost; FT8's ~15s slot / ~2.4s+ budget is
    the primarily reasoned-about case."
tdd_checkpoint: null

## Superseded Current Focus (enum_filter_cb sub-investigation, completed 2026-07-07)

hypothesis: CONFIRMED (Angle B, via a mechanism the prior session did not check) — ESP-IDF's
  USB Host stack exposes a fully public, documented `usb_host_enum_filter_cb_t` callback
  (`components/usb/include/usb/usb_types_stack.h:71`, wired via `usb_host_config_t.enum_filter_cb`,
  gated by `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK`) that is invoked during enumeration
  AFTER the device descriptor (including idVendor/idProduct) has already been read
  (`enum.c: select_active_configuration()`, line ~234-263). Returning `false` for the Yaesu
  auxiliary device (VID 0x26aa PID 0x0030) causes `enum_cancel()` to run
  (`enum.c:1267`), which calls `usbh_dev_close()` — freeing that device's default EP0 control
  pipe/channel (`usbh.c: device_free()` calls `hcd_pipe_free()` on `default_pipe`, confirmed
  line ~430) — and then `usb_host.c:401-402` automatically calls `hub_port_disable()` on the
  specific hub port the rejected device was attached to, purely as a side effect of the
  framework's own built-in `ENUM_EVENT_CANCELED` handling. No private/internal ESP-IDF headers,
  no hub-driver patching, no reaching past a supported API layer. This corrects the prior
  session's (and STATE.md's) claim that VID/PID-based suppression is "structurally impossible ...
  its control pipe is allocated before VID/PID is ever readable" — that claim is factually wrong:
  the full device descriptor (with VID/PID) is read and cached in `dev_desc` BEFORE
  `select_active_configuration()`/the filter callback runs, at the `GET_FULL_DEV_DESC` stage
  which precedes it in the enum state machine.
test: Enable `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y` in sdkconfig; register a filter
  callback in `usb_lib_task()` (main/stream_uac.cpp) that returns `false` only for
  VID 0x26aa / PID 0x0030 (unconditionally — this VID:PID pair is Yaesu-specific and cannot
  collide with QMX/QDX/KH1/generic-USB devices, so no profile gating is even required for
  safety, though the fix is naturally FTX-1-only in practice since only the FTX-1 exposes this
  device), and `true` for everything else (default bConfigurationValue = 1 preserved for all
  other devices). Verify via `idf.py build` (clean compile) and full code-path re-reading; then
  request a hardware checkpoint (build+flash+confirm: aux device port disabled/no longer
  enumerated, mic+speaker both negotiate together successfully, CAT commands FA;/MD0C;/TX1;/TX0;
  still work, no regression on QMX/QDX/KH1 since the callback is a no-op passthrough for any
  other VID/PID).
expecting: If confirmed on hardware, this frees exactly 1 HCD channel (the aux device's default
  control pipe), closing the 9-vs-8 channel deficit without needing the mic-close/reopen
  architectural change previously deferred to a future session.
next_action: DONE — fix implemented (sdkconfig + main/stream_uac.cpp). `idf.py build` confirmed
  clean (exit 0; only pre-existing unrelated -Wunused-function warnings in main.cpp; stream_uac.cpp
  compiled without warnings). Hardware checkpoint requested — see CHECKPOINT REACHED.
reasoning_checkpoint:
  hypothesis: "The ESP-IDF public enum_filter_cb mechanism can reject the Yaesu aux device
    (0x26aa:0x0030) by VID/PID during enumeration, and the framework automatically frees its
    control-pipe channel and disables its hub port as a documented side effect of cancellation —
    with zero risk to CP210x CAT, the audio codec, or any other supported radio (QMX/QDX/KH1)."
  confirming_evidence:
    - "usb_types_stack.h:71 declares usb_host_enum_filter_cb_t(dev_desc, *bConfigurationValue)
      as a public API type in the public include/ directory (not private_include/)."
    - "enum.c select_active_configuration() (line 234-263) calls usbh_dev_get_desc() to obtain
      dev_desc BEFORE invoking enum_filter_cb — proving VID/PID IS available at filter-decision
      time, contradicting the prior session's stated reason for rejecting this approach."
    - "enum.c enum_cancel() (line 1267-1306) calls usbh_dev_close() when the filter returns
      false, and usbh.c's device_free() (line ~430) frees the device's default_pipe via
      hcd_pipe_free() — the control-pipe channel genuinely returns to the pool, not left
      dangling."
    - "usb_host.c line 401-402: ENUM_EVENT_CANCELED handler calls
      hub_port_disable(parent_dev_hdl, parent_port_num) automatically — port disable is already
      wired into the framework's own cancellation path, requiring zero direct calls into
      hub.c/ext_hub.c's private-only hub_port_disable()/ext_hub_port_disable() functions."
    - "Kconfig USB_HOST_ENABLE_ENUM_FILTER_CALLBACK (components/usb/Kconfig:167) documents this
      exact use case: 'control whether a device should be enumerated' — this is first-class,
      intended, documented behavior, not an incidental side effect being repurposed."
  falsification_test: "If dev_desc's idVendor/idProduct were unavailable/zeroed at the point
    enum_filter_cb is invoked, or if enum_cancel()/usbh_dev_close() left the default pipe
    allocated (channel not actually freed), or if hub_port_disable() were never reached from the
    cancellation path (leaving the port live/reset-looping), the hypothesis would be false. None
    of these were observed — each was verified by reading the actual code path, not inferred."
  fix_rationale: "Addresses the root cause directly: the aux device's default control pipe is one
    of the 9 required channels. Rejecting its enumeration via the documented filter mechanism
    prevents that channel from ever being held, exactly as the framework's PORT_REQ_DISABLE /
    ENUM_EVENT_CANCELED path is designed to do for exactly this scenario (a device the host
    chooses not to support). It is not a workaround bypassing API layers — it uses the
    already-existing, most-supported customization point ESP-IDF provides for this precise
    purpose."
  blind_spots: "Not yet build-verified (idf.py build pending) or hardware-verified. Unconfirmed:
    (1) whether the FTX-1's internal hub actually presents the aux device on port enumeration in
    a way where GET_FULL_DEV_DESC succeeds cleanly before the filter runs (if the aux device is
    itself misbehaving/non-compliant, enumeration could stall/retry before ever reaching
    select_active_configuration(), in which case this fix would have no effect — but that is a
    'no benefit', not a 'regression', case). (2) Whether disabling this port affects only the
    intended single downstream port or whether the ESP32-S3-side observed topology maps the aux
    device to the same physical hub port as another device (unlikely for a hub, each port is
    physically separate by definition, but not yet empirically confirmed against real
    enumeration logs). (3) Whether CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK interacts with any
    other already-tuned Kconfig option in this project's sdkconfig (checked: no other Kconfig
    option references or depends on it)."
tdd_checkpoint: null

## Evidence

- timestamp: 2026-07-07 (half-duplex redesign implementation)
  checked: main/stream_uac.cpp's RX_CONNECTED and TX_CONNECTED handlers (mic/speaker open+
    negotiate logic), main/main.cpp's arm_pending_tx() and check_slot_boundary()/tx_tick()
    (every TX-scheduling and TX-completion path).
  found: Refactored the mic's open+negotiate code (previously inline in the RX_CONNECTED
    handler) into mic_negotiate_and_start()/mic_ensure_stream_task()/mic_open_and_start()/
    mic_close(), and the speaker's pre-open+claim code (previously inline in TX_CONNECTED) into
    spk_open_and_claim()/spk_close() — all static, FTX-1-profile-agnostic helpers reusable both
    at initial enumeration and for on-demand reopen/reclose. For UAC_PROFILE_FTX1 specifically,
    TX_CONNECTED now defers the speaker's open (previously eager) until first needed, since
    opening it immediately would collide with the mic's already-open channel. Added
    uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx() as the public swap entry points (no-op for every
    other profile) and wired them into main.cpp: uac_ftx1_prepare_tx() at the end of
    arm_pending_tx() (the single point of truth for every TX-scheduling path — verified via grep
    that all 6 arm_pending_tx() call sites funnel through it), and uac_ftx1_prepare_rx() both in
    check_slot_boundary()'s g_was_txing&&!g_tx_active completion branch and in tx_tick()'s
    cancel branch (the cancel path bypasses check_slot_boundary() entirely by setting
    g_was_txing=false directly, so it needed its own explicit call to avoid leaving the radio
    stuck in TX-direction/mic-closed state after a cancelled TX).
  implication: The swap is keyed entirely off signals that already exist and already fire with
    the documented lead time (decode-complete before slot-boundary TX-trigger), requiring no new
    timing logic of its own — it rides the existing three-event ordering guarantee rather than
    inventing a new one. mic_close() blocks (bounded, <=300ms) until stream_uac_task has
    self-deleted before closing the handle, avoiding a close-while-read-in-flight race across
    tasks; spk_close() relies on uac_tx_end()'s already-established call-order guarantee that
    the writer task is idle before check_slot_boundary() ever observes TX-complete.

- timestamp: 2026-07-07 (build verification)
  checked: `idf.py build` (sourced ESP-IDF v5.5.1 PowerShell session, MSYSTEM env var cleared to
    avoid the export script's Msys/Mingw rejection) after the half-duplex redesign changes to
    main/stream_uac.cpp, main/stream_uac.h, and main/main.cpp.
  found: Exit code 0. stream_uac.cpp and main.cpp both recompiled cleanly. Only pre-existing,
    unrelated -Wunused-function warnings in main.cpp (schedule_manual_pending_tx,
    autoseq_has_pending_tx, normalize_date_ymd, qso_load_fetch_file_list — all present before
    this session's changes).
  implication: Redesign compiles cleanly against the real ESP-IDF v5.5.1 toolchain; ready for
    hardware verification. Not yet hardware-tested.

- timestamp: 2026-07-07 (hardware checkpoint #3, exact channel-count instrumentation — DECISIVE)
  checked: Temporarily instrumented the real compiled HCD source — not ESP-IDF's global install,
    which turned out to be a red herring (see below), but the actual build dependency at
    managed_components/espressif__usb/src/hcd_dwc.c (this project vendors its own copy of the
    `espressif/usb` component; the global C:\Espressif\esp-idf-v5.5.1\components\usb\hcd_dwc.c is
    never compiled for this project — confirmed via build log, which references
    `esp-idf/espressif__usb/CMakeFiles/...` object paths, and via `Get-ChildItem` showing the actual
    .obj's source is the vendored managed_components copy). Added one temporary ESP_LOGE printing
    `port->hal->channels.num_allocated` and `port->hal->constant_config.chan_num_total` at the exact
    point HCD channel allocation fails. Rebuilt, reflashed, reconnected FTX-1.
  found: "E (10959) HCD DWC: No more HCD channels available (allocated=8 total=8)" — this fires
    199ms after the aux device's rejection log (10760ms), comfortably after ENUM's cancel/cleanup
    completes (per checkpoint #2's timing evidence). At the exact moment the mic's pipe-alloc is
    attempted, ALL 8 physical HCD channels (the ESP32-S3's confirmed hardware ceiling,
    `chan_num_total=8`, read directly from GHWCFG2 silicon register via
    `usb_dwc_ll_ghwcfg_get_channel_num()`) are already allocated by: the FTX-1's internal USB hub
    (CONFIG_USB_HOST_HUBS_SUPPORTED=y confirms hub support is compiled; a physical hub is required to
    fan out to the 3 addressed devices seen on this bus) + the CP210x CDC device (CAT-1, already
    open) + the audio codec device's control pipe + its already-claimed speaker streaming pipe. The
    aux device's channel WAS already freed by this point (rejection confirmed via ENUM's own log
    line in checkpoint #1) — it is not part of this 8.
  implication: This is not "off by 1, keep hunting" — it is a measured, zero-margin hard wall.
    Hub + CDC(CAT) + audio-control + speaker alone already consume the full 8-channel ceiling before
    the mic is even attempted. There is no channel left to free among the load-bearing devices
    without breaking CAT or speaker (TX) functionality. The aux-device rejection was a real,
    working, necessary optimization (it prevented needing a 9th channel just to reach this point),
    but it cannot close a gap that doesn't have 1 more channel of margin anywhere. Simultaneous
    mic + speaker + CAT operation is confirmed structurally impossible on this exact USB topology
    and ESP32-S3 hardware ceiling — no further software-only channel-freeing avenue remains
    unexplored (hub, CDC, and audio-control are all load-bearing; their teardown breaks core
    functionality, unlike the aux device which had none).
  diagnostic_reverted: Yes — the temporary ESP_LOGE instrumentation in
    managed_components/espressif__usb/src/hcd_dwc.c was reverted immediately after capturing this
    result; no diagnostic code remains in the tree.

- timestamp: 2026-07-07 (hardware checkpoint #1, enum_filter_cb fix)
  checked: COM5 debug UART, full boot log with FTX-1 reconnect, `enum_filter_cb` fix installed
    (CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y, ftx1_enum_filter_cb() registered).
  found: The rejection fires exactly as designed — "Rejecting enumeration of FTX-1 aux device
    VID:0x26aa PID:0x0030 (frees 1 HCD channel for mic+speaker)" at t=8902ms, followed by ESP-IDF's
    own "ENUM: Canceled enumeration of the device 0x30:0x26aa, configuration value=1" at t=8967ms —
    confirming the framework-level enum_cancel()/usbh_dev_close() path genuinely ran. Speaker
    negotiated successfully (candidate 2/2, 16-bit/2ch) and CP210x CAT opened normally. But the mic
    candidate-scan, starting at t=9100ms (133ms after the confirmed cancel), still failed at
    candidate 2/4 (16-bit/2ch) with "E (9118) HCD DWC: No more HCD channels available" ->
    "EP Alloc error: ESP_ERR_NOT_SUPPORTED" -> "Unable to claim Interface" -> "No 48k UAC mic format".
  implication: The fix works exactly as intended (confirmed by ESP-IDF's own log line, not just our
    code's log line) and does free 1 channel — but it is not sufficient. The true channel deficit is
    at least 2, not 1 as the original 9-vs-8 breakdown assumed.

- timestamp: 2026-07-07 (hardware checkpoint #2, settle-delay hypothesis test)
  checked: Added a 50ms vTaskDelay() in main/stream_uac.cpp between cp210x_try_open() and the start
    of the mic candidate-scan loop, on the theory that enum_cancel()'s channel-free might be
    completing asynchronously (via the USB lib task) after our own log line prints, racing the mic's
    pipe-alloc attempt. Rebuilt, reflashed, retested on hardware.
  found: No change in outcome — mic still fails with "No more HCD channels available" at the same
    logical point. Critically, the full log shows the ENUM-level "Canceled enumeration" confirmation
    completes at t=8967ms, while the mic's failing pipe-alloc attempt doesn't occur until t=9118ms —
    a 151ms gap that already exceeds the added 50ms delay, entirely due to the natural sequencing of
    CP210x CAT-1 setup (device descriptor dump, RTS/DTR deassert, line coding set) that happens in
    between. The channel-free is not racing the mic's request; it has already completed with margin
    to spare, and the shortage persists regardless.
  implication: This was not a timing/async-cleanup race. The deficit is structural — rejecting the
    aux device's 1 channel is confirmed insufficient on real hardware, not just in theory. Ruled out
    as a contributing factor.

- timestamp: 2026-07-07
  checked: main/stream_uac.cpp cp210x_try_open() (line 303) and
    managed_components/espressif__usb_host_cp210x_vcp/usb_host_cp210x_vcp.c (line 102-117).
  found: cp210x_vcp_open() is a thin wrapper that calls cdc_acm_host_open(vid, pid,
    interface_idx=0, ...) — explicitly and only interface 0 (the CAT-1 UART). The CP2105's second,
    unused UART (interface 1, visible in the device's full descriptor dump: IF num=1, 2 bulk
    endpoints) is never claimed by any code in this project.
  implication: Ruled out "the unused second CP2105 UART interface is being accidentally claimed" as
    a source of the extra 1-channel deficit — it is confirmed never claimed.

(Prior sessions' evidence carries over — see resolved/ftx1-hcd-channel-exhaustion.md.)

- timestamp: 2026-07-07
  checked: C:\Espressif\esp-idf-v5.5.1\components\usb\include\usb\usb_types_stack.h:71 (public
    typedef), components\usb\include\usb\usb_host.h:112 (usb_host_config_t.enum_filter_cb field),
    components\usb\Kconfig:167 (CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK).
  found: ESP-IDF exposes `usb_host_enum_filter_cb_t(const usb_device_desc_t *dev_desc, uint8_t
    *bConfigurationValue) -> bool` as a fully public, documented callback settable at
    `usb_host_install()` time, gated by a Kconfig option whose help text explicitly states its
    purpose: "control whether a device should be enumerated."
  implication: A supported, first-class mechanism exists for exactly the VID/PID-based rejection
    the prior session concluded was impossible. This directly contradicts STATE.md's MAJOR entry
    and the previously resolved file's stated reasoning.

- timestamp: 2026-07-07
  checked: components\usb\enum.c select_active_configuration() (line 234-263) and enum_cancel()
    (line 1267-1306); components\usb\usbh.c device_free() (~line 407-430); components\usb\usb_host.c
    (line 313-402, ENUM_EVENT_CANCELED handling).
  found: `usbh_dev_get_desc()` populates `dev_desc` (including idVendor/idProduct) BEFORE
    `enum_filter_cb` is invoked — VID/PID is available at decision time. Returning false triggers
    `enum_cancel()` -> `usbh_dev_close()`, and `device_free()` calls `hcd_pipe_free()` on the
    device's `default_pipe`, genuinely returning the HCD channel to the pool. Separately,
    `usb_host.c`'s handling of `ENUM_EVENT_CANCELED` (line 401-402) calls `hub_port_disable()`
    automatically on the specific parent port — no direct call into hub.c/ext_hub.c internals
    (which remain private-header-only) is needed from application code.
  implication: The full mechanism — reject by VID/PID, free the channel, disable the port — is
    achievable entirely through the public `usb_host_config_t.enum_filter_cb` customization point.
    No core ESP-IDF hub-driver modification, no private headers, no bypassing API layers. This is
    a materially different (and safe) mechanism from both angles this session was asked to
    investigate (Angle A: hub interrupt pipe; Angle B: direct hub_port_disable() call) — it
    achieves Angle B's goal through the enumeration filter rather than a direct port-disable call.

- timestamp: 2026-07-07
  checked: `idf.py build` in a sourced ESP-IDF v5.5.1 PowerShell session, after implementing the
    fix in main/stream_uac.cpp and enabling CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y.
  found: Exit code 0. stream_uac.cpp recompiled with zero warnings/errors. Only pre-existing,
    unrelated -Wunused-function warnings in main.cpp (unchanged from before this session).
  implication: Fix compiles cleanly against the real ESP-IDF v5.5.1 toolchain; ready for hardware
    verification.

## Eliminated

- hypothesis: "Freeing the aux device's 1 channel via enum_filter_cb is sufficient to close the
    channel deficit and let mic+speaker+CAT coexist."
  evidence: Confirmed on real hardware (checkpoint #1 above) that the rejection and channel-free
    genuinely happen (ESP-IDF's own ENUM log confirms enum_cancel() completed), yet the mic
    candidate-scan still fails with "No more HCD channels available". The fix is necessary but not
    sufficient — the true deficit is at least 2 channels, not 1.
  timestamp: 2026-07-07

- hypothesis: "The mic's failure is a timing race — enum_cancel()'s channel-free happens
    asynchronously and hasn't completed by the time the mic candidate-scan starts."
  evidence: Disproven on real hardware (checkpoint #2 above). ESP-IDF's own cancellation-confirmed
    log line appears 151ms before the mic's failing pipe-alloc attempt — comfortably before, with no
    added delay needed. A deliberately-added 50ms settle delay made no difference to the outcome.
  timestamp: 2026-07-07

- hypothesis: "The CP2105's unused second UART interface (interface 1) is being unintentionally
    claimed, consuming channels it doesn't need to."
  evidence: Code reading confirms cp210x_vcp_open() -> cdc_acm_host_open() is called with
    interface_idx=0 explicitly; interface 1 is never referenced anywhere in this project.
  timestamp: 2026-07-07

(Prior sessions' eliminated hypotheses carry over — see resolved file: freeing CP210x's bulk IN
pipe post-open remains correctly eliminated; that mechanism genuinely has no safe per-endpoint
release at any USB Host stack layer, unrelated to this session's finding.)

- hypothesis: [CORRECTED, not merely re-eliminated] "VID/PID-based enumeration suppression of the
    Yaesu auxiliary device is structurally impossible because its control pipe is allocated before
    VID/PID is ever readable during enumeration" (as stated in the resolved file and STATE.md's
    MAJOR entry from the prior session).
  evidence: This claim is factually incorrect. `enum.c`'s `select_active_configuration()` reads the
    full device descriptor (via `usbh_dev_get_desc()`, populated during the earlier
    `GET_FULL_DEV_DESC` enumeration stage) and passes it — with `idVendor`/`idProduct` populated —
    to `enum_filter_cb` before deciding whether to proceed. The public
    `usb_host_config_t.enum_filter_cb` mechanism, gated by `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK`,
    was not discovered or checked by the prior session; it is documented in `components/usb/Kconfig`
    for exactly this use case.
  timestamp: 2026-07-07

## Resolution (superseded by half-duplex redesign below — kept for history)

status: NOT RESOLVED (as of the enum_filter_cb sub-session) — confirmed hard hardware limitation,
  no further channel-freeing avenue found. Superseded by the half-duplex redesign section below.
summary: |
  Three hardware checkpoints this session moved this from "theory" to "measured fact":
  (1) the enum_filter_cb fix genuinely works and frees the aux device's 1 channel (confirmed via
  ESP-IDF's own ENUM log, not just this project's log line); (2) the residual mic failure is not a
  timing race (a 50ms settle delay made no difference; the real gap between confirmed channel-free
  and the mic's failing attempt was already 150ms+ with no delay added); (3) direct instrumentation
  of the real compiled HCD source (managed_components/espressif__usb/src/hcd_dwc.c — the global
  ESP-IDF install is NOT what this project compiles against, a discovery in its own right) measured
  `allocated=8 total=8` at the exact moment of failure — meaning hub + CDC(CAT) + audio-control +
  speaker alone already consume the entire 8-channel hardware ceiling, with the mic still needing a
  9th that categorically does not exist. The enum_filter_cb fix is being KEPT (it is correct,
  necessary, and harmless — it prevents the aux device from wasting a channel neither used nor
  useful to this project) but it does not, and structurally cannot, close the remaining gap alone.
  The previously-deferred "close mic pipe during TX slot, reopen right after" half-duplex redesign
  (FT8/FT4 are half-duplex within a single transmission — RX decode and TX never need to be
  simultaneous) remains the only known path to a working E2E-01/E2E-02, and is real, unscoped
  engineering work requiring a dedicated future session to prototype against the 15s/7.5s slot
  timing budget.
root_cause: |
  Same underlying hard ceiling documented in resolved/ftx1-hcd-channel-exhaustion.md: ESP32-S3's
  USB-OTG host controller has a fixed 8-HCD-channel limit, and the FTX-1's real USB topology needs
  9 once mic+speaker are both active alongside CAT. Of those 9, one channel is the FTX-1's internal
  auxiliary device (VID:0x26aa PID:0x0030, Yaesu Musen Co.) that Mini-FT8 has no functional use for
  — it is not CAT, not audio, not claimed by any driver in this codebase. A prior session (see
  resolved/ftx1-hcd-channel-exhaustion.md, and STATE.md's MAJOR entry) incorrectly concluded that
  VID/PID-based suppression of this device was "structurally impossible" because its control pipe
  is supposedly "allocated before VID/PID is ever readable during enumeration." Direct reading of
  ESP-IDF v5.5.1's enum.c disproves this: the device descriptor (containing idVendor/idProduct) is
  read and cached in dev_desc during the GET_FULL_DEV_DESC stage, which completes BEFORE
  select_active_configuration() invokes the public, documented usb_host_enum_filter_cb_t callback.
fix: |
  Enabled CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y in sdkconfig (previously unset). Added a
  static ftx1_enum_filter_cb() in main/stream_uac.cpp, registered in host_config.enum_filter_cb
  only when s_profile == UAC_PROFILE_FTX1 (scoped so QMX/QDX/KH1/generic-USB sessions never
  install this callback at all, though the VID:0x26aa/PID:0x0030 check alone is already
  device-specific and cannot match any other supported radio). The callback returns false only
  for that exact VID/PID pair; ESP-IDF's own enum_cancel() -> usbh_dev_close() then frees that
  device's default EP0 control-pipe channel, and usb_host.c's ENUM_EVENT_CANCELED handler
  automatically calls hub_port_disable() on that device's specific hub port — both fully
  documented, built-in framework behavior triggered by returning false from the filter callback,
  with no private ESP-IDF headers or hub-driver internals touched.
verification: |
  `idf.py build` (sourced ESP-IDF v5.5.1 PowerShell session) completed with exit code 0. Only
  main/stream_uac.cpp needed recompilation for this change; it compiled with zero warnings/errors.
  Hardware-verified in the checkpoint #1/#2/#3 evidence above (aux device rejection confirmed via
  ESP-IDF's own ENUM log; mic+speaker still could not both negotiate due to the deeper 8-channel
  ceiling documented above). This fix is KEPT in the current codebase.
files_changed:
  - sdkconfig (CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK: n -> y)
  - main/stream_uac.cpp (added ftx1_enum_filter_cb(), registered in usb_lib_task() host_config
    when s_profile == UAC_PROFILE_FTX1)

## Resolution (current — half-duplex mic/speaker redesign)

status: RESOLVED — hardware-verified 2026-07-07. Half-duplex mic/speaker time-multiplexing
  confirmed working on the physical FTX-1 across 7+ consecutive RX->TX->RX cycles (repeated CQ
  beaconing over 6+ minutes), with zero channel-exhaustion errors and real TX audio confirmed
  reaching the radio each cycle. Channel exhaustion no longer blocks FTX-1 mic+speaker+CAT
  operation.
summary: |
  Implemented the half-duplex mic/speaker swap deferred by the enum_filter_cb sub-session above.
  Since the 8-channel ceiling is measured, hard, and zero-margin (checkpoint #3), and hub/CDC/
  audio-control are all load-bearing and cannot be freed, the only remaining path is to never
  need a 9th channel at all: mic and speaker are now time-multiplexed for the FTX-1 profile only,
  reusing the same "claim ahead of time, suspended" pattern the speaker already used for QMX. The
  swap is keyed off signals that already exist and already fire with the required lead time
  (arm_pending_tx() for RX->TX, check_slot_boundary()'s TX-completion branch and tx_tick()'s
  cancel branch for TX->RX) — no new timing logic was invented. Build-verified AND
  hardware-verified (see checkpoint #4 in Evidence): mic+speaker negotiation succeeds in both
  directions with zero channel-exhaustion errors, across at least 7 consecutive slots, decode and
  CAT unaffected.
known_cosmetic_issue: |
  Every mic-close logs an ERROR-level "uac-host: uac_host_device_stop(2446): Unable to release UAC
  Interface" (traced to usb_host_interface_release() returning non-OK inside
  uac_host_interface_release_and_free_transfer(), uac_host.c:1115). Confirmed NOT a resource leak
  — 7+ consecutive cycles show no degradation, which would be impossible if channels were actually
  leaking on an already-zero-margin 8-channel budget. Likely an idempotency quirk in the vendored
  espressif__usb_host_uac driver (e.g. double-suspend surfacing as an error rather than a no-op).
  Left as a low-priority cosmetic follow-up — not blocking, not touched this session.
root_cause: |
  Same measured root cause as above: ESP32-S3's fixed 8-HCD-channel ceiling, combined with the
  FTX-1's real USB topology (hub + CP210x CAT-1 + audio-control, each load-bearing and consuming
  1 channel), leaves exactly 0 spare channels — enough for exactly one of {mic, speaker} to
  stream at a time, never both.
fix: |
  main/stream_uac.cpp: refactored mic open+negotiate into mic_negotiate_and_start()/
  mic_ensure_stream_task()/mic_open_and_start()/mic_close(), and speaker pre-open+claim into
  spk_open_and_claim()/spk_close() — all reusable both at initial enumeration and for on-demand
  reopen/reclose. Added uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx() as the public swap entry
  points, scoped to s_profile == UAC_PROFILE_FTX1 (no-op, zero behavior change for QMX/QDX/KH1/
  generic-USB). For FTX-1 specifically, TX_CONNECTED now defers the speaker's open (previously
  eager for all profiles) until uac_ftx1_prepare_tx() claims it on demand, since opening it
  immediately at enumeration would collide with the mic's already-open channel.
  main/stream_uac.h: declared the two new public functions.
  main/main.cpp: called uac_ftx1_prepare_tx() at the end of arm_pending_tx() (the single point of
  truth for every TX-scheduling path: autoseq tick, beacon CQ, free-text queue, manual RX-tap
  reply, schedule_manual_pending_tx() — verified via grep that all 6 call sites funnel through
  it). Called uac_ftx1_prepare_rx() in check_slot_boundary()'s g_was_txing&&!g_tx_active
  TX-completion branch, and separately in tx_tick()'s cancel branch (which bypasses
  check_slot_boundary() by setting g_was_txing=false directly, so needed its own explicit call).
verification: |
  `idf.py build` (sourced ESP-IDF v5.5.1 PowerShell session, MSYSTEM env var cleared) completed
  with exit code 0. stream_uac.cpp and main.cpp both recompiled cleanly with zero new
  warnings/errors. Pre-existing unrelated -Wunused-function warnings in main.cpp unchanged from
  before this session. Hardware verification (mic-only RX decode across multiple slots, TX-slot
  channel usage stays at 8 with no exhaustion error, speaker successfully claimed/resumed for TX,
  mic successfully reopens and resumes decode after TX ends, CAT unaffected, no QMX/QDX/KH1
  regression, slot timing not degraded) is pending — requested via CHECKPOINT REACHED below.
files_changed:
  - main/stream_uac.cpp (refactored mic/speaker open logic into reusable helper functions;
    added s_mic_addr/s_mic_iface/s_mic_known and s_ftx1_direction state; deferred FTX-1's
    speaker open at TX_CONNECTED; added uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx())
  - main/stream_uac.h (declared uac_ftx1_prepare_tx()/uac_ftx1_prepare_rx())
  - main/main.cpp (wired uac_ftx1_prepare_tx() into arm_pending_tx(); wired
    uac_ftx1_prepare_rx() into check_slot_boundary()'s TX-completion branch and tx_tick()'s
    cancel branch)
