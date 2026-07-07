# Project Research Summary

**Project:** Mini-FT8 — Yaesu FTX-1 radio backend integration
**Domain:** CAT-controlled USB transceiver integration for FT8/FT4 digital modes on ESP32-S3 (ESP-IDF USB host)
**Researched:** 2026-07-04
**Confidence:** MEDIUM-HIGH

## Executive Summary

This milestone adds the Yaesu FTX-1 as a fourth radio backend to Mini-FT8, alongside the existing QMX/QDX/KH1 backends. The FTX-1's single USB port exposes a composite device: a Silicon Labs CP2105 "Dual CP210x" USB-UART bridge (2 CDC ports — CAT-1 "Enhanced COM" in scope, CAT-2 "Standard COM" out of scope) plus USB Audio Class interfaces for bidirectional audio. Unlike QMX/QDX, which are genuine CDC-ACM class devices, the CP210x reports a vendor-specific interface class (0xFF), so it cannot be opened via the existing CDC-ACM scan path — it requires the official `espressif/usb_host_cp210x_vcp` component, a plain-C driver that patches CDC-ACM's vtable with AN571 vendor-request commands. Critically, research overturned an initial assumption in PROJECT.md: the C++ `usb_host_vcp` factory/service (which requires enabling C++ exceptions, incompatible with this codebase's plain `esp_err_t` style) is unnecessary — the project should depend only on `usb_host_cp210x_vcp`'s plain-C `cp210x_vcp_open()` function, which is a near drop-in sibling to the existing `cdc_acm_host_open()` calls already used for QMX.

Architecturally, this is additive, not a new subsystem: one new `radio_control_ftx1.cpp` (pure C CAT encoding, mirroring `radio_control_qmx.cpp`) plus extensions to the existing `stream_uac.cpp` orchestration file (new `UAC_PROFILE_FTX1` branch, reusing the same single USB-host task pair — never a second task pair, since the ESP32-S3 has exactly one USB-OTG peripheral). CAT command sending stays fire-and-forget (no response parsing), matching QMX/KH1 precedent, since none of the in-scope FTX-1 commands (`FA`, `MD0C`, `TX0/1`, `PC1`) require read-back.

The dominant risks are: (1) FIFO/USB-channel budget — the existing 200-line FIFO partition was hand-tuned for exactly two clients (bidirectional UAC + CDC-ACM bulk-OUT-only); adding CP210x's bulk-IN+OUT CAT traffic alongside bidirectional audio is a third client that may exceed the budget, and must be re-tuned once real FTX-1 endpoint sizes are known; (2) double-PTT from CP210x auto-asserting RTS/DTR at port open, which must be explicitly deasserted regardless of the radio's `RPTT SELECT` menu setting; (3) copy-paste CAT formatting bugs (FTX-1's `FA` is 9 digits vs QMX's 11; `PC1` is a different field than a bare `PC`) — this codebase has already been bitten by this exact bug class once (QMX's `TA` command); and (4) the FTX-1's actual USB Audio descriptor (sample rate, sync mode) is unconfirmed until hardware validation — the generic-negotiation profile, not QMX's hardcoded format, is the correct starting point. All of these require real-hardware validation; the user has a physical FTX-1 unit available.

## Key Findings

### Recommended Stack

Add `espressif/usb_host_cp210x_vcp@^2.2.0` to `main/idf_component.yml`. It depends on `espressif/usb_host_cdc_acm@^2.1.0`, already satisfied by the project's pinned `^2.2.0` — no version bump needed. This driver is a pure-C, single-file component that calls the same `cdc_acm_host_open()` API already used for QMX, then patches 4 vtable function pointers (line-coding, control-line, break) to use CP210x AN571 vendor requests instead of standard CDC class requests. All existing send/receive functions (`cdc_acm_host_data_tx_blocking`, `cdc_acm_host_line_coding_set`, etc.) work unchanged against the returned `cdc_acm_dev_hdl_t`.

**Core technologies:**
- `espressif/usb_host_cp210x_vcp@^2.2.0` — CP210x CAT driver — official, plain-C, drop-in sibling to existing CDC-ACM usage; no new API to learn
- `espressif/usb_host_cdc_acm@^2.2.0` (unchanged, already a dependency) — base CDC-ACM transport/vtable that CP210x patches
- ESP-IDF v5.5.1 (unchanged) — no USB-host API changes required

**Explicitly rejected:** `espressif/usb_host_vcp` (the generic C++ VCP factory/service). It requires `CONFIG_COMPILER_CXX_EXCEPTIONS` and throws `esp_err_t` from constructors — incompatible with this codebase's plain error-code idiom used everywhere else. Use `usb_host_cp210x_vcp`'s C function `cp210x_vcp_open()` directly with the known VID (`0x10C4`)/PID (`0xEA70`, CP2105)/interface (0 = CAT-1) instead of generic multi-chip auto-detection, since the FTX-1's chip is already known.

### Expected Features

**Must have (table stakes) — v1/MVP:**
- CP210x CAT connection established (prerequisite for all CAT commands)
- `FA<9-digit>;` frequency sync before RX/TX
- `MD0C;` DATA-U mode set once at sync time (not per-TX)
- `TX1;`/`TX0;` CAT-only PTT, with `RPTT SELECT`=OFF confirmed on hardware (avoids double-PTT)
- Bidirectional UAC audio (mic RX + speaker TX) via runtime format negotiation, not a hardcoded profile
- End-to-end FT8/FT4 QSO validated on real hardware

**Should have (differentiators), v1.x — deferred until MVP proven:**
- `PC1<3-digit>;` TX power read/set — requires a new `set_power` vtable hook (none of QMX/QDX/KH1 currently expose one) plus UI wiring
- Generic UAC negotiation becoming production-proven on a second real device (validates existing infrastructure, not FTX-1-specific)
- Defensive DATA-U re-assertion — only add if hardware testing actually reproduces the DATA-U→USB reversion quirk reported on related Yaesu rigs (unconfirmed on FTX-1)

**Defer (v2+, explicitly out of scope per PROJECT.md):**
- Split VFO (single-VFO model matches QMX/KH1; no confirmed FT8 need)
- SPA-1 external amplifier power control
- CAT-2 (Standard COM) second port usage
- CAT-3 wired UART fallback

### Architecture Approach

FTX-1 is a fourth radio backend added through the existing seams, not a new layer: `radio_control_ftx1.cpp` (new, pure C, mirrors `radio_control_qmx.cpp` structurally) implements `radio_control_ops_t` and calls through new `cat_vcp_ready()`/`cat_vcp_send()` adapter functions that live inside `stream_uac.cpp` — exactly mirroring the existing `cat_cdc_ready()`/`cat_cdc_send()` pair for QMX. All CP210x/USB-host types stay contained inside `stream_uac.cpp`; they must never leak into `radio_control_backend.h` or `main.cpp`.

**Major components:**
1. `radio_control_ftx1.cpp` (new) — encodes/sends Kenwood-ASCII CAT commands (`FA`, `MD0C`, `TX0/1`, `PC1`), exposes the ops vtable; no USB types visible
2. `stream_uac.cpp` (extended) — new `UAC_PROFILE_FTX1` branch: installs `cdc_acm_host` (still required — CP210x driver extends it, does not replace it) plus the CP210x open path; owns bidirectional UAC negotiation reusing QMX's speaker pre-open trick combined with GENERIC_USB's candidate-scanning loop
3. `usb_host_cp210x_vcp` + `usb_host_cdc_acm` (external, ESP-IDF Component Registry) — recognize and open the CP210x chip
4. Station config / `g_radio` dispatch (existing, extended) — add `"ftx1"` as a selectable radio type, one new case

Build order: (1) vtable plumbing, no hardware needed; (2) CP210x dependency + driver-install branch, hardware needed to confirm VID/PID; (3) CAT command wiring; (4) bidirectional UAC negotiation (parallel with 3); (5) end-to-end integration/parity testing. Steps 3 and 4 can proceed in parallel once step 2 lands.

### Critical Pitfalls

1. **FIFO/channel budget exceeded by a third USB client** — the existing 200-line FIFO partition is hand-tuned for exactly two clients (UAC + CDC-ACM bulk-OUT); adding CP210x bulk-IN+OUT alongside bidirectional audio may exceed it. Avoid by deriving a distinct `UAC_PROFILE_FTX1` FIFO branch from actual measured endpoint sizes, not copy-pasting QMX's numbers.
2. **CDC-ACM interface-hint scan misclaims the CP210x vendor interface** — the existing `USB_CLASS_COMM` scan won't recognize CP210x's vendor-class (0xFF) interfaces but the blind interface-scan fallback may still probe them. Avoid by gating all CDC-ACM install/scan logic strictly behind `s_profile == UAC_PROFILE_QMX`, and using the CP210x driver's VID/PID-based open for FTX-1.
3. **Dual class-driver install left active** — leaving CDC-ACM install unconditional while adding the CP210x path risks two drivers competing for the same device/resources. Avoid with strict profile-exclusive driver install, mirrored logging for both paths.
4. **PTT race — audio starts before radio keys, or unkeys before last symbol finishes** — CP210x/UAC path has different, higher latency than QMX's single CDC-ACM link. Avoid by reusing QMX's drain-before-unkey pattern verbatim and measuring actual PTT-to-RF delay on hardware, adding pre-roll if needed.
5. **CP210x auto-asserts RTS/DTR on port open** — can cause spurious keyup independent of CAT, defeating the `RPTT SELECT`=OFF precondition. Avoid by explicitly deasserting RTS/DTR immediately after port open, as defense in depth.
6. **Kenwood-ASCII CAT formatting bugs from copy-pasting QMX's field widths** — FTX-1's `FA` is 9 digits (QMX is 11), `PC1` is a different field than a bare `PC`; fire-and-forget sends make malformed commands fail silently. Avoid by deriving format strings directly from the FTX-1 CAT manual, not search-and-replace of QMX's code, and validating against the radio's physical display.
7. **FTX-1's UAC descriptor assumed identical to QMX's** (fixed 24-bit/48kHz/stereo, synchronous) — unconfirmed; if adaptive/async sync is actually required, the existing wall-clock-paced pump will drift. Avoid by dumping actual descriptors on first enumeration before hardcoding a profile.

## Implications for Roadmap

Based on research, suggested phase structure:

### Phase 1: Backend Vtable Plumbing
**Rationale:** No hardware needed; strict prerequisite for everything else being reachable via the existing radio-selection path; fully code-reviewable in isolation.
**Delivers:** `radio_control_ftx1_get_ops()` declared and stubbed, `"ftx1"` wired into station config / `g_radio` dispatch in `main.cpp`.
**Addresses:** Enables the FTX-1 backend to be selected at all (prerequisite feature).
**Avoids:** N/A (no USB work yet, no pitfalls in scope).

### Phase 2: CP210x USB Bring-up and Enumeration
**Rationale:** Highest-risk, highest-uncertainty step (new non-CDC-ACM dependency); must be validated against real hardware before CAT logic is layered on top.
**Delivers:** `espressif/usb_host_cp210x_vcp` dependency added; `UAC_PROFILE_FTX1` enum value; CDC-ACM install/scan strictly profile-gated; CP210x device opens reliably via known VID/PID/interface; `cat_vcp_ready()`/`cat_vcp_send()` adapters implemented; RTS/DTR explicitly deasserted at open.
**Uses:** `espressif/usb_host_cp210x_vcp@^2.2.0` from STACK.md.
**Implements:** `stream_uac.cpp` driver-install branch and CAT adapter shim from ARCHITECTURE.md.
**Avoids:** Pitfalls 1 (FIFO budget), 2 (interface misclaim), 3 (dual driver install), 5 (RTS/DTR auto-assert).

### Phase 3: CAT Command Implementation
**Rationale:** Depends on Phase 1 (vtable) and Phase 2 (working CP210x channel); testable against real hardware once the VCP channel opens reliably.
**Delivers:** `radio_control_ftx1.cpp` real command bodies — `FA` (9-digit), `MD0C`, `TX0/TX1`, with format strings derived directly from the FTX-1 CAT manual (not copy-pasted from QMX).
**Addresses:** Table-stakes features — frequency sync, mode set, CAT PTT.
**Avoids:** Pitfall 6 (formatting bugs), Pitfall 4 (PTT sequencing — reuse QMX drain-then-unkey pattern).

### Phase 4: Bidirectional UAC Audio Negotiation
**Rationale:** Independent of Phase 3 once Phase 2's profile scaffolding lands; can proceed in parallel with Phase 3.
**Delivers:** Mic candidate-list and speaker pre-open guards extended to include `UAC_PROFILE_FTX1`; real descriptor (sample rate/bit depth/channel count/sync mode) probed and validated against hardware; FIFO partition re-tuned if needed.
**Addresses:** Bidirectional USB audio table-stakes feature.
**Avoids:** Pitfall 7 (UAC descriptor mismatch), contributes to resolving Pitfall 1 (FIFO budget) with real numbers.

### Phase 5: End-to-End Integration and Parity Testing
**Rationale:** Requires Phases 1-4 complete; this is where FIFO/timing issues from combining CP210x bulk CAT with bidirectional audio (a first for this codebase) will surface.
**Delivers:** Full FT8/FT4 RX decode + autoseq + TX through the FTX-1, validated end-to-end on real hardware; acceptance criterion from PROJECT.md.
**Addresses:** Core Value feature — end-to-end FT8 QSO parity with QMX/QDX/KH1.
**Avoids:** Confirms no regression on Pitfalls 1 and 4 under combined sustained load (per PITFALLS.md's "Looks Done But Isn't" checklist).

### Phase 6 (optional, v1.x): TX Power Control
**Rationale:** Purely additive; does not gate core FT8 function; natural second phase once P1 is proven reliable.
**Delivers:** New `set_power` hook added to `radio_control_ops_t` vtable, `PC1<3-digit>;` CAT command, station-configuration UI wiring to make it reachable.
**Addresses:** Differentiator feature (TX power read/set) — genuinely new capability, not present in QMX/QDX/KH1 today.
**Avoids:** N/A — should not be started before Phase 5 proves core reliability.

### Phase Ordering Rationale

- USB enumeration/driver bring-up (Phase 2) must precede CAT command work (Phase 3) and audio negotiation (Phase 4) because both depend on a reliably-opening CP210x channel; this is the single highest-uncertainty step and should be validated against the official ESP-IDF `cdc_acm_vcp` example early.
- CAT (Phase 3) and audio (Phase 4) can run in parallel once Phase 2 lands, since they are independent USB interfaces on the same composite device — no reason to serialize them.
- Power control (Phase 6) is explicitly deferred past end-to-end validation (Phase 5) because it requires a new vtable hook change (touches the shared backend header) and is not required for FT8/FT4 parity — bundling it earlier risks destabilizing the core path for a non-blocking feature.
- This ordering directly avoids Pitfalls 1-3 and 5 by resolving USB-transport-layer risk before any CAT-protocol-layer work is built on top of an unverified channel, and avoids Pitfall 4 by deferring PTT-timing validation until real commands exist to test against real hardware.

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 2 (CP210x USB bring-up):** New non-CDC-ACM dependency, first VID/PID-based (rather than class-based) device open in this codebase; official ESP-IDF `cdc_acm_vcp` example should be directly reviewed before implementation (STACK.md/ARCHITECTURE.md flagged this example as not fully fetched during research).
- **Phase 4 (UAC audio negotiation):** FTX-1's actual USB Audio descriptor (sample rate, sync mode, bit depth) is entirely unconfirmed until hardware is probed; if adaptive/async sync is required, this becomes a genuinely new capability (feedback-endpoint handling), not a tuning fix.

Phases with standard patterns (skip research-phase):
- **Phase 1 (vtable plumbing):** Directly mirrors 3 existing backends (QMX/QDX/KH1); no new pattern.
- **Phase 3 (CAT commands):** Kenwood-ASCII fire-and-forget pattern already established by QMX/KH1; only the field widths/values differ, which come from the FTX-1 CAT manual, not new architecture.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Component source, headers, and changelogs read directly from the `espressif/esp-usb` GitHub repo and Component Registry; MEDIUM only for FTX-1-specific USB descriptor details (corroborated by two independent web sources, not yet hardware-validated) |
| Features | MEDIUM | Hamlib/WSJT-X community CAT/PTT behavior verified via multiple sources; exact FTX-1 firmware quirks (e.g., DATA-U reversion) are extrapolated from related Yaesu models (FT-991A/FT-710/FTDX-10), since the FTX-1 itself is new |
| Architecture | MEDIUM (integration pattern HIGH) | The backend-vtable/CAT integration pattern is HIGH confidence — directly modeled on existing QMX code; the CP210x driver-install sequencing (extends vs. replaces CDC-ACM) is MEDIUM, confirmed from official docs/headers but not full source trace, and should be hardware/example-verified |
| Pitfalls | MEDIUM | Grounded in this codebase's own hard-won USB-host tuning history plus verified ESP-IDF/esp-usb documentation and Yaesu CAT manual excerpts; audio-descriptor specifics and real-device PTT latency are explicitly unconfirmed without hardware |

**Overall confidence:** MEDIUM-HIGH

### Gaps to Address

- **FTX-1 exact VID/PID and CAT-1 interface index:** Confirmed via two independent web sources (VID `0x10C4`, PID `0xEA70` CP2105) but not yet hardware-validated. Use `CP210X_PID_AUTO` as a safe fallback if the hardcoded PID doesn't match on first connection.
- **FTX-1 USB Audio descriptor (sample rate, bit depth, sync mode):** Entirely unknown until probed on real hardware; do not hardcode a format — use the generic negotiation loop and dump descriptors on first enumeration.
- **FIFO/channel budget for 3-client topology (audio + CP210x bulk CAT):** Cannot be derived analytically with confidence per STACK.md — flagged as needing empirical hardware validation, not just descriptor math.
- **DATA-U → USB mode reversion quirk:** Reported in community forums for related Yaesu rigs (FTDX-10), single low-confidence source, unconfirmed on FTX-1 specifically. Do not pre-build a defensive workaround; only add if real hardware testing reproduces it.
- **CP210x RTS/DTR default behavior on port open:** Assumed to auto-assert based on general VCP driver behavior; must be confirmed against real hardware across multiple cold-boot/attach cycles.
- **PTT-to-RF turn-on delay on the FTX-1:** Unmeasured; needed to calibrate any pre-roll/drain timing for Phase 3/5.

## Sources

### Primary (HIGH confidence)
- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_cp210x_vcp/usb_host_cp210x_vcp.c — full driver source
- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_cp210x_vcp/idf_component.yml and CHANGELOG.md — version/dependency data
- https://components.espressif.com/components/espressif/usb_host_cp210x_vcp — registry page
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html and https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html — official USB host programming guides
- Project source: `main/radio_control_qmx.cpp`, `main/radio_control_kh1_cat.cpp`, `main/stream_uac.cpp`, `main/radio_control_backend.h`, `.planning/PROJECT.md`, `.planning/codebase/CONCERNS.md`, `managed_components/espressif__usb_host_cdc_acm/` — direct read of existing patterns and current dependency pins

### Secondary (MEDIUM confidence)
- WebSearch: Yaesu FTX-1 USB CP2105 VID/PID/composite-descriptor sources (Windows Device Manager descriptions, corroborated by two independent sources)
- Yaesu FTX-1 CAT Operation Reference Manual (referenced in PROJECT.md) — command syntax, not independently re-verified against the PDF in this research pass
- FT-991/FT-710/FT-891/FTDX-10 community guides — Yaesu digital-mode menu conventions and CAT-PTT-vs-RTS/DTR best practice, from closely related but not identical models
- espressif/esp-idf issues #14831 and #12667 — community-reported multi-CDC-device / multi-interface claim issues supporting FIFO/channel exhaustion concerns

### Tertiary (LOW confidence)
- ftdx-10@groups.io forum thread on DATA-U→USB mode reversion — single source, could not fully fetch, treat as unconfirmed on FTX-1 until hardware-validated
- General ESP32 USB host channel/enumeration timing WebSearch results — not FTX-1-specific

---
*Research completed: 2026-07-04*
*Ready for roadmap: yes*
