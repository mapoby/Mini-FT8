<!-- generated-by: gsd-doc-writer -->
# Architecture

## System Overview

Mini-FT8 is an FT8/FT4 amateur radio transceiver application that runs standalone on an
M5Stack Cardputer ADV (ESP32-S3). It decodes FT8/FT4 signals from audio supplied by an
external radio, drives an automated QSO sequencer (autoseq) to complete contacts with
minimal operator input, and transmits replies back through the same radio. Radios are
attached over USB (audio class device, CDC-ACM, or CP210x virtual COM) or serial, and are
abstracted behind a common CAT control interface so QMX, QDX, KH1, and FTX-1 can be
swapped without touching the app logic.

The system is a single ESP-IDF firmware image (`idf.py build` → `MiniFT8_Merged_Auto.bin`)
with no external server component — it is a self-contained embedded application, not a
client/server system.

```text
┌─────────────────────────────────────────────────────────────────────┐
│                          ESP32-S3 (dual core)                        │
│                                                                        │
│  Core 0                                   Core 1                      │
│  ┌───────────────────────────┐            ┌──────────────────────┐   │
│  │ app_task_core0()           │            │ stream_uac_task /     │   │
│  │  (main.cpp)                │            │ mic_stream_task /     │   │
│  │  - keyboard/UI input        │            │ audio decode task     │   │
│  │  - check_slot_boundary()    │◄──callback─┤  (ft8_audio_pipeline, │   │
│  │  - decode_monitor_results() │            │   stream_uac.cpp,     │   │
│  │  - LVGL UI refresh          │            │   stream_mic.cpp)     │   │
│  └──────────┬──────────────────┘            └──────────┬────────────┘   │
│             │                                            │              │
│             ▼                                            ▼              │
│  ┌───────────────────┐   ┌──────────────┐   ┌──────────────────────┐  │
│  │ autoseq.cpp         │   │ core_api.cpp │   │ ft8_lib (decode/     │  │
│  │ (QSO state machine) │◄──┤ (snapshot +  │   │ encode, FFT)          │  │
│  └───────────┬──────────┘   │  callbacks)  │   └──────────────────────┘  │
│              │               └──────┬───────┘                            │
│              ▼                      ▼                                    │
│  ┌────────────────────┐   ┌──────────────────┐                          │
│  │ radio_control*.cpp  │   │ components/ui    │                          │
│  │ (CAT backends:       │   │ (LVGL screens)   │                          │
│  │  QMX/QDX/KH1/FTX-1)  │   └──────────────────┘                          │
│  └──────────┬──────────┘                                                  │
└─────────────┼───────────────────────────────────────────────────────────┘
              ▼
   USB Host (UAC / CDC-ACM / CP210x VCP) or UART  →  External radio
```

## Component Responsibilities

| Component | Responsibility | File(s) |
|-----------|----------------|---------|
| **Main App Loop** | Keyboard input, mode switching, slot boundary detection, UI refresh | `main/main.cpp:app_task_core0()` |
| **Autoseq Engine** | QSO state machine, TX queue management, message generation | `main/autoseq.cpp/h` |
| **Core API** | UI-agnostic functional core, snapshot accessors, change callbacks | `main/core_api.cpp/h` |
| **Audio Pipeline** | RX audio processing, FFT analysis, waterfall generation, FT8/FT4 decoding | `main/ft8_audio_pipeline.cpp/h`, `ft8_lib/` |
| **USB Audio Streaming** | UAC device negotiation, mic/speaker pipe management, FTX-1 half-duplex swap | `main/stream_uac.cpp/h` |
| **Mic Streaming** | KH1 built-in/analog mic capture path | `main/stream_mic.cpp/h` |
| **Audio Source Selection** | Chooses active audio backend (QMX UAC, generic USB UAC, KH1 mic, FTX-1 CP210x) | `main/audio_source.cpp/h` |
| **Radio Control** | CAT control (QMX, QDX, FTX-1) and KH1 serial interface, VFO sync, TX keying | `main/radio_control.cpp`, `main/radio_control_backend.h`, `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`, `main/radio_control_ftx1.cpp`, `main/radio_control_kh1_cat.cpp` |
| **GPS / RTC** | Date/time sync, grid location, external RTC support | `main/gps.cpp`, `components/external_rtc/` |
| **UI Rendering** | Screen layout, RX list display, waterfall, countdown bar | `components/ui/ui.cpp` |
| **Storage** | Configuration persistence, ADIF logging, SD card access | `components/storage_service/` |
| **FT8/FT4 Protocol Library** | Vendored encode/decode/FFT implementation (Karlis Goba's ft8_lib) | `components/ft8_lib/` |
| **Board Support** | Cardputer ADV hardware bring-up (display, keyboard, power) | `components/board_cardputer_adv/` |

## Pattern Overview

- **Two-core split:** The main application task (`app_task_core0`, keyboard input, mode
  switching, slot-boundary TX trigger, UI refresh) runs pinned to core 0. Audio streaming
  and decode tasks (`stream_uac_task`, `mic_stream_task`, `uac_lib_task`, `usb_lib_task`)
  run pinned to core 1, so USB/audio I/O never blocks UI responsiveness or vice versa.
- **Three-event ordering guarantee:** Event 2 (decode complete) ALWAYS happens before
  Event 3 (slot boundary check/TX trigger).
- **One-shot CQ/FreeText entries:** Added to the autoseq queue with a `TX_NONE` marker;
  transmitted once then evicted.
- **Snapshot + callback pattern:** `core_api` consumers pull snapshots via `core_get_*()`
  and register callbacks via `core_on_*_changed()`; no polling.
- **Radio backend abstraction:** Each supported radio implements a common
  `radio_control_ops_t` vtable (`main/radio_control_backend.h`); `radio_control.cpp`
  dispatches to the active backend by `radio_control_backend_t` (`RADIO_CONTROL_QMX`,
  `RADIO_CONTROL_KH1_CAT`, `RADIO_CONTROL_QDX`, `RADIO_CONTROL_FTX1`). `autoseq.cpp` never
  calls a radio-specific function directly; `main.cpp` mostly goes through the vtable too,
  but calls a handful of KH1-specific functions directly for diagnostics
  (`radio_control_kh1_set_enabled()`, `radio_control_kh1_is_enabled()`,
  `radio_control_kh1_diag_test()`), bypassing the abstraction for that one radio.
- **Half-duplex time-multiplexing (FTX-1 only):** The ESP32-S3's USB host controller has a
  measured hard 8-channel HCD pipe ceiling that cannot hold the mic, speaker, hub, and
  CP210x CAT device open simultaneously for the FTX-1's USB topology. `stream_uac.cpp`
  time-multiplexes the mic and speaker pipes via `uac_ftx1_prepare_tx()` /
  `uac_ftx1_prepare_rx()`, releasing one direction's pipe before claiming the other. This
  swap is scoped to `UAC_PROFILE_FTX1`; QMX/QDX/KH1 keep both pipes open permanently.
- **Display-first architecture:** UI changes are driven by dirty flags to reduce redraw
  overhead.

## Data Flow

### Primary Path: Receive, Decode, Reply

1. **Audio capture** — `stream_uac.cpp` (USB Audio Class radios: QMX, generic USB, FTX-1)
   or `stream_mic.cpp` (KH1 analog mic) captures incoming audio into DMA buffers on the
   core-1 audio task.
2. **Resample + decode** — `stream_uac.cpp`/`stream_mic.cpp` resample the captured audio to
   the 6 kHz FT8 processing rate (via `resample_init()`/`uac_pcm_to_ft8_samples()` in
   `resample.cpp`), and `ft8_audio_pipeline.cpp` feeds the resulting symbol buffers into
   `ft8_lib` (`components/ft8_lib/`) for FFT-based tone detection and message decode.
   Results are collected into a `monitor_t`.
3. **Decode monitor callback** — Once a slot's worth of symbols (79 for FT8) has been
   processed, `decode_monitor_results()` (`main/main.cpp`) parses newly decoded messages,
   updates the autoseq queue (`autoseq.cpp`), and (if beaconing) queues a new CQ entry.
4. **Slot boundary check** — On the core-0 main loop, `check_slot_boundary()`
   (`main/main.cpp`) detects the even/odd slot transition using RTC time and, if a QSO
   context has a message ready, triggers TX.
5. **TX generation and keying** — `autoseq.cpp` generates the next TX message
   (`generate_response()` / `set_state()`), and the active `radio_control_ops_t` backend's
   `begin_tx()` / `set_tone_hz()` / `end_tx()` are called to key the radio and drive tone
   output (via `dds_q15.cpp` DDS synthesis, streamed out through `stream_uac.cpp`'s
   speaker writer task or the KH1 CAT path).
6. **UI update** — `core_api.cpp` fires change callbacks (`core_on_rx_changed()`,
   `core_on_qso_changed()`); the UI (`components/ui/ui.cpp`) pulls a fresh snapshot and
   redraws the RX list, waterfall, and QSO state.

### Secondary Flow: User Manual QSO (Tap RX)

A user selects a decoded RX line in the UI, which calls `core_cmd_tap_rx()`
(`core_api.cpp`). This creates or advances a `QsoContext` in `autoseq.cpp` outside the
normal beacon/auto-CQ flow, following the same state machine and TX generation path as
above.

### Secondary Flow: Manual TX / FreeText

The user composes a free-text message via the UI; `core_api.cpp` queues it as a one-shot
entry with `TX_NONE`/`FREETEXT` marking so it transmits once on the next slot boundary and
is then evicted from the queue.

### Secondary Flow: Beacon CQ

When beacon mode is enabled (`CoreBeaconMode::EVEN`/`ODD`), `decode_monitor_results()`
re-adds a CQ entry to the autoseq queue at the end of each slot if no active QSO is in
progress, keeping a continuous CQ beacon running on the configured slot parity.

### Timing

- **RTC**: `rtc_now_ms()` provides monotonic millisecond time; compensated by `g_rtc_comp`
  ppm-like drift correction.
- **Slot Parity**: `(rtc_now_ms() / 7500) % 2` determines even (0) / odd (1) slot.
- **TX Countdown**: `ms_to_boundary = 7500 - (rtc_now_ms() % 7500)` drives the on-screen
  countdown timer.
- **Autoseq Queue**: Sorted by state priority (more advanced QSO states rank higher);
  evicted when `IDLE` or after an inactivity timeout.

## Key Abstractions

| Abstraction | Purpose | Location | Pattern |
|-------------|---------|----------|---------|
| `radio_control_ops_t` | Common CAT/PTT/tuning vtable implemented per supported radio | `main/radio_control_backend.h`; implementations in `radio_control_qmx.cpp`, `radio_control_qdx.cpp`, `radio_control_ftx1.cpp`, `radio_control_kh1_cat.cpp` | Struct-of-function-pointers dispatched by `radio_control.cpp:current_ops()` based on `radio_control_backend_t` |
| `QsoContext` | Represents one active or inactive QSO contact | `main/autoseq.cpp`, `main/autoseq.h` | State machine: `CALLING → REPLYING → REPORT → ROGER_REPORT → ROGERS → SIGNOFF → IDLE` |
| `RxDecodeEntry` / `UiRxLine` | Single decoded message for display and interaction | `components/ui/include/ui.h` | Lightweight, display-only snapshot; no state mutation until the user taps it |
| Core API (`core_get_*`, `core_cmd_*`, `core_on_*_changed`) | UI-agnostic command/query interface | `main/core_api.cpp/h` | "Notify + pull": register a callback, then pull a fresh snapshot when it fires |
| `g_protocol` (`ProtocolConfig*`) | Runtime selection of FT8 vs FT4 encoding/decoding | `main/protocol.h`, set in `main.cpp` | Global pointer chosen at boot from `Station.txt`; never changed mid-session |
| `audio_source_backend_t` | Selects the active audio capture/playback backend (QMX UAC, generic USB UAC, KH1 mic, FTX-1 CP210x) | `main/audio_source.cpp/h` | Enum + name-lookup table; drives which streaming task (`stream_uac.cpp` or `stream_mic.cpp`) is armed |
| `uac_stream_profile_t` | Selects USB Audio Class device quirks/pipe management strategy | `main/stream_uac.h` (`UAC_PROFILE_QMX`, `UAC_PROFILE_GENERIC_USB`, `UAC_PROFILE_FTX1`) | Profile-gated branches inside `stream_uac.cpp` (e.g., FTX-1 half-duplex swap only fires under `UAC_PROFILE_FTX1`) |

## Directory Structure

```text
main/                    Application logic (single ESP-IDF "main" component)
  main.cpp               App entry point, main loop, slot timing, UI event wiring
  autoseq.cpp/h          QSO state machine and TX queue
  core_api.cpp/h         UI-agnostic functional core (snapshot + callback API)
  core_api_internal.h    Internal core_api helpers not exposed to consumers
  ft8_audio_pipeline.cpp/h  RX audio → FFT → decode pipeline glue
  stream_uac.cpp/h       USB Audio Class device streaming (QMX, generic UAC, FTX-1)
  stream_mic.cpp/h       KH1 analog mic capture path
  audio_source.cpp/h     Active audio backend selection
  radio_control.cpp/h    Radio backend dispatch
  radio_control_backend.h  Common radio_control_ops_t vtable definition
  radio_control_qmx.cpp, radio_control_qdx.cpp,
  radio_control_ftx1.cpp, radio_control_kh1_cat.cpp   Per-radio CAT backends
  gps.cpp/h               GPS time/grid sync
  resample.cpp/h          Q15 fixed-point audio resampler
  dds_q15.cpp/h           Integer DDS tone generator for TX
  protocol.h               FT8/FT4 protocol config selection
  station_types.h          Shared station/config structs

components/              Reusable ESP-IDF components
  ui/                     LVGL-based UI (components/ui/ui.cpp)
  storage_service/        ADIF logging, config persistence, SD/FATFS access
  external_rtc/            DS3231 RTC driver
  board_cardputer_adv/     Cardputer ADV board bring-up
  ft8_lib/                 Vendored FT8/FT4 protocol library (Karlis Goba)
  M5Unified/, M5GFX/, M5Cardputer/   M5Stack hardware abstraction libraries

managed_components/      ESP-IDF Component Manager-fetched dependencies (vendored, not
                          hand-edited): espressif__usb_host_uac, espressif__usb_host_cdc_acm,
                          espressif__usb_host_cp210x_vcp, espressif__usb, espressif__esp_tinyusb,
                          espressif__tinyusb, espressif__cmake_utilities,
                          espressif__esp_codec_dev, lvgl__lvgl
```

## Entry Points

- **`app_main()`** — `main/main.cpp:5712`. ESP-IDF boot entry point. Initializes storage,
  UI, and audio source, then spawns `app_task_core0` pinned to core 0
  (`xTaskCreatePinnedToCore(app_task_core0, "app_core0", ..., 0)`).
- **`app_task_core0()`** — `main/main.cpp:4677`. The main application task: storage init,
  keyboard/UI event loop, `check_slot_boundary()`, and LVGL refresh.
- **`stream_uac_task` / `usb_lib_task` / `uac_lib_task`** — `main/stream_uac.cpp`. Spawned
  pinned to core 1; own USB host library processing and UAC audio streaming.
- **`mic_stream_task`** — `main/stream_mic.cpp:177`. Spawned pinned to core 1; handles
  KH1 analog mic capture.
- **`core_api.cpp`** — Not a task; a synchronous, callback-driven API surface called by
  the UI layer (and any future control surface) from whatever task invokes it.

## Architectural Constraints

- **Threading:** Main app loop (`app_task_core0`) runs pinned to core 0. USB/UAC library
  tasks, the UAC streaming task, the speaker writer task, and the KH1 mic streaming task
  all run pinned to core 1.
- **Global state:** Significant module-level state lives in `main.cpp`: `g_call`,
  `g_grid`, `g_bands`, `g_autoseq_max_retry`, `g_beacon`, `g_radio`, `g_protocol`,
  `g_rtc_comp`, etc. (see `core_api.cpp` extern list for the accessible subset).
- **Circular imports:** None critical; header ordering is `core_api.h` → `autoseq.h` →
  `ui.h`.
- **Slot boundary timing:** Synchronized via RTC (or GPS, if present). Parity =
  `(rtc_now_ms() / 7500) % 2`. Skew tolerance is roughly ±1s before the audio pipeline
  desynchronizes from the radio's transmit slots.
- **Autoseq queue limit:** `AUTOSEQ_MAX_QUEUE` (30) active contexts; eviction by age and
  inactivity timeout when the queue is full.
- **Memory:** Heap fragmentation is critical during USB audio startup; buffers are sized
  around a 4608-byte DMA buffer and a 128-entry callsign hashtable (16 bytes/entry).
- **USB Host Controller pipe ceiling (FTX-1):** The ESP32-S3's USB host controller driver
  (HCD) has a measured hard 8-channel pipe ceiling. For the FTX-1's USB topology (hub +
  UAC mic + UAC speaker + CP210x CAT), this is insufficient to hold mic and speaker open
  simultaneously alongside the hub and CAT device, forcing the half-duplex time-multiplex
  design in `stream_uac.cpp` (see `uac_ftx1_prepare_tx()` / `uac_ftx1_prepare_rx()`).
  Other radios (QMX, QDX, KH1) do not hit this ceiling and keep both audio pipes open for
  the life of the session.
- **New USB dependency (FTX-1 CAT):** `espressif__usb_host_cp210x_vcp` is the first
  non-CDC-ACM, non-UAC USB host dependency in the project, used exclusively for FTX-1 CAT
  control (`main/stream_uac.cpp` calls `cp210x_vcp_open()` against `CP2105_PID`).

## Anti-Patterns to Avoid

- **Polling instead of event-driven:** Consumers of `core_api` should register a change
  callback rather than repeatedly pulling snapshots on a timer.
- **Blocking I/O in the main loop:** `app_task_core0` must not block on USB/serial I/O;
  that work belongs on the core-1 audio/streaming tasks.
- **Shared mutable state without synchronization:** Cross-core state such as the FTX-1
  half-duplex direction (`s_ftx1_direction`) and mic handle must be guarded (see
  `stream_uac.cpp`'s swap-serialization lock around `uac_ftx1_prepare_tx()` /
  `uac_ftx1_prepare_rx()`), since both the main task and the audio/decode task can trigger
  a TX arm concurrently.

## Error Handling

- **Config load failure:** Log error, fall back to hardcoded defaults (empty callsign,
  band center of 40m).
- **Radio control failure:** Log warning, continue RX-only (no CAT sync).
- **Audio underrun:** Mute speaker output, continue decoding on a partial slot.
- **Storage unavailable:** Skip ADIF logging, keep the QSO in memory only.

## Cross-Cutting Concerns

- **Logging:** ESP-IDF `ESP_LOG*` macros (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) throughout,
  each module defining a static `TAG`. A human-readable transaction log is also written to
  `RT[YYMMDD].txt` via `debug_log_line()`, and console UART mirroring is available when a
  debug pin is enabled.
- **Input validation:** Callsigns are validated as 3–11 alphanumeric characters plus `/`
  in `core_cmd_set_call()`; grid locators are validated as 4, 6, or 8 characters in
  `core_cmd_set_grid()`; frequency is range-checked per active radio backend.
- **Security:** This is a local, standalone device. CAT radio control assumes a trusted,
  physically-connected radio; there is no network-facing attack surface.

<!-- VERIFY: Hardware validation of FTX-1 USB Audio descriptor specifics (sample rate, bit depth, channel layout) — the project's own constraints note these are unconfirmed from documentation alone and must be checked against the physical radio. -->
