---
created: 2026-07-08T16:30:00.000Z
title: WiFi connectivity for NTP sync and QRZ.com log upload
area: general
files: []
---

## Problem

Currently the Cardputer's clock is synced only via GPS, manual entry (with
±1s nudge), the new manual slot-boundary snap (`0` on STATUS), and RX-signal
median timing correction (see the self-sync logic in `decode_monitor_results()`,
`main/main.cpp` ~line 3107). QSO logs are local-only (ADIF files on internal
FATFS/SD, `[YYMMDD].adi`) with no automatic upload to an online logbook.

User wants:
1. WiFi connectivity so the device can NTP-sync its clock (alternative/
   supplement to GPS, useful when no GPS module is attached).
2. Automatic QSO log upload to QRZ.com's online logbook (QRZ Logbook API)
   after each completed contact.

## Solution (TBD — needs its own milestone/phase planning)

Not a quick task — this is a new capability requiring real architecture
decisions:

- **Feasibility check first**: this ESP32-S3 board already runs USB Host
  (CDC CAT + UAC bidirectional audio) simultaneously with the FT8 decode
  pipeline under a measured, tight budget — both heap (see 04-02's HEAP
  logging, `AUDIO_PIPE_*` markers showing free/min/largest at each pipeline
  stage) and USB HCD channels (the ESP32-S3's hardware-fixed 8-channel
  ceiling, hit and resolved via half-duplex mic/speaker swap — see
  `.planning/debug/resolved/ftx1-hcd-channel-exhaustion-halfduplex.md`).
  WiFi (`esp_wifi`) has its own significant heap/DMA footprint on ESP32-S3.
  Need to confirm WiFi + USB Host can coexist on this SoC without starving
  either subsystem, and what the actual free-heap margin looks like with
  WiFi's stack added on top of the existing audio+USB budget, before
  committing to implementation.
- New persistent config: WiFi SSID/password storage, QRZ API key storage
  (extend `Station.txt` / `storage_service`, following the existing
  `StationConfig`-style pattern).
- New UI: credential entry screens (SSID/password, QRZ API key) — likely a
  new `UIMode`, following existing STATUS/BAND view patterns.
- New background task: NTP client (lwIP SNTP, already vendored via ESP-IDF's
  `lwip` component per the tech stack) and QRZ Logbook API HTTP client
  (QRZ's API is a simple HTTP POST with URL-encoded ADIF payload + API key).
- Error handling: connectivity loss, WiFi credentials wrong/unavailable,
  QRZ API failures — must degrade gracefully to current fully-offline
  behavior (WiFi should be optional/best-effort, never block core FT8
  RX/TX/logging functionality).
- Where this fits: current milestone (v1.0) is FTX-1 radio support and just
  reached a fully working, hardware-verified QSO end-to-end. This WiFi/QRZ
  feature is unrelated to FTX-1 and should land in a future milestone, not
  be squeezed into the current one.
