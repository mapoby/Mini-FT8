# Pitfalls Research

**Domain:** ESP32-S3 USB-host CAT+audio radio integration (Yaesu FTX-1 via CP210x VCP + bidirectional UAC)
**Researched:** 2026-07-04
**Confidence:** MEDIUM — grounded in this codebase's existing hard-won USB-host tuning (`stream_uac.cpp`, `radio_control_qmx.cpp`) plus verified ESP-IDF/esp-usb documentation and Yaesu CAT manual excerpts; audio-descriptor and real-device PTT-latency specifics are unconfirmed without hardware (flagged LOW where noted).

## Critical Pitfalls

### Pitfall 1: FIFO/channel budget silently exceeded when adding a third USB client

**What goes wrong:**
`stream_uac.cpp` hand-tunes the ESP32-S3's 200 FIFO lines for exactly two simultaneous clients: bidirectional isochronous UAC (rx=91 lines/364B, ptx=91 lines/364B) plus CDC-ACM's small non-periodic bulk-OUT (nptx=18 lines/72B) — see the `host_config.fifo_settings_custom` block and its comment "Built-in Kconfig biases do not cover 24-bit/48k/stereo MPS=300 in both directions at once." Adding a CP210x VCP client (its own bulk IN+OUT endpoints, separate from the existing CDC-ACM's OUT-only 64-byte CAT path) introduces a fourth endpoint set competing for the same fixed FIFO/channel pool. If the FTX-1's UAC descriptor also uses a larger max-packet-size (unknown until hardware is probed), the existing 91/18/91 split may no longer fit, or channel allocation (separately limited, ~8-16 physical channels depending on variant) may run out during enumeration, causing silent device-open failures.

**Why it happens:**
The FIFO partition and channel budget are invisible until enumeration fails at runtime; there's no compile-time check. Copy-pasting the QMX profile's FIFO numbers for an FTX-1 profile without re-deriving them from the FTX-1's actual descriptors (once probed on real hardware) is the natural shortcut.

**How to avoid:**
Add a distinct `UAC_PROFILE_FTX1` FIFO branch (mirroring the existing `if (s_profile == UAC_PROFILE_QMX)` branch in `usb_lib_task`) sized for the FTX-1's actual endpoint MPS values once known, and budget nptx lines for CP210x's larger bulk transfers (not the CDC-ACM 64-byte case). Log `usb_host_lib_handle_events` / device-open return codes loudly during bring-up; don't assume default Kconfig FIFO sizing "just works" for a new device family.

**Warning signs:**
`uac_host_device_open` or the VCP open call returns `ESP_ERR_NOT_FOUND`/`ESP_ERR_NO_MEM` only when both audio directions and CAT are active together (works fine with audio alone or CAT alone — a sign of FIFO/channel exhaustion, not a driver bug).

**Phase to address:**
Phase covering FTX-1 USB bring-up / device enumeration — before CAT command work, since this determines whether both interfaces can even coexist on the bus.

---

### Pitfall 2: CDC-ACM interface-hint scan misclaims the CP210x vendor interface

**What goes wrong:**
`cdc_new_dev_cb` in `stream_uac.cpp` scans the attached device's interface descriptors for `bInterfaceClass == USB_CLASS_COMM` (0x02) to set `s_cdc_iface_hint`, and `cdc_try_open`'s fallback loop blindly probes interfaces 0-11 with `cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, iface, ...)` if no hint or QMX VID/PID match is found. The CP210x is a **vendor-specific class (0xFF)**, not CDC-ACM — the hint scan won't find it, but the blind interface-scan fallback will still attempt `cdc_acm_host_open()` against its interface number. Whether this fails cleanly (`ESP_ERR_NOT_FOUND`) or corrupts the CP210x's interface claim depends on IDF version behavior and is not guaranteed safe.

**Why it happens:**
The existing scan logic was written for CDC-ACM-only radios (QMX/QDX). It has no concept of a vendor-class VCP chip and will run unconditionally unless explicitly gated by profile.

**How to avoid:**
Gate the entire CDC-ACM install/scan path (driver install, `cdc_new_dev_cb`, `cdc_try_open` fallback loop) behind `s_profile == UAC_PROFILE_QMX` (it already is for driver install — extend the same gating to the open/scan logic). For `UAC_PROFILE_FTX1`, install `usb_host_vcp` instead and let it claim the interface via its own VID/PID-based driver dispatch (CP210x is matched by VID/PID, not class byte, so no ambiguity with the CDC-ACM scan).

**Warning signs:**
FTX-1 CAT port fails to open, or opens but returns garbage/no response, specifically when the CDC-ACM fallback scan loop's log lines (`"CDC open iface %d failed"`) appear for interfaces that belong to the CP210x chip.

**Phase to address:**
Phase adding the FTX-1 CP210x/`usb_host_vcp` backend — must be resolved before any CAT command testing, since a misclaimed interface makes CAT unreliable in ways that look like protocol bugs.

---

### Pitfall 3: Two class-driver clients (CDC-ACM + usb_host_vcp) or duplicate CDC install left active for FTX-1 profile

**What goes wrong:**
The USB Host Library does support multiple simultaneous class-driver clients (confirmed in Espressif docs), but this project currently assumes exactly one CAT-transport driver is installed per profile. If FTX-1 support is added by leaving the existing `cdc_acm_host_install()` call unconditional (e.g. only half-gating it) while also installing `usb_host_vcp`, both drivers compete to claim the same device/interface, or the CDC-ACM driver install consumes client/channel resources for a chip it will never successfully open, silently subtracting headroom from the VCP driver.

**Why it happens:**
Straightforward copy-paste: the QMX code path installs CDC-ACM unconditionally today only because there was one profile that needed it. Adding a second CAT-transport type is easy to implement additively (install both "just in case") rather than making driver install strictly profile-exclusive.

**How to avoid:**
Make CAT-transport driver install strictly profile-scoped: `UAC_PROFILE_QMX` installs `cdc_acm_host` only; `UAC_PROFILE_FTX1` installs `usb_host_vcp` only. Mirror the existing `if (s_profile == UAC_PROFILE_QMX) { ... } else { ESP_LOGI(TAG, "CDC-ACM driver skipped profile=%s", ...); }` pattern exactly, and add the same skip-logging for the new driver so the log makes profile exclusivity visible/testable.

**Warning signs:**
Boot log shows both "CDC-ACM driver installed" and "VCP driver installed" for an FTX-1 session — should never both appear.

**Phase to address:**
Same phase as Pitfall 2 (FTX-1 USB backend bring-up).

---

### Pitfall 4: PTT race — audio streaming starts before the radio has actually keyed, or `TX0;` unkeys before the last symbol finishes

**What goes wrong:**
Two related races. (1) On TX start: the existing QMX pattern (`qmx_begin_tx`) sends `MD6;`/`TX;` and returns as soon as the blocking CDC write completes (fixed 200ms timeout, no response parsing) — audio/DDS start is triggered by the caller immediately after `begin_tx` returns, with no verification the radio's PA has actually keyed. Over a CP210x/UAC-based FTX-1 link (higher and less predictable latency than direct USB CDC on QMX — an added USB-serial bridge chip plus the FTX-1's own PTT-to-RF turn-on delay), the first FT8/FT4 tone symbol(s) may be transmitted into an unkeyed or still-settling PA, corrupting the first symbol and desyncing the very tight FT8 15-second slot timing. (2) On TX end: `uac_tx_end()` already handles this correctly for QMX via a `vTaskDelay(pdMS_TO_TICKS(20))` drain before `dds_cpfsk_end()`/`uac_host_device_suspend()` — but if the FTX-1 backend's `end_tx()` sends `TX0;` *before* the audio pipeline has actually drained the last symbols (e.g., if it's wired to fire immediately rather than reusing the drain-then-unkey pattern), the radio unkeys mid-symbol, clipping the final characters of the last TX slot.

**Why it happens:**
Kenwood-style ASCII CAT is fire-and-forget in this codebase (no ack/response parsing) — success is assumed once the write syscall returns, not once the radio confirms. This is fine when PTT and audio share one low-latency USB link with known settle time (QMX's own DDS on the same interface), but the FTX-1's PTT command travels over a different transport (VCP/CP210x) than its audio (UAC), so their relative timing must be explicitly guaranteed rather than assumed to be simultaneous.

**How to avoid:**
Reuse the QMX end-of-TX drain pattern verbatim for FTX-1 (`begin_tx` and `set_tone_hz` must complete before `uac_tx_start_writer()` is invoked; `end_tx` must wait for the writer to finish its last buffer before sending `TX0;`). Additionally, measure the FTX-1's actual PTT-to-RF-out delay on real hardware and, if non-trivial, insert a short deliberate pre-roll (silence or first-tone-hold) between `TX1;` completion and the first live symbol, rather than assuming zero latency. Do not assume CP210x round-trip latency matches the QMX's direct CDC-ACM latency — these are different USB device classes with different driver-level buffering.

**Warning signs:**
Intermittent garbled decodes on the *other* station's end of the very first FT8/FT4 transmission after PTT engages, but not on retransmissions; or truncated/garbled final characters of TX messages specifically (last-symbol clipping) — both are classic PTT-sequencing symptoms, not encoding bugs.

**Phase to address:**
Phase implementing FTX-1 TX (`begin_tx`/`set_tone_hz`/`end_tx`) — needs explicit hardware validation with the user's physical FTX-1, not just protocol-level review.

---

### Pitfall 5: CP210x driver auto-asserts RTS/DTR on port open, causing a spurious PTT keyup independent of CAT

**What goes wrong:**
PROJECT.md explicitly notes: `RPTT SELECT` menu item must stay `OFF` so RTS/DTR hardware PTT doesn't conflict with CAT-triggered `TX1;`/`TX0;`. Many USB-serial VCP driver stacks (CP210x included) assert DTR (and sometimes RTS) by default immediately upon opening the port, before any application-level control lines are explicitly set. If `usb_host_vcp`'s CP210x backend does this on `open()`, and the FTX-1's `RPTT SELECT` is misconfigured (or a user's radio ships with a different default), the radio could key on device attach/firmware boot — before any CAT command is sent — independent of whether the firmware's CAT logic is correct.

**Why it happens:**
Hardware/PTT-line behavior lives in the vendor driver layer (`usb_host_vcp`'s CP210x sub-driver), invisible from this project's CAT command code — it's easy to test CAT logic purely at the protocol level and miss that the transport layer itself can toggle physical control lines.

**How to avoid:**
On FTX-1 CP210x port open, explicitly deassert RTS and DTR (if `usb_host_vcp`'s API exposes line control) before issuing any CAT commands, regardless of `RPTT SELECT` menu state — treat it as defense in depth, not a documented-menu-setting substitute. Confirm on real hardware that no keyup occurs at attach/boot before CAT is ever sent.

**Warning signs:**
Radio keys briefly on USB attach/firmware boot/reconnect, with no corresponding `TX1;` in logs.

**Phase to address:**
Phase adding the FTX-1 CP210x/VCP backend — verify during initial hardware bring-up, before autoseq/TX logic is layered on top.

---

### Pitfall 6: Kenwood-ASCII CAT string formatting bugs — wrong digit count copied from a different radio's command family

**What goes wrong:**
`radio_control_qmx.cpp`'s `qmx_set_tone_hz` documents three real formatting bugs that were fixed (`lrintf` vs `floorf` producing negative fractions, `%02d` overflow to 3 digits at `frac=100`, unclamped `ta_int` range) — all silent-rejection failures where the radio simply ignores a malformed command with no error surfaced to the firmware. The FTX-1's own command set has different field widths than QMX's (`FA` is **9 digits** per PROJECT.md, vs QMX's `%011d` 11-digit `FA`; `PC` is a **3-digit** field prefixed with a sub-index, `PC1<3-digit W>;`, not a bare `PC<field>;`). A new `radio_control_ftx1.cpp` written by adapting `radio_control_qmx.cpp` as a template is at high risk of carrying over QMX's digit-width constants (`%011d` instead of `%09d`) or QMX's field naming (`PC` instead of `PC1`) without updating them for the FTX-1's documented format, producing commands that are syntactically CAT-like but wrong-length/wrong-field and silently rejected.

**Why it happens:**
Kenwood-style CAT protocols share the `<CMD><fixed-width-digits>;` grammar across many radios but differ in field widths and prefixes per-radio-model; this codebase's fire-and-forget send pattern (no response/ack parsing) means a malformed command produces no crash and no log error — it just doesn't do anything, which is easy to miss in bench testing if the operator doesn't check the radio's actual displayed frequency/mode/power.

**How to avoid:**
Write the FTX-1 format strings directly from the CAT Operation Reference Manual field widths (9-digit `FA`, `PC1` + 3-digit, fixed literal `MD0C;`), not by search-and-replace of QMX's strings. Add a boundary-value check comment/test for each field (e.g., `470000000` is exactly 9 digits — verify `snprintf` with `%09d` doesn't silently truncate or misalign at the upper bound; `010` W is exactly 3 digits at the top of the 005-010 range). Where a `tests/tx_e2e/`-style unit test already exists for QMX's `TA` formatting bug class, add an analogous one for FTX-1's `FA`/`PC1`/`MD` strings, since these are the same class of bug that has already bitten this codebase once.

**Warning signs:**
Radio's on-screen frequency/mode/power doesn't match what the firmware just "set" via CAT, with no error returned — must be caught by watching the physical radio display during bring-up, not by log inspection alone (fire-and-forget sends look identical whether accepted or rejected).

**Phase to address:**
Phase implementing FTX-1 `sync_frequency_mode`/`set_tune` CAT commands — verify each field against the manual's exact width/range table, and validate against real hardware display, not just protocol correctness.

---

### Pitfall 7: FTX-1's bidirectional USB Audio descriptor assumed identical to QMX's (synchronous, fixed 24-bit/48kHz/stereo)

**What goes wrong:**
`stream_uac.cpp`'s QMX profile hardcodes a single candidate format (`UAC_CHANNELS`/`UAC_BIT_RESOLUTION` at `UAC_SAMPLE_RATE`) with no negotiation loop, and assumes a synchronous isochronous endpoint (no feedback/adaptive-clock handling anywhere in the pump/read code). PROJECT.md itself flags this as unconfirmed: "FTX-1 needs bidirectional... but with unknown/different USB Audio descriptor specifics." If the FTX-1 uses adaptive or asynchronous UAC sync mode (common on more modern USB audio interfaces) rather than QMX's simple synchronous mode, the existing fixed-rate DDS pump (`spk_writer_task`, driven purely by wall-clock `vTaskDelay` pacing rather than a feedback endpoint) will drift against the FTX-1's actual DAC clock over a multi-symbol FT8 transmission, causing audible pitch drift or underrun/overrun at the speaker endpoint.

**Why it happens:**
The existing pump design was built and tuned against one specific radio's descriptor (QMX) and never needed feedback-endpoint support because QMX doesn't require it. Assuming a new radio "just works" the same way without probing its actual descriptor is a natural but unverified assumption baked into the requirements themselves.

**How to avoid:**
Treat the FTX-1 UAC format/sync-mode as fully unknown until probed on real hardware (as PROJECT.md already plans) — dump the FTX-1's UAC descriptors (channel count, bit depth, sync type) on first enumeration before hardcoding a profile, using the same `uac_host_printf_device_param`/descriptor-print pattern already used for QMX. If the FTX-1 turns out to use adaptive/async sync, budget explicit design time for feedback-endpoint or rate-matching logic — do not assume the existing synchronous-mode pump code will "just work" unmodified.

**Warning signs:**
Audible pitch/frequency drift in transmitted tones over a long TX slot, or RX decode SNR/timing degradation that worsens the longer a transmission runs (classic symptom of clock-rate mismatch, distinct from a one-shot formatting bug).

**Phase to address:**
Phase doing FTX-1 audio format negotiation/bring-up — should be an explicit hardware-validation checkpoint before autoseq/TX integration, given PROJECT.md already flags this as an open unknown.

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|-----------------|-----------------|
| Fire-and-forget CAT sends with no response/ack parsing (existing QMX/KH1 pattern, likely to be copied for FTX-1) | Simple, fast to implement, matches existing backends | Formatting bugs and radio-side rejections are invisible except by watching the physical display; no way to detect a desynced radio programmatically | Acceptable for MVP bring-up with a human watching the radio; not acceptable once autoseq relies on FTX-1 unattended for multi-hour sessions — add minimal response validation before then |
| Hardcoding FTX-1's UAC format as a fixed candidate (copy QMX's single-candidate approach) rather than a negotiation loop | Faster to bring up if the descriptor happens to match QMX | Breaks silently (falls to "Format not supported") the moment real hardware differs even slightly from assumption | Only acceptable after the format has been confirmed once against real hardware; do not ship with an assumed format before probing |
| Leaving CDC-ACM driver install/scan logic unconditionally active while adding VCP for FTX-1 | Less code to write/gate | Silent interface misclaim or wasted channel/FIFO budget (Pitfalls 2 & 3) | Never acceptable — must be profile-gated from the first commit that adds FTX-1 |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|-----------------|-------------------|
| Espressif `usb_host_vcp` (CP210x) | Assuming it auto-coexists with `cdc_acm_host` driver installed for a different profile without explicit profile-gating | Install exactly one CAT-transport driver per active profile (`cdc_acm_host` for QMX/QDX, `usb_host_vcp` for FTX-1); never both |
| CP210x line control (RTS/DTR) | Assuming the port opens with lines deasserted by default | Explicitly deassert RTS/DTR after opening the FTX-1 CAT port, before any CAT traffic, regardless of `RPTT SELECT` menu setting |
| FTX-1 Kenwood-ASCII CAT | Copy-pasting QMX's field widths (`%011d` for `FA`, bare `PC`) instead of the FTX-1 manual's actual widths (9-digit `FA`, `PC1`+3-digit) | Derive every format string directly from the FTX-1 CAT Operation Reference Manual field-width table; add a boundary-value smoke test per field |
| FTX-1 bidirectional UAC | Assuming synchronous sync mode and fixed 24-bit/48k/stereo like QMX without probing | Dump the FTX-1's actual UAC descriptors on first enumeration; add adaptive/async handling only if the descriptor requires it |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|-----------------|
| FIFO/channel budget tuned only for 2-client (UAC+CDC) topology | Device-open failures only when both audio directions and CAT are simultaneously active | Derive a distinct FIFO partition for the FTX-1 profile from its actual endpoint MPS values, don't reuse QMX's 91/18/91 split blindly | Immediately upon adding a third client (VCP) if FIFO lines are already at or near the 200-line ESP32-S3 ceiling |
| DMA-capable static buffers (`DMA_ATTR uint8_t s_usb_buffer[...]`) sized only for the QMX profile's transfer size | New profile's audio transfer size (unknown until probed) may not evenly divide the existing 2304-byte buffer, silently truncating/misaligning reads | Recompute `UAC_READ_BUFFER_SIZE` as a multiple of the FTX-1's actual per-ms USB transfer size, not an assumed carryover from QMX's 288-byte/48kHz/24-bit/stereo figure | As soon as the FTX-1's audio descriptor differs from QMX's assumed 288 B/ms transfer size |
| Task priority ordering (`UAC_STREAM_TASK_PRIORITY` below `USB_HOST_TASK_PRIORITY` so CDC CAT TX to QMX is never preempted) | FTX-1's CAT commands (over VCP, a different driver/task) delayed behind audio pipeline work if the new VCP driver's internal task isn't given equivalent priority discipline | Apply the same "CAT transport task priority ≥ audio stream task priority" rule to whatever task `usb_host_vcp` creates internally for the FTX-1 | Under load, specifically during simultaneous TX audio pump + PTT command, exactly the scenario Pitfall 4 describes |

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| No validation of CAT responses/echoes from FTX-1 (if `usb_host_vcp` surfaces RX data at all, unlike the current CDC CAT which is TX-only) | A misbehaving/spoofed USB device (or cable-induced corruption) could inject unexpected data that's parsed without bounds checking, echoing the codebase's existing `sscanf`-without-validation pattern noted in CONCERNS.md | If FTX-1 backend ever reads CAT responses (unlike current QMX fire-and-forget), apply the same input-length/range validation discipline CONCERNS.md already recommends project-wide — don't add a second unvalidated parsing path |

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-------------------|
| No feedback when FTX-1 CAT command is silently rejected (Pitfall 6 class of bug) | User sees TX appear to run (audio streaming) but the radio never actually transmits or transmits on the wrong frequency/power — confusing, hard to diagnose remotely | Surface a lightweight "radio state" indicator on Cardputer UI (last confirmed frequency/mode reported by radio if `usb_host_vcp` supports RX, or at minimum a "CAT last-sent" timestamp) so users have something to check besides staring at the FTX-1's own display |
| PTT keys audio before RF (Pitfall 4) with no operator-visible warning | First TX of a session (or every TX, if unaddressed) is garbled on the other station's end with no local indication anything is wrong | Validate PTT-to-RF timing once on real hardware and bake in whatever pre-roll/delay is needed; don't leave this as a "works on my bench" assumption |

## "Looks Done But Isn't" Checklist

- [ ] **FTX-1 CAT bring-up:** Often verified only with `FA`/`MD`/`TX1` commands sent and no crash observed — verify by watching the FTX-1's own display update to the correct frequency/mode/PTT state, not just absence of firmware errors
- [ ] **PTT sequencing:** Often tested only in a quiet bench setup with a dummy load, where garbled first-symbol clipping isn't audible — verify with an actual decode on a second radio/software receiver to confirm no clipped first or last symbol
- [ ] **UAC format negotiation:** Often "works" on the specific hardware unit tested once and assumed general — verify against the FTX-1's actual descriptor dump (not just successful `uac_host_device_start`), and re-test after any firmware update to the radio itself (Yaesu firmware updates can change USB descriptor behavior)
- [ ] **CP210x line-control defaults:** Often untested at cold boot / fresh USB attach — verify no spurious keyup occurs on device plug-in before any CAT command is sent, across multiple power-cycle tests
- [ ] **Frequency range boundaries:** `FA` command's 9-digit field is often tested only mid-band — verify at both the low (`000030000`) and high (`470000000`) documented boundaries, since off-by-one digit-count bugs (Pitfall 6) surface exactly at field-width edges
- [ ] **FIFO/channel budget:** Often validated only with audio streaming alone or CAT alone — verify explicitly with both directions of audio *and* CAT active simultaneously under sustained load (a full multi-slot FT8 session), since exhaustion (Pitfall 1) only manifests under combined load

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|-----------------|
| FIFO/channel exhaustion (Pitfall 1) | MEDIUM | Re-derive FIFO partition from actual FTX-1 descriptor MPS values; re-test with both audio directions + CAT active; may require trimming buffer sizes elsewhere (e.g., `UAC_READ_BUFFER_SIZE`) to free DMA-capable BSS headroom |
| CDC-ACM/VCP interface misclaim (Pitfalls 2 & 3) | LOW | Add explicit profile gating around driver install and the CDC scan/open path; this is a code-structure fix, not a hardware or protocol issue |
| PTT/audio race (Pitfall 4) | MEDIUM | Add explicit pre-roll delay or drain-before-unkey sequencing (mirrors existing QMX `uac_tx_end` pattern); requires real-hardware timing measurement to calibrate correctly, not a guess |
| CAT formatting bug (Pitfall 6) | LOW | Isolated to a single format string per field; fix is a one-line `snprintf` correction once the manual's exact width is confirmed against real hardware display feedback |
| UAC descriptor mismatch (Pitfall 7) | HIGH | May require adding feedback-endpoint/adaptive-sync handling not currently present anywhere in `stream_uac.cpp` — a genuinely new capability, not a tuning fix, if the FTX-1 turns out to need async sync mode |

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|-------------------|----------------|
| FIFO/channel exhaustion (1) | FTX-1 USB bring-up / enumeration phase | Both audio directions + CAT active simultaneously for a sustained session without device-open failures |
| CDC-ACM/VCP interface misclaim (2) | FTX-1 USB bring-up / enumeration phase | Boot log shows only the VCP driver installed for FTX-1 profile, never CDC-ACM; CAT commands reach the correct interface |
| Dual-driver install (3) | FTX-1 USB bring-up / enumeration phase | Same as above — log audit confirms strict profile exclusivity |
| PTT/audio race (4) | FTX-1 TX implementation phase | Real-hardware decode test on a second receiver shows no clipped first/last symbol across multiple TX cycles |
| CP210x RTS/DTR auto-assert (5) | FTX-1 USB bring-up / enumeration phase | No spurious PTT keyup observed across multiple cold-boot/attach cycles, before any CAT command sent |
| CAT formatting bugs (6) | FTX-1 CAT command implementation phase | Radio's own display confirms correct frequency/mode/power at both boundary and mid-range values for every command |
| UAC descriptor mismatch (7) | FTX-1 audio format negotiation phase | Descriptor dump confirms sync mode/format before hardcoding a profile; no audible pitch drift over a full-length TX slot |

## Sources

- [espressif/esp-idf cdc_acm_vcp example README](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/host/cdc/cdc_acm_vcp/README.md) — HIGH confidence, official Espressif example demonstrating CDC-ACM extended for CP210x/FTDI/CH34x VCP devices
- [ESP-IDF USB Host Programming Guide, ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html) — HIGH confidence, official docs on multi-client support and channel limitations
- [ESP-USB Programming Guide, ESP32-S3](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html) — HIGH confidence, official
- [espressif/esp-idf issue #14831 — Problem with several USB CDC ACM devices on a USB Hub](https://github.com/espressif/esp-idf/issues/14831) — MEDIUM confidence, community-reported real-world multi-CDC-device issue supporting Pitfall 1/3 concerns
- [espressif/esp-idf issue #12667 — handling 2 HID interfaces for the same device](https://github.com/espressif/esp-idf/issues/12667) — MEDIUM confidence, illustrates general class of "multiple interface claim" pitfalls on this platform
- Yaesu FTX-1 CAT Operation Reference Manual (2507/2508 series, yaesu.com) — HIGH confidence for command syntax as referenced in PROJECT.md; not independently re-verified against the PDF in this research pass, cross-check field widths directly against the manual before finalizing format strings
- This codebase: `main/stream_uac.cpp`, `main/radio_control_qmx.cpp`, `.planning/codebase/CONCERNS.md`, `.planning/PROJECT.md` — HIGH confidence, direct source inspection; existing documented bug-fix comments (TA command) are the primary evidence base for Pitfall 6's pattern

---
*Pitfalls research for: ESP32-S3 CAT+USB-audio radio integration (Yaesu FTX-1 milestone)*
*Researched: 2026-07-04*
