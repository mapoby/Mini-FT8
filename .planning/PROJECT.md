# Mini-FT8 — Yaesu FTX-1 Support

## What This Is

Mini-FT8 is an FT8/FT4 amateur radio transceiver application running on an M5Stack Cardputer ADV (ESP32-S3), currently supporting the QMX, QDX, and (Elecraft) KH1 as external radios via CAT control plus USB/serial audio. This milestone adds the Yaesu FTX-1 as a new supported radio, controlled entirely over a single USB connection that carries both CAT (frequency/mode/PTT/power) and audio (mic in / speaker out).

## Core Value

A user with a Yaesu FTX-1 can plug it into the Cardputer over USB and run full FT8/FT4 QSOs — RX decode, autoseq, and TX — exactly as they already can with QMX/QDX/KH1.

## Requirements

### Validated

- ✓ FT8/FT4 RX decode and TX via QMX/QDX (CAT over USB CDC-ACM + USB audio in/out) — existing
- ✓ FT8/FT4 RX decode and TX via Elecraft KH1 (CAT over inverted UART + USB mic audio) — existing
- ✓ Autoseq QSO state machine, ADIF/Cabrillo logging, GPS/RTC time sync — existing
- ✓ LVGL-based UI, waterfall display, keyboard-driven mode navigation — existing
- ✓ User can select "FTX-1" as the active radio (alongside QMX/QDX/KH1) in station configuration, with a stubbed `radio_control_ops_t` vtable mirroring the QMX/QDX/KH1 pattern — Phase 1 (Backend Vtable Plumbing), hardware-verified on the physical Cardputer
- ✓ Firmware establishes CAT control with the FTX-1 over its USB CP210x virtual COM port via the `espressif/usb_host_cp210x_vcp` backend, with RTS/DTR deasserted immediately after open and the CDC-ACM scan path hard-gated away from the vendor-class interface — Phase 2 (CP210x USB Bring-up & CAT Connection), hardware-verified on the physical FTX-1 (VID 0x10C4/PID 0xEA70/CAT-1=interface 0 confirmed) with no regression to QMX

### Active

- [ ] Firmware sets VFO-A frequency via CAT (`FA` command, 9-digit Hz, 000030000–470000000 range)
- [ ] Firmware sets VFO-A frequency via CAT (`FA` command, 9-digit Hz, 000030000–470000000 range)
- [ ] Firmware sets DATA-U operating mode via CAT (`MD0C;`) suitable for FT8/FT4
- [ ] Firmware keys/unkeys PTT via CAT (`TX1;` / `TX0;`) rather than RTS/DTR hardware toggling
- [ ] Firmware sets/reads TX power via CAT (`PC` command, field-head range 005–010 W)
- [ ] Firmware streams RX audio from the FTX-1's USB audio input (mic) into the existing FT8/FT4 decode pipeline
- [ ] Firmware streams TX (FSK tone) audio to the FTX-1's USB audio output (speaker) during transmit, mirroring the QMX DDS/UAC-OUT approach
- [ ] FT8 and FT4 both work end-to-end with the FTX-1 (parity with QMX/QDX/KH1)

### Out of Scope

- CAT-3 wired UART connection to the FTX-1 — explicitly USB-only per user requirement; the CAT-3 5V CMOS jack path (like the existing KH1 UART backend) is not used
- SPA-1 external amplifier power control (5–100 W range) — only the FTX-1 field head's own 5–10 W range is in scope; no confirmed external amp in use
- CAT-2 (Standard COM) port usage — PTT and CAT are combined over the single CAT-1 (Enhanced COM) port, mirroring QMX's single-port design; the second virtual COM port is not used

## Context

- Existing radio backends live behind a common `radio_control_ops_t` vtable (`main/radio_control_backend.h`): `ready`, `on_audio_start`, `sync_frequency_mode`, `begin_tx`, `set_tone_hz`, `end_tx`, `set_tune`, `set_time`. QMX (`main/radio_control_qmx.cpp`) and KH1 (`main/radio_control_kh1_cat.cpp`) are the closest analogs for a new `radio_control_ftx1.cpp`.
- USB audio is handled in `main/stream_uac.cpp` via a `uac_stream_profile_t` enum (currently `UAC_PROFILE_QMX` — hardcoded VID/PID, strict 24-bit/48kHz/stereo bidirectional — and `UAC_PROFILE_GENERIC_USB` — mic-only, format-negotiated). FTX-1 needs bidirectional (mic + speaker) like QMX but with unknown/different USB Audio descriptor specifics — will need runtime format negotiation similar to the existing generic-profile candidate loop, validated against real hardware.
- QMX's CAT port is genuine USB CDC-ACM (class 0x02), opened via ESP-IDF's `usb_host_cdc_acm` component with known VID 0x0483 / PID 0xA34C. The FTX-1's CAT port is a **Silicon Labs CP210x** USB-to-UART bridge — a vendor-specific (non-CDC-ACM) chip — so the existing CDC-ACM open/scan logic in `stream_uac.cpp` will not recognize it.
- Decision: bring in Espressif's `usb_host_cp210x_vcp` component (v2.2.0, plain-C, drop-in sibling of the existing `usb_host_cdc_acm` code already used for QMX — internally calls the same `cdc_acm_host_open()` and patches CP210x AN571 vendor requests over it) as a new dependency for FTX-1 CAT. This supersedes the initially-considered generic `usb_host_vcp` (C++-only, requires exceptions, incompatible with this codebase's plain-C style) — confirmed via research (`.planning/research/STACK.md`). No new dependency-version bump needed: it requires `usb_host_cdc_acm ^2.1.0`, already satisfied by the project's pinned `^2.2.0`.
- CAT command set is Kenwood-style ASCII with `;` terminators (same family as QMX/QDX), extracted from Yaesu's official FTX-1 CAT Operation Reference Manual (2508-C):
  - `FA<9-digit Hz>;` — set/read VFO-A frequency (000030000–470000000 Hz)
  - `MD0C;` — set MAIN-side mode to DATA-U
  - `TX1;` / `TX0;` — CAT-triggered PTT on/off (`RPTT SELECT` menu item must stay `OFF` so RTS/DTR hardware PTT doesn't conflict)
  - `PC1<3-digit W>;` — set/read TX power on the FTX-1 field head (005–010 W)
  - Serial params: CAT-1 (Enhanced COM) defaults to 38400 bps, 8 data bits, no parity, 1–2 stop bits, configurable via radio menu
- User has physical FTX-1 hardware available for testing (USB audio descriptor details, CP210x VID/PID, and end-to-end TX/RX will be validated against real hardware during implementation).
- Target platform constraints are unchanged: ESP32-S3 single-threaded three-event architecture (decode → autoseq → slot boundary), ~320 KB usable heap, existing FIFO/DMA tuning in `stream_uac.cpp` for simultaneous bidirectional USB audio.

## Constraints

- **Tech stack**: Must integrate with existing ESP-IDF v5.5.1 / FreeRTOS / ESP32-S3 architecture; no new RTOS or threading model
- **New dependency**: Espressif `usb_host_cp210x_vcp` component (plain-C, not the C++ `usb_host_vcp` generic service) required for CP210x CAT support — first non-CDC-ACM, non-UAC USB dependency in the project
- **Hardware validation**: USB Audio descriptor specifics (sample rate, bit depth, channel layout) for the FTX-1 are unconfirmed from documentation alone and must be validated against the physical radio
- **USB bus bandwidth**: Existing FIFO partitioning in `stream_uac.cpp` was hand-tuned for QMX's simultaneous bidirectional 24-bit/48kHz/stereo audio + CDC CAT; adding a CP210x VCP endpoint alongside UAC may require re-tuning

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Use Espressif's `usb_host_cp210x_vcp` component for CP210x CAT, not a custom driver, the generic C++ `usb_host_vcp`, or CAT-3 UART | Plain-C, drop-in sibling of the existing CDC-ACM code; the generic `usb_host_vcp` requires C++ exceptions, incompatible with this codebase's style; custom vendor-request driver is more maintenance burden; CAT-3 UART contradicts the USB-only requirement | Confirmed in Phase 2 — component fetched/compiled cleanly, hardware bring-up passed with no regression |
| Combine PTT + frequency/mode CAT on a single virtual COM port (CAT-1 only) | Mirrors QMX's single-port design; avoids needing the second CAT-2 port | — Pending |
| Support both FT8 and FT4 with FTX-1 | Matches feature parity with existing QMX/QDX/KH1 backends | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-07-06 after Phase 2 (CP210x USB Bring-up & CAT Connection) completion*
