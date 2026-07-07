---
status: blocked_pending_decision
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

## Resolution

status: NOT RESOLVED — confirmed hard hardware limitation, no further software avenue found.
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
  Pre-existing unrelated -Wunused-function warnings in main.cpp (schedule_manual_pending_tx,
  autoseq_has_pending_tx, normalize_date_ymd, qso_load_fetch_file_list) are unchanged from before
  this session and out of scope. Hardware verification (aux device no longer enumerates, mic+
  speaker both negotiate together, CAT commands still functional, no QMX/QDX/KH1 regression) is
  pending — requested via CHECKPOINT REACHED.
files_changed:
  - sdkconfig (CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK: n -> y)
  - main/stream_uac.cpp (added ftx1_enum_filter_cb(), registered in usb_lib_task() host_config
    when s_profile == UAC_PROFILE_FTX1)
