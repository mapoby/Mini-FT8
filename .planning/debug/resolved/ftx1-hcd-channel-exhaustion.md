---
status: resolved
trigger: |
  With the FTX-1 speaker fix in place (see resolved/ftx1-speaker-24bit-rejected.md), the speaker
  now successfully claims USB host resources for the first time. However, this causes mic (RX)
  negotiation on the SAME boot to consistently fail with "HCD DWC: No more HCD channels available"
  -> "EP Alloc error: ESP_ERR_NOT_SUPPORTED" -> "No 48k UAC mic format for profile=ftx1_cp210x".
  Reproduced identically across 2 separate power cycles during Phase 5 (End-to-End Integration and
  Parity Testing) hardware checkpoint testing. This blocks simultaneous mic RX + speaker TX, which
  is required for any real FT8/FT4 QSO (must decode incoming signals while able to transmit
  replies).
created: 2026-07-07
updated: 2026-07-07
---

## Symptoms

- **Expected behavior:** Both the mic (RX, decode pipeline) and speaker (TX, DDS tone output) UAC
  interfaces should be able to open and remain claimed simultaneously, exactly as they already do
  for the QMX radio backend (which has worked in production before this milestone).
- **Actual behavior:** Once the speaker's candidate-scan loop successfully claims its USB host
  resources (2ch/16-bit/48000Hz, per the just-applied fix), the mic's subsequent candidate-scan
  attempt fails on its second candidate (2ch/16-bit/48000Hz — the format that DOES negotiate
  correctly when tried alone) with a hardware/driver-level resource error, not a format-mismatch
  error:
  ```
  I (18620) UAC_STREAM: Starting stream (profile=ftx1_cp210x) candidate 2/4: 48000Hz, 16-bit, 2ch
  E (18628) HCD DWC: No more HCD channels available
  E (18633) USBH: EP Alloc error: ESP_ERR_NOT_SUPPORTED
  W (18664) UAC_STREAM: Stream candidate failed: ESP_ERR_NOT_SUPPORTED
  ```
  Candidates 1, 3, 4 also fail (24-bit / 1ch formats, as expected — device doesn't support them),
  but candidate 2 (the one that actually negotiates when speaker isn't also active) fails
  specifically with the HCD channel-exhaustion error, not a format mismatch. End result:
  `No 48k UAC mic format for profile=ftx1_cp210x` — mic never opens at all when speaker succeeds
  first in the enumeration order.
- **Error messages:** `HCD DWC: No more HCD channels available`, `USBH: EP Alloc error:
  ESP_ERR_NOT_SUPPORTED`, `USB HOST: EP allocation error ESP_ERR_NOT_SUPPORTED`, `USB HOST: Claiming
  interface error: ESP_ERR_NOT_SUPPORTED`, `uac_host_interface_claim_and_prepare_transfer(1151):
  Unable to claim Interface`.
- **Timeline:** First observed today (2026-07-07) immediately after fixing the FTX-1 speaker's
  24-bit-rejection bug (see resolved/ftx1-speaker-24bit-rejected.md) — the speaker never
  successfully claimed any USB resources before that fix, so this resource contention was
  structurally impossible to observe until now.
- **Reproduction:** Boot the Cardputer with the FTX-1 (running the post-speaker-fix firmware)
  connected and selected as active radio. On enumeration, the speaker's candidate-scan succeeds
  first (claims resources), then the mic's candidate-scan runs immediately after and fails on its
  otherwise-working 2ch/16-bit candidate with the HCD channel error. Reproduced identically across
  2 separate power cycles.

## Additional observation (may or may not be related — flag but don't assume)

Immediately after the mic failure, an unexpected THIRD USB device attach is logged on the SAME
physical port session:
```
I (18728) UAC_STREAM: USB dev attached: VID:0x26aa PID:0x0030 cfgs:1
I (20793) UAC_STREAM:   IF num=0 alt=0 eps=1 class=0x02 subclass=0x02 proto=0x01
I (20806) UAC_STREAM:   -> CDC candidate iface 0
```
This is in addition to the already-known CP210x (VID:0x10c4 PID:0xea70, addr 2, the CAT chip) and
the C-Media-style audio codec (VID:0x0d8c PID:0x0016, addr 3). VID:0x26aa PID:0x0030 has not been
seen or explained before in this project. Given the FTX-1 exposes multiple USB "devices" at
different addresses through what is presumably an internal USB hub, this could be: (a) a genuine
third internal peripheral inside the FTX-1 (e.g. a HID/CDC interface for panel controls) that
happens to enumerate at this point in the boot sequence regardless of the mic failure, or (b) a
phantom/corrupted enumeration artifact caused by the channel-exhaustion error itself. Investigate
whether this device consumes a channel that could otherwise go to the mic, before assuming it's
unrelated noise.

## Current Focus

hypothesis: CONFIRMED — ESP32-S3's USB-OTG (DWC_OTG) core has a hardware-fixed, read-only total
  of 8 host channels (GHWCFG2.NUMHSTCHNL=7, +1). Once mic+speaker are both active alongside the
  FTX-1's actual USB topology (internal hub + CP2105 CAT + audio codec + a genuine 4th Yaesu
  peripheral at VID:0x26aa), the minimum required set of simultaneously-held HCD channels is 9 —
  exactly one more than the SoC provides. This is a hard architectural ceiling, not a firmware bug.
  Additionally confirmed: no VID/PID-based enumeration gate exists anywhere in the ESP-IDF USB Host
  stack that could suppress the Yaesu auxiliary device's control-pipe allocation before its
  identity is known, so there is no software mechanism to avoid this cost either.
test: Enumerated every USB device/endpoint that must hold a persistent HCD channel (pipe) once
  mic+speaker are both active, cross-referenced against ESP-IDF's usbh.c pipe-allocation logic and
  the DWC_OTG hardware channel-count register. Separately traced whether VID/PID could be used to
  gate enumeration before the default control pipe is allocated.
expecting: If total required channels <= 8, some other bug explains the failure. If total == 9,
  hypothesis confirmed as a hard ceiling exceeded by exactly 1. If a VID/PID enumeration gate
  existed before pipe allocation, a safe code fix would be possible; if the pipe is always
  allocated before VID/PID is knowable, no such fix exists.
next_action: DONE — root cause confirmed as a hard hardware ceiling with no safe fix available in
  the ESP-IDF USB Host stack's capabilities. Session finalized as investigated/documented, no code
  fix applied. Recommended future direction (TX-slot mic-close/reopen) recorded in Resolution.fix
  for a dedicated future session.
reasoning_checkpoint:
  hypothesis: "ESP32-S3's fixed 8-channel USB host controller (GHWCFG2.NUMHSTCHNL, read-only
    silicon register) cannot supply the 9 concurrent HCD channels that the FTX-1's actual USB
    topology requires once mic+speaker are both open, because: internal hub (default ctrl pipe +
    status-change interrupt pipe = 2) + CP2105 CAT-1 (default ctrl pipe + bulk IN + bulk OUT = 3,
    already minimal -- only interface 0 of 2 is claimed) + audio codec (default ctrl pipe + speaker
    OUT + mic IN = 3, both directions structurally required) + the genuine Yaesu-manufactured
    VID:0x26aa auxiliary device (default ctrl pipe only, never claimed by our code = 1) sum to 9.
    Furthermore, no VID/PID-based enumeration-skip mechanism exists in ESP-IDF's USB Host stack to
    avoid allocating that auxiliary device's control pipe in the first place."
  confirming_evidence:
    - "C:\\Espressif\\esp-idf-v5.5.1\\components\\soc\\esp32s3\\register\\soc\\usb_reg.h:9762 --
      USB_NUMHSTCHNL field documented 'RO; default: 7' inside USB_GHWCFG2_REG -- a read-only
      hardware config register baked into ESP32-S3 silicon at synthesis, confirming total
      channels = 7+1 = 8, non-negotiable via Kconfig or software."
    - "C:\\Espressif\\esp-idf-v5.5.1\\components\\hal\\esp32s3\\include\\hal\\usb_dwc_ll.h:362-365 --
      usb_dwc_ll_ghwcfg_get_channel_num() reads this same hardware field at runtime; confirms the
      HAL itself treats channel count as hardware-reported, not configurable."
    - "C:\\Espressif\\esp-idf-v5.5.1\\components\\usb\\usbh.c:382,389 -- hcd_pipe_alloc() is called
      once per USB device address to create its default control pipe at enumeration
      (USB_DEVICE_STATE_CONFIGURED), and this pipe/channel is held for the device's entire time on
      the bus (freed only at physical disconnect, line 430) -- regardless of whether any class
      driver ever claims an interface on that device."
    - "managed_components/espressif__usb_host_cdc_acm/cdc_acm_host.c:917-931 -- confirms the
      CDC-ACM driver's new_dev_cb probe (our cdc_new_dev_cb in stream_uac.cpp:335) is transient:
      it opens the device, calls the callback, then immediately usb_host_device_close()s it. This
      rules out a channel *leak* from our own descriptor-inspection code -- the mystery device's
      channel consumption is solely its own default control pipe from raw enumeration, not from
      anything we open."
    - "main/stream_uac.cpp:284-303 (cp210x_try_open) -- confirms only CP210x interface_idx=0
      (CAT-1) is ever opened; the CP2105's second interface (CAT-2) is never claimed, so no extra
      channel is spent there. Already minimal."
    - "main/stream_uac.cpp:203-278 (cdc_try_open) -- confirms the hint-scan / blind interface loop
      that could have opportunistically claimed the mystery CDC-class device is hard-gated by 'if
      (s_profile != UAC_PROFILE_QMX) return;' (CAT-02 boundary) and never runs for the FTX-1
      profile. Rules out our own code claiming an extra interface on the mystery device."
    - "Web search: USB VID 0x26AA is registered to YAESU MUSEN CO., LTD -- confirms the 'mystery'
      device is a genuine internal FTX-1 peripheral (not a phantom/corrupted enumeration artifact,
      not USB-IF-unassigned noise), so its default-pipe channel cost is real and unavoidable as
      long as it is present behind the FTX-1's internal hub."
    - "Symptoms.errors: mic candidate 2 (2ch/16-bit/48000Hz) fails with the HCD-channel error only
      when it runs *after* the speaker's successful claim in the same boot -- consistent with an
      8-channel budget being exactly exhausted by the 8th allocation (speaker) and the 9th
      allocation (mic) being the one that fails, matching enumeration order."
    - "C:\\Espressif\\esp-idf-v5.5.1\\components\\usb\\usbh.c:364-405 (device_alloc) -- confirms
      hcd_pipe_alloc() for the EP0 default control pipe happens at dev_addr=0, immediately upon
      port-reset-driven device discovery, BEFORE SET_ADDRESS and BEFORE GET_DESCRIPTOR(device) is
      ever issued. VID/PID is only readable via a GET_DESCRIPTOR transfer sent over that same
      already-allocated pipe. There is no earlier point in the enumeration sequence -- in usbh.c,
      hub.c, or the underlying HCD layer -- where VID/PID could gate whether the pipe is allocated,
      because the pipe is a prerequisite for learning the VID/PID in the first place."
    - "C:\\Espressif\\esp-idf-v5.5.1\\components\\usb\\hub.c (root/ext port reset + hub_port_disable
      at lines ~504, ~754) -- a port COULD be disabled before reset/enumeration, but only on a
      per-port-number basis with VID/PID still unknown at that time; using this to exclude the
      Yaesu auxiliary device would require hardcoding a fixed hub-port-to-peripheral mapping inside
      the FTX-1's internal hub, which is fragile (undocumented, may vary by unit/firmware revision)
      and was rejected as a workaround rather than a real fix."
  falsification_test: "If the summed channel count for hub+CP210x+audio-codec(both
    directions)+mystery-device came to <= 8, this hypothesis would be false and some other
    resource-management bug (e.g. a channel leak in our code) would have to explain the failure.
    It does not: the count is exactly 9, and every contributing pipe is independently verified as
    either hardware-mandatory (default control pipes, hub status pipe) or already-minimal in our
    own code (CP210x single-interface claim, transient descriptor probing). Separately, if ESP-IDF's
    USB Host stack exposed a VID/PID enumeration filter callback anywhere before hcd_pipe_alloc(),
    that would falsify 'no safe fix is possible' -- no such callback exists in usbh.c or hub.c;
    device_alloc()'s pipe allocation unconditionally precedes any descriptor read."
  fix_rationale: "N/A -- no code fix proposed. The confirmed root cause is a fixed hardware
    resource ceiling (ESP32-S3 silicon: 8 HCD channels, read-only register) being exceeded by
    exactly 1 channel given the FTX-1's real, Yaesu-confirmed USB topology (hub + CP2105 dual-UART
    + audio codec + one auxiliary Yaesu device). Our firmware already claims the minimum possible
    set of interfaces/endpoints; there is no further interface-claiming discipline left to apply
    that would free a channel without either (a) refusing to open the CAT-1 bulk IN direction
    (breaking already-verified bidirectional CAT query support from Phase 3), or (b) dynamically
    closing/reopening the mic's UAC pipe around each TX slot (a genuine architectural change with
    real-time risk under 15s/7.5s slot timing, out of scope for a minimal fix this session).
    VID/PID-based enumeration suppression of the auxiliary device is structurally impossible (the
    pipe is allocated before VID/PID is readable); port-based suppression is possible in principle
    but fragile and topology-dependent, and was rejected as a workaround rather than a real fix."
  blind_spots: "Have not empirically verified on real hardware whether usb_host_interface_claim()
    for the CP2105's single claimed interface allocates exactly 2 non-control channels (bulk
    IN+OUT) versus some other count, nor whether the internal hub's status-change interrupt pipe
    is unconditionally held open by the ESP-IDF hub driver vs. requested only transiently -- these
    are inferred from ESP-IDF source reading, not directly measured via register/channel dump on
    the physical device. Have not tested whether disabling CONFIG_USB_HOST_HUB_MULTI_LEVEL (if the
    Yaesu topology is only 1 hub level deep, not nested) changes anything -- it would not free a
    channel regardless (the downstream device still needs its own default pipe), so was not tested
    on hardware to avoid an unnecessary flash/disconnect cycle for a change already understood to
    be ineffective. Have not prototyped or hardware-tested the TX-slot mic-close/reopen approach --
    it is a recommendation for a future session, not a verified solution."
tdd_checkpoint: null

## Evidence

- timestamp: 2026-07-07
  checked: Debug UART log, 2 separate power-cycle boot sequences with the post-speaker-fix firmware.
  found: Speaker candidate-scan (added by the just-committed fix) succeeds first
    ("Speaker pre-opened+claimed"), then mic candidate-scan's otherwise-working 2ch/16-bit
    candidate fails specifically with "HCD DWC: No more HCD channels available", not a format
    rejection. Reproduced identically both times.
  implication: This is a genuine resource-contention bug triggered by the speaker fix succeeding,
    not a flaky/one-off enumeration glitch. Confirmed reproducible.

- timestamp: 2026-07-07
  checked: C:\Espressif\esp-idf-v5.5.1\components\soc\esp32s3\register\soc\usb_reg.h (USB_GHWCFG2_REG,
    USB_NUMHSTCHNL field) and components\hal\esp32s3\include\hal\usb_dwc_ll.h
    (usb_dwc_ll_ghwcfg_get_channel_num) and components\hal\usb_dwc_hal.c (chan_num_total).
  found: USB_NUMHSTCHNL is documented "RO; default: 7" -- a read-only hardware register field.
    usb_dwc_ll_ghwcfg_get_channel_num() returns hw->ghwcfg2_reg.numhstchnl + 1 = 8. This value comes
    directly from silicon, not Kconfig, and cannot be increased by any build-time or runtime
    configuration.
  implication: ESP32-S3 has a hard, non-negotiable ceiling of exactly 8 total USB host channels.
    Any bug analysis must work within this fixed budget.

- timestamp: 2026-07-07
  checked: C:\Espressif\esp-idf-v5.5.1\components\usb\usbh.c (device connect/pipe-alloc logic,
    lines ~330-430).
  found: hcd_pipe_alloc() is called once per attached USB device address to create its default
    control pipe (EP0) when the device reaches USB_DEVICE_STATE_CONFIGURED during enumeration.
    This pipe/channel is held for the device's entire lifetime on the bus and is only freed
    (hcd_pipe_free) on physical disconnect -- independent of whether any class driver ever opens
    or claims an interface on that device.
  implication: Every USB device attached behind the FTX-1's internal hub (hub itself, CP2105,
    audio codec, and the unexplained 4th device) permanently consumes at least 1 HCD channel for
    its default control pipe merely by being present on the bus, whether or not our firmware ever
    uses it.

- timestamp: 2026-07-07
  checked: managed_components/espressif__usb_host_cdc_acm/cdc_acm_host.c lines 917-931
    (USB_HOST_CLIENT_EVENT_NEW_DEV handling) and main/stream_uac.cpp cdc_new_dev_cb (line 335),
    cdc_try_open (line 203, CAT-02 guard at line 206), cp210x_try_open (line 284-303, claims only
    interface_idx=0).
  found: (1) The CDC-ACM driver's new-device probe opens the device, invokes our new_dev_cb, then
    immediately closes it again -- purely transient, does not leak a channel. (2) Our cdc_try_open()
    hint-scan (which could opportunistically claim a CDC-class device such as the mystery
    VID:0x26aa one) is hard-gated by "if (s_profile != UAC_PROFILE_QMX) return;" and never executes
    for the FTX-1 profile. (3) cp210x_try_open() only ever opens CP210x interface_idx=0 (CAT-1);
    the CP2105's second interface (CAT-2) is never claimed.
  implication: Our own firmware is already claiming the minimum possible set of interfaces/
    endpoints. There is no channel being wasted by our code; the mystery device's channel cost is
    solely its own unavoidable default control pipe from raw bus enumeration.

- timestamp: 2026-07-07
  checked: Web search for USB VID 0x26AA.
  found: VID 0x26AA is registered to YAESU MUSEN CO., LTD. (the FTX-1's manufacturer).
  implication: The "mystery" device is a genuine internal FTX-1 peripheral, not a phantom/corrupted
    enumeration artifact caused by the channel-exhaustion error. Its default-pipe channel
    consumption (1 channel) is real, structural, and unavoidable as long as it remains attached
    behind the FTX-1's internal hub.

- timestamp: 2026-07-07
  checked: Full channel budget tally across all 4 devices needed for simultaneous mic+speaker+CAT
    operation: internal hub (default ctrl pipe + status-change interrupt pipe = 2), CP2105 CAT-1
    (default ctrl pipe + bulk IN + bulk OUT = 3, already minimal), audio codec (default ctrl pipe +
    speaker OUT + mic IN = 3, both directions structurally required for a QSO), Yaesu VID:0x26aa
    device (default ctrl pipe only, never claimed = 1).
  found: Sum = 2 + 3 + 3 + 1 = 9 channels required; ESP32-S3 hardware ceiling = 8. Before the
    speaker fix, the audio codec only needed ctrl+mic (2 channels), giving a total of 8 -- exactly
    at the ceiling, which is why mic-alone always worked. The speaker fix adds exactly 1 more
    required channel (speaker OUT), pushing the total to 9 -- exactly 1 over budget. This matches
    the observed failure occurring on the very next channel-allocation call (mic candidate 2) after
    the speaker's successful claim.
  implication: Root cause confirmed with an exact, falsifiable channel-count match to the observed
    failure point. This is a hard architectural ceiling, not a resource leak or claim-ordering bug
    in our firmware.

- timestamp: 2026-07-07
  checked: Whether ESP-IDF's USB Host Library (usbh.c, hub.c) offers any mechanism to intentionally
    skip/reject enumeration of a specific device by VID/PID before its default control pipe (EP0)
    is allocated -- specifically device_alloc() (usbh.c:364-405) and the port-reset/new-device flow
    in hub.c (root_port_disable at line 504, ext_hub_port_disable at line ~360/765).
  found: device_alloc() calls hcd_pipe_alloc() for the EP0 default control pipe at dev_addr=0
    (usbh.c:373-386) immediately upon port-reset-driven device discovery, setting
    USB_DEVICE_STATE_DEFAULT -- this happens strictly BEFORE SET_ADDRESS and BEFORE
    GET_DESCRIPTOR(device) is ever issued. VID/PID is only knowable via a GET_DESCRIPTOR transfer
    sent over that same already-allocated default pipe (usbh_dev_set_addr at usbh.c:1228,
    subsequent descriptor read). Port-disable functions (root_port_disable/ext_hub_port_disable) do
    exist and could stop a port before reset, but only on a per-port-number basis with VID/PID
    still unknown at that time -- using them to exclude "the Yaesu auxiliary device" specifically
    would require hardcoding a fixed hub-port-to-peripheral mapping inside the FTX-1's internal hub.
  implication: No VID/PID-based enumeration gate exists anywhere in the ESP-IDF USB Host stack
    before the control-pipe/channel is already allocated -- this is a structural property of USB
    enumeration itself (the pipe is a prerequisite for reading the descriptor that reveals VID/PID),
    not a policy gap ESP-IDF could reasonably close. Port-based exclusion is theoretically possible
    but fragile (undocumented, topology-dependent, may vary by unit/firmware revision) and was
    rejected as a workaround rather than a genuine fix, consistent with this session's guidance to
    avoid forcing something fragile.

## Eliminated

- hypothesis: CP210x unnecessarily claims its second (CAT-2) interface, wasting a channel.
  evidence: main/stream_uac.cpp:303 cp210x_try_open() calls cp210x_vcp_open(CP2105_PID,
    /*interface_idx=*/0, ...) -- only interface 0 is ever opened. Interface 1 is never claimed.
  timestamp: 2026-07-07

- hypothesis: The mystery VID:0x26aa device is being unnecessarily claimed/kept-open by our own
    firmware (e.g. via the CDC-ACM hint-scan), wasting a channel that could go to the mic.
  evidence: cdc_try_open()'s hint-scan is hard-gated by "if (s_profile != UAC_PROFILE_QMX) return;"
    (CAT-02 boundary) and never runs for UAC_PROFILE_FTX1. The CDC-ACM driver's new_dev_cb probe
    (cdc_new_dev_cb) that logs this device's descriptors is transient (opens, calls back, closes
    immediately per cdc_acm_host.c:917-931) -- it never leaves a claimed interface or a leaked
    channel behind.
  timestamp: 2026-07-07

- hypothesis: The mystery VID:0x26aa attach is a phantom/corrupted enumeration artifact caused by
    the channel-exhaustion error itself, and therefore irrelevant to the real fix.
  evidence: Web search confirms VID 0x26AA is registered to YAESU MUSEN CO., LTD. -- this is a
    genuine internal FTX-1 peripheral, not enumeration noise. It is directly relevant: its
    unavoidable default-control-pipe channel is one of the 9 required channels that exceed the
    8-channel hardware ceiling.
  timestamp: 2026-07-07

- hypothesis: The firmware could refuse to enumerate the Yaesu auxiliary device (VID:0x26aa) by
    VID/PID, preventing its control-pipe channel from ever being allocated and freeing 1 channel
    for mic+speaker to coexist.
  evidence: Traced ESP-IDF's usbh.c device_alloc() (lines 364-405): the EP0 default control pipe is
    allocated via hcd_pipe_alloc() immediately upon port-reset-driven device discovery, strictly
    before SET_ADDRESS and before any GET_DESCRIPTOR(device) transfer. VID/PID is only readable via
    a descriptor read sent over that same already-allocated pipe -- there is no earlier point in
    usbh.c, hub.c, or the HCD layer where VID/PID could gate pipe allocation. Port-based disabling
    exists (root_port_disable/ext_hub_port_disable) but only by port number, with VID/PID unknown at
    that time, and would require hardcoding a fragile, undocumented hub-port-to-peripheral mapping.
  timestamp: 2026-07-07

## Resolution

root_cause: |
  ESP32-S3's USB-OTG (DWC_OTG) host controller has a hardware-fixed total of 8 host channels
  (GHWCFG2.NUMHSTCHNL register field, read-only silicon value, confirmed in
  C:\Espressif\esp-idf-v5.5.1\components\soc\esp32s3\register\soc\usb_reg.h:9762, default=7,
  +1 = 8). This is not configurable via Kconfig or any software setting.

  The FTX-1's actual USB topology (confirmed via live hardware enumeration logs plus a VID lookup
  confirming VID:0x26aa is a genuine Yaesu-manufactured internal peripheral, not enumeration noise)
  requires the following simultaneously-held HCD channels once mic (RX) and speaker (TX) are both
  active alongside CAT control:
    - Internal USB hub: default control pipe + status-change interrupt pipe = 2
    - CP2105 CAT-1 (Enhanced COM, interface 0 only -- already minimal): default control pipe +
      bulk IN + bulk OUT = 3
    - USB audio codec (both directions, now that the companion speaker-fix session made TX audio
      actually negotiate): default control pipe + speaker OUT + mic IN = 3
    - Yaesu auxiliary device (VID:0x26aa): default control pipe only, never claimed by our
      firmware = 1
    Total = 9 channels required; hardware ceiling = 8. Exceeded by exactly 1.

  Before the speaker fix (resolved/ftx1-speaker-24bit-rejected.md, commit ffe422c), the audio codec
  only ever claimed its default control pipe + mic IN (2 channels), for a total of 8 -- exactly at
  the ceiling, which is why mic-alone always worked and this bug was structurally impossible to
  observe until the speaker began successfully claiming its own channel.

  Our firmware's interface-claiming discipline is already minimal (CP210x opens only 1 of its 2
  interfaces; the CDC-ACM driver's descriptor-probe of unclaimed devices is transient and does not
  leak a channel). There is no channel being wasted by application code that could be freed by a
  smaller/more careful claim. Additionally confirmed: ESP-IDF's USB Host stack (usbh.c, hub.c)
  provides no VID/PID-based enumeration gate that could suppress the Yaesu auxiliary device's
  control-pipe allocation -- the pipe is allocated before the device's identity is ever readable,
  which is a structural property of USB enumeration itself, not a software policy gap.
fix: |
  No safe code fix applied this session. This is a hard SoC hardware ceiling, not a firmware
  resource-management bug, and no mechanism exists within the ESP-IDF USB Host stack's capabilities
  to avoid it:
    - VID/PID-based enumeration suppression of the Yaesu auxiliary device is structurally
      impossible (its control pipe is allocated before its VID/PID can be read).
    - Port-based enumeration suppression (disabling a specific hub port before reset) exists in
      principle but would require hardcoding an undocumented, unit/firmware-revision-dependent
      hub-port-to-peripheral mapping inside the FTX-1 -- rejected as fragile, not a real fix.
    - Reducing our own firmware's interface-claiming footprint further is not possible without
      breaking existing functionality (CP210x already claims only 1 of 2 interfaces; refusing the
      CAT-1 bulk IN direction would break bidirectional CAT query support from Phase 3).
  Forcing a workaround under these constraints would be fragile; documenting this as a known,
  understood hardware limitation is the honest outcome for this session, per the explicit
  investigation guidance to avoid forcing something fragile when a genuine, safe fix isn't
  available.

  Recommended direction for a dedicated future session (not implemented/verified this session):
  since FT8/FT4 is half-duplex within a slot (no need to decode incoming audio during the ~15s/7.5s
  slot in which you are transmitting), it may be architecturally sound to close the mic's UAC
  device (freeing its 1 channel) immediately before opening/activating the speaker for TX, and
  reopen the mic immediately after TX completes. This would need careful verification that UAC
  device open/close latency does not eat into the slot timing budget, and is a larger,
  higher-risk change than this debug session's scope -- it should be scoped and planned as its own
  phase/task rather than attempted as a quick fix.
verification: |
  Not applicable -- no code fix was applied, so no hardware re-verification of a fix was performed.
  Root cause is verified/confirmed via direct hardware register documentation (read-only
  channel-count register), direct ESP-IDF source reading (per-device default-pipe allocation
  lifecycle, and the absence of any VID/PID enumeration gate before pipe allocation), direct
  reading of this project's own firmware (confirming minimal interface claims and transient
  descriptor probing), and an external VID lookup (confirming the previously-unexplained device is
  a genuine Yaesu peripheral, not noise). The exact channel arithmetic (8 before speaker fix, 9
  after) matches the observed failure transitioning from "always worked" to "fails on the very next
  channel allocation after the speaker succeeds" with no gap in the explanation.
files_changed: []
