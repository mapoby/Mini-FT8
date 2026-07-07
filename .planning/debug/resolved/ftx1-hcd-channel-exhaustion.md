---
status: resolved
trigger: |
  Follow-up to the resolved ftx1-hcd-channel-exhaustion investigation (same slug, reopened). The
  prior session confirmed a hard ESP32-S3 8-channel USB host ceiling vs. 9 channels required once
  mic+speaker are both active on the FTX-1. VID/PID-based enumeration suppression of the extra
  Yaesu auxiliary device was ruled out as structurally impossible. This follow-up investigates a
  different angle: can the CP210x CAT-1 interface's bulk IN channel (which our own application code
  never actually uses -- data_cb is NULL in cp210x_try_open(), confirmed by code reading) be
  explicitly freed AFTER the interface has already opened/enumerated normally, rather than
  preventing its allocation in the first place?
created: 2026-07-07
updated: 2026-07-07
---

## Symptoms

(Carried over from the original investigation — see resolved/ftx1-hcd-channel-exhaustion.md for
the full original write-up. Summary: ESP32-S3 has a hard 8 host-channel ceiling; FTX-1's actual USB
topology needs 9 once mic+speaker both active; mic negotiation fails with "HCD DWC: No more HCD
channels available" once the speaker successfully claims its channel.)

## New angle being investigated

`cp210x_try_open()` (main/stream_uac.cpp ~line 284) opens the CP210x CAT-1 interface via
`cp210x_vcp_open()` with `data_cb = NULL` — our firmware never registers a callback to process
incoming bulk IN data, and a full-codebase grep confirms no CAT read/receive logic exists anywhere
in main/radio_control_ftx1.cpp. The bulk IN channel for CP210x (channel #4 in the original
breakdown) is therefore allocated by the vendored `espressif__usb_host_cdc_acm` driver but never
actually used by our application.

The original investigation confirmed the vendored driver has no supported "OUT-only open" mode
(`cdc_acm_host.c` has `assert(in_ep_desc)` and unconditionally submits a continuous poll on the
bulk IN endpoint as part of `cdc_acm_transfers_allocate()`/`cdc_acm_host_open()`), ruling out
preventing the IN channel's allocation at open time without patching the vendored driver.

**This follow-up asks a different question:** rather than preventing the IN channel's allocation,
can it be explicitly released/freed *after* the interface has already opened normally (letting the
driver's own open sequence complete as designed), while keeping the OUT endpoint's channel and CAT
functionality (TX1;/TX0;/FA/MD0C; commands) alive? This would only need to happen once, after
CP210x enumeration completes and before the audio codec's mic/speaker negotiation is attempted
(reordering may also be relevant — investigate whether enumeration/negotiation order can be
controlled, or whether this must be a true "free after open, before contention" step).

## Current Focus

hypothesis: DISCONFIRMED — no supported mechanism exists to free only the CP210x CAT-1 bulk IN
  channel while keeping bulk OUT (CAT TX) alive; all three USB Host stack layers (raw HCD,
  usbh.c/usb_host.c interface abstraction, CDC-ACM driver) treat an interface's endpoints as one
  atomic, all-or-nothing resource.
test: Read hcd_dwc.c hcd_pipe_free(), usb_host.c interface_claim()/interface_release()/
  ep_wrapper_alloc()/ep_wrapper_free()/endpoint_halt/flush/clear(), and cdc_acm_host.c's
  in_xfer_cb/poll-resubmission/cdc_acm_host_close() to determine whether any layer exposes a safe
  per-endpoint release while an interface stays claimed.
expecting: A safe per-endpoint release API at any layer would confirm the new angle viable; its
  absence at all three layers (plus the concrete precedent of an earlier crash this session from a
  related out-of-band resource-state inconsistency) confirms it is not.
next_action: DONE — original hard-ceiling finding reconfirmed; this follow-up angle closed with no
  safe fix found. Session archived to resolved/. No hardware changes made, no code changed.
reasoning_checkpoint:
  hypothesis: "The CP210x CAT-1 interface's bulk IN channel, though allocated by the CDC-ACM driver
    at open time, can be explicitly freed afterward at some layer of the ESP-IDF USB Host stack
    without destabilizing the driver or breaking CAT TX (bulk OUT), freeing exactly 1 channel for
    mic+speaker to both negotiate."
  confirming_evidence:
    - "hcd_dwc.c hcd_pipe_free() (line 1981-2002) can free a single pipe independently at the raw
      HCD layer in isolation — this is real and true, but only at the lowest layer."
    - "usb_host.c interface_claim()/interface_release() (line 1314-1426) allocate/free ALL endpoints
      of a claimed interface as one atomic array with no public per-endpoint release; the only
      per-endpoint public calls (usb_host_endpoint_halt/flush/clear, line 1508-1568) never release
      the underlying channel."
    - "cdc_acm_host.c's only release primitive, cdc_acm_host_close() (line 678-720), tears down both
      the IN and OUT endpoints of the data interface together via one usb_host_interface_release()
      call (line 717); its poll-resubmission logic (in_xfer_cb, line 784-810) runs unconditionally
      on data.in_xfer whenever it is non-NULL, independent of whether a callback is registered."
  falsification_test: "If any of the three layers (raw HCD, usbh.c/usb_host.c interface abstraction,
    or cdc_acm_host.c) exposed a supported per-endpoint release that leaves sibling endpoints and
    the interface claim intact, the hypothesis would be confirmed viable and worth prototyping. None
    of the three do — this was checked exhaustively by reading each layer's actual free/release
    code, not inferred."
  fix_rationale: "N/A — hypothesis disconfirmed, no fix proposed. Bypassing the public API to call
    the private per-endpoint free function directly would corrupt usb_host.c's interface bookkeeping
    and leave the CDC-ACM driver referencing a freed pipe on its next poll cycle, risking the same
    class of abort() this project already experienced once this session from an unrelated
    out-of-band USB Host Library state inconsistency (cdc_acm_host.c:717)."
  blind_spots: "Have not empirically tested on hardware whether freeing the pipe via the private,
    unsupported usbh_ep_free()/hcd_pipe_free() path actually crashes (this was not attempted,
    correctly, given the source-level evidence of unconditional poll resubmission and the concrete
    precedent of a related crash this session — attempting it would be exactly the kind of forced,
    fragile workaround this investigation was told to avoid). If a future ESP-IDF version adds a
    supported per-endpoint release API, this conclusion should be re-checked."
tdd_checkpoint: null

## Evidence

(Original investigation's evidence carries over — see resolved/ftx1-hcd-channel-exhaustion.md.)

- timestamp: 2026-07-07
  checked: main/radio_control_ftx1.cpp (full file), main/stream_uac.cpp cp210x_try_open()
    (~line 284-330), grep for "cat_cdc_ready|cat_cdc_send|data_rx|read" across main/.
  found: `cp210x_try_open()` passes `data_cb = NULL` to `cp210x_vcp_open()`. No CAT read/receive
    logic exists anywhere in the FTX-1 backend — only `cat_cdc_send()` (write) exists. The
    `in_buffer_size = 256` config comment claims "FTX-1 needs RX for query replies (Phase 3)" but
    no code processes any received bytes; this appears to be either dead/anticipated-but-unused
    configuration, or a stale comment from an earlier design that changed.
  implication: Our application layer genuinely does not need the CP210x's bulk IN channel. If it
    can be safely freed at the HCD level without destabilizing the CDC-ACM driver, this could
    resolve the 9-vs-8 channel deficit without touching the FTX-1's actual audio/mic/speaker
    negotiation logic at all.

- timestamp: 2026-07-07
  checked: C:\Espressif\esp-idf-v5.5.1\components\usb\hcd_dwc.c `hcd_pipe_free()` (line 1981-2002).
  found: At the raw HCD layer, a single pipe genuinely CAN be freed independently — it removes only
    that one `pipe_t` from the port's idle-pipe list and frees only its own `chan_obj`/buffers. It
    has no awareness of sibling pipes on the same interface. Precondition: the pipe must have no
    in-flight URB and not be mid-execution (`ESP_ERR_INVALID_STATE` otherwise).
  implication: Question 1 (raw HCD layer) answered YES in isolation — but this is the lowest layer;
    two more layers sit on top of it that must also be checked (usbh.c's endpoint/interface
    abstraction, then the CDC-ACM driver itself).

- timestamp: 2026-07-07
  checked: C:\Espressif\esp-idf-v5.5.1\components\usb\usb_host.c `interface_claim()` (line 1314-1371),
    `interface_release()` (line 1373-1426), `ep_wrapper_alloc()`/`ep_wrapper_free()` (line 1246-1289),
    `usb_host_endpoint_halt/flush/clear()` (line 1508-1568).
  found: `usb_host_interface_claim()` (the public API the CDC-ACM driver calls) allocates ONE
    `hcd_pipe` per endpoint in the interface, all stored together in a single fixed-size array
    `intf_obj->constant.endpoints[bNumEndpoints]` owned by one `interface_t` object. There is NO
    public API to free a single endpoint's pipe independently — only `usb_host_interface_release()`
    exists, and it iterates ALL endpoints of that interface, requiring ALL of them to have zero
    in-flight URBs / not be pending before it frees ANY of them, then frees them together as one
    atomic unit. `usb_host_endpoint_halt/flush/clear()` are the only per-endpoint public calls that
    exist, and none of them free the underlying HCD pipe/channel — they only halt/flush the queued
    transfer(s) on it; the channel allocation is untouched and the channel remains consumed.
    The private per-endpoint free function (`usbh_ep_free()`, used internally by `ep_wrapper_free()`)
    does exist one layer below, but it is not exposed to class drivers (not part of `usb_host.h`).
  implication: Two layers above raw HCD (usbh.c's interface abstraction, and the public usb_host
    client API that CDC-ACM is built on) treat "all endpoints of one claimed interface" as a single,
    atomic, all-or-nothing resource. There is no supported way for a class driver — including
    CDC-ACM — to release only the bulk IN pipe of the CP210x's CAT-1 interface while keeping the
    bulk OUT pipe (and CAT functionality) alive. Freeing just the IN pipe would require reaching
    past this API and calling the private `usbh_ep_free()`/raw `hcd_pipe_free()` directly on one
    slot of `intf_obj->constant.endpoints[]` while leaving that slot's pointer non-NULL and the
    interface object otherwise intact — corrupting the bookkeeping that
    `usb_host_interface_release()` and `cdc_acm_host_close()` rely on later (double-free /
    use-after-free at close time).

- timestamp: 2026-07-07
  checked: managed_components/espressif__usb_host_cdc_acm/cdc_acm_host.c — `in_xfer_cb()` (line
    784-810), `cdc_acm_reset_in_transfer()` (line ~109-122), `cdc_acm_host_open()`'s poll
    resubmission (line 203-205, 219, 902-908), `cdc_acm_host_close()` (line 678-720, specifically
    the `usb_host_interface_release()` call at line 717 that already caused an abort() this session
    on an unrelated unexpected-disconnect path).
  found: The CDC-ACM driver continuously self-resubmits the bulk IN transfer regardless of whether
    a `data_cb`/`in_cb` is registered (`if (cdc_dev->data.in_xfer) { ...submit... }` — no check on
    `in_cb`). This confirms the FTX-1's CP210x CAT-1 IN pipe is indeed being polled uselessly today
    (matches the new-angle hypothesis). However, the driver has exactly one way to stop this: full
    `cdc_acm_host_close()`, which tears down BOTH the IN and OUT endpoints of the data interface
    together via a single `usb_host_interface_release()` call (line 717) — there is no CDC-ACM API
    to say "stop polling/using IN only, keep OUT and the interface claim alive." If the IN pipe's
    HCD channel were freed out from under the driver by any external means, `cdc_dev->data.in_xfer`
    would remain non-NULL and the driver would keep calling `usb_host_transfer_submit()` against a
    freed pipe handle on every poll cycle — undefined behavior, most plausibly landing in the exact
    same class of crash already observed this session (`ESP_ERROR_CHECK`/`abort()` on an
    inconsistent USB Host Library state).
  implication: Confirms the new angle's own stated risk. There is no supported mechanism, at any of
    the three layers (raw HCD, usbh.c interface/endpoint abstraction, or CDC-ACM driver itself), to
    free only the CP210x's bulk IN channel while leaving CAT (bulk OUT) functional. The only
    "release" primitive available closes the whole interface, which would also kill CAT TX
    (FA/MD0C;/TX1;/TX0; commands use the same bulk OUT pipe on the same interface).

## Eliminated

(Original investigation's eliminated hypotheses carry over — see resolved file. VID/PID-based
enumeration suppression already ruled out; this is a genuinely new, different mechanism.)

- hypothesis: The CP210x CAT-1 interface's unused bulk IN channel can be explicitly freed after
    normal open, using a supported API, to free 1 channel for mic+speaker.
  evidence: Traced all three layers between application code and hardware. (1) Raw HCD layer
    (hcd_dwc.c hcd_pipe_free(), line 1981) CAN free one pipe independently in isolation. (2) The
    usbh.c/usb_host.c layer immediately above it (interface_claim()/interface_release(), line
    1314-1426) treats every endpoint of one claimed interface as a single atomic unit stored in one
    fixed array — no public per-endpoint free exists; usb_host_endpoint_halt/flush/clear() (line
    1508-1568) only affect queued transfers, never the underlying channel allocation. (3) The
    CDC-ACM driver built on top (cdc_acm_host.c) has exactly one release primitive,
    cdc_acm_host_close(), which tears down BOTH the IN and OUT endpoints of the data interface
    together (line 717) — there is no API to stop/forget only the IN endpoint while keeping OUT
    (and therefore CAT TX) alive. The driver's poll-resubmission logic
    (in_xfer_cb/cdc_acm_reset_in_transfer) runs unconditionally whenever data.in_xfer is non-NULL,
    independent of whether in_cb is registered, so it would keep referencing a pipe freed out from
    under it by any lower-level workaround, risking the same class of abort()/crash already observed
    this session at cdc_acm_host.c:717 during an unrelated unexpected-disconnect event.
  timestamp: 2026-07-07

## Resolution

root_cause: |
  Same underlying root cause as the original investigation (see resolved/ftx1-hcd-channel-exhaustion.md):
  ESP32-S3's USB-OTG host controller has a hardware-fixed ceiling of 8 HCD channels, and the FTX-1's
  real USB topology requires 9 once mic+speaker are both active alongside CAT. This follow-up
  additionally confirms there is no software mechanism, at any layer of the ESP-IDF USB Host stack
  (raw HCD, the usbh.c/usb_host.c endpoint-and-interface abstraction, or the vendored CDC-ACM
  driver), to free only the CP210x CAT-1 interface's unused bulk IN channel while keeping its bulk
  OUT channel (and therefore CAT TX) alive. All three layers were checked:
    - hcd_dwc.c's hcd_pipe_free() can free a single pipe in isolation, but nothing above it exposes
      that capability per-endpoint.
    - usb_host.c's interface_claim()/interface_release() treat every endpoint of a claimed interface
      as one atomic array; the only public per-endpoint calls (halt/flush/clear) never release the
      channel itself.
    - cdc_acm_host.c's only release primitive, cdc_acm_host_close(), tears down the whole data
      interface (both bulk endpoints) together, and its poll-resubmission logic
      (in_xfer_cb/cdc_acm_reset_in_transfer) runs unconditionally on data.in_xfer regardless of
      whether a callback is registered, so it would reference a pipe freed out from under it by any
      workaround that reached past the public API — the same class of abort()/crash already
      observed this session at cdc_acm_host.c:717.
  Bypassing these layers to free just the IN pipe would require directly manipulating private
  usbh.c/usb_host.c structures (interface_t.constant.endpoints[], the CDC-ACM driver's internal
  data.in_xfer bookkeeping) underneath APIs that were not designed to be used that way — this is not
  a "safe but obscure" mechanism, it is unsupported at every layer above raw HCD, and the specific
  risk (driver referencing a freed pipe on its next poll) is not hypothetical: an equivalent
  inconsistent-state abort() already occurred once this session on a related but separate code path.
fix: |
  No fix attempted, and none is applied. Per the explicit investigation guidance, a genuinely unsafe
  or unsupported mechanism (freeing a pipe out from under a driver's own internal bookkeeping,
  bypassing two layers of API-enforced atomicity) is not an acceptable trade for closing a 1-channel
  deficit, especially given this project already experienced a real, related USB Host Library
  abort() this session from a different out-of-band resource-state inconsistency. The honest
  conclusion is that this follow-up angle does not offer a safe path either, and the original
  hard-ceiling finding from resolved/ftx1-hcd-channel-exhaustion.md stands unchanged. The only
  previously identified candidate direction remains the same as before (TX-slot mic-close/reopen,
  a larger architectural change out of scope for a debug session, not attempted or verified here).
verification: |
  Not applicable — no code fix was applied, so nothing to verify on hardware. This follow-up's
  conclusion is verified via direct ESP-IDF source reading across all three relevant layers
  (hcd_dwc.c, usbh.c/usb_host.c, and the vendored cdc_acm_host.c), each confirming the same
  atomic-interface constraint independently, plus the concrete precedent of an actual crash this
  session from a related class of out-of-band USB Host Library state inconsistency.
files_changed: []
