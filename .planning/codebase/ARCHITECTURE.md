<!-- refreshed: 2026-07-04 -->
# Architecture

**Analysis Date:** 2026-07-04

## System Overview

Mini-FT8 is a single-threaded FT8/FT4 radio transceiver application for the M5Stack Cardputer running on ESP32-S3. The architecture follows a **three-event state machine pattern** with strict ordering to eliminate race conditions during TX/RX transitions.

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                          UI / Main App Loop                              │
│                      `main/main.cpp:app_task_core0`                      │
├──────────────────────────────────────────────────────────────────────────┤
│   - Keyboard/display input handling                                      │
│   - Mode navigation (RX, TX, MENU, STATUS, GPS, QSO, BAND, DELETE, MSC) │
│   - Slot boundary detection (15s even/odd)                              │
│   - TX state machine ticking                                             │
└────────────────────────┬─────────────────────────────────────────────────┘
                         │
        ┌────────────────┴────────────────┬────────────────┐
        ▼                                  ▼                ▼
┌──────────────────┐          ┌──────────────────┐  ┌─────────────────┐
│ Audio/Decode     │          │ Functional Core  │  │ Radio Control   │
│ Pipeline         │          │ (Autoseq + QSO)  │  │ (CAT, direct)   │
│ `main/` and      │          │ `main/autoseq.`  │  │ `main/radio_`   │
│ `ft8_lib/`       │          │ `cpp & core_api` │  │ `.cpp & .h`     │
└──────────────────┘          └──────────────────┘  └─────────────────┘
        │                              │                     │
        └──────────────────────────────┼─────────────────────┘
                                       │
                    ┌──────────────────┴──────────────────┐
                    ▼                                     ▼
            ┌────────────────────┐          ┌──────────────────────┐
            │ Configuration      │          │ Storage / Logging    │
            │ (Station.txt)      │          │ (FATFS, ADIF, logs)  │
            │ `storage_service`  │          │ `storage_service`    │
            └────────────────────┘          └──────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File(s) |
|-----------|----------------|---------|
| **Main App Loop** | Keyboard input, mode switching, slot boundary detection, UI refresh | `main/main.cpp:app_task_core0()` |
| **Autoseq Engine** | QSO state machine, TX queue management, message generation | `main/autoseq.cpp/h` |
| **Core API** | UI-agnostic functional core, snapshot accessors, change callbacks | `main/core_api.cpp/h` |
| **Audio Pipeline** | RX audio processing, FFT analysis, waterfall generation, FT8/FT4 decoding | `main/ft8_audio_pipeline.cpp/h`, `ft8_lib/` |
| **Radio Control** | CAT control (QMX, QDX), KH1 serial interface, VFO sync | `main/radio_control*.cpp` |
| **GPS / RTC** | Date/time sync, grid location, external RTC support | `main/gps.cpp`, `external_rtc/` |
| **UI Rendering** | Screen layout, RX list display, waterfall, countdown bar | `components/ui/ui.cpp` |
| **Storage** | Configuration persistence, ADIF logging, SD card access | `components/storage_service/` |

## Pattern Overview

**Overall:** Single-threaded event-driven state machine following the **DX-FT8 reference architecture**. No mutex protection needed within the main loop; synchronization only at task boundaries and shared resources.

**Key Characteristics:**
- **Three-event ordering guarantee:** Event 2 (decode complete) ALWAYS happens before Event 3 (slot boundary check/TX trigger)
- **One-shot CQ/FreeText entries:** Added to autoseq queue with TX_NONE marker; transmit once then evict
- **Snapshot + callback pattern:** Core API consumers pull snapshots and register change callbacks; no polling
- **Display-first architecture:** UI changes driven by dirty flags, reducing redraw overhead

## Layers

**Event 1: Decode Start**
- Purpose: Signal that 79 FT8 symbols have been received and decoding should begin
- Location: `main/ft8_audio_pipeline.cpp` (audio processing task feeds decoded symbols to `ft8_lib`)
- Contains: Symbol buffering, RX waterfall generation
- Depends on: FT8/FT4 library headers (`ft8_lib/ft8/decode.h`, `ft8_lib/common/monitor.h`)
- Used by: Audio task, waterfall display

**Event 2: Decode Complete**
- Purpose: Process decoded messages, update autoseq queue, prepare next TX message
- Location: `main/main.cpp:decode_monitor_results()` (called from audio pipeline callback)
- Contains: Message parsing, CQ addition (if beacon on), TX queue refresh
- Depends on: `autoseq.h`, `ui.h`, `core_api_internal.h`
- Used by: Main app loop

**Event 3: Slot Boundary (15s boundary)**
- Purpose: Detect even/odd slot transition and trigger TX if message ready
- Location: `main/main.cpp:check_slot_boundary()` (called from main app loop)
- Contains: Slot parity detection, TX initiation, autoseq tick() call
- Depends on: `autoseq.h`, `radio_control.h`, `audio_source.h`
- Used by: Main app loop (highest frequency)

**Functional Core Layer**
- Purpose: UI-agnostic API exposing QSO state, configuration, and commands
- Location: `main/core_api.cpp/h`
- Contains: Snapshot accessors, change callbacks, mutation commands
- Depends on: `autoseq.h`, `station_types.h`, `core_api_internal.h`
- Used by: UI (currently); future control surfaces

**Autoseq (Auto-Sequencer) Layer**
- Purpose: Manage active QSO contexts, apply retry logic, generate TX messages
- Location: `main/autoseq.cpp/h`
- Contains: QsoContext state machine, queue sorting/eviction, ADIF/Cabrillo logging callbacks
- Depends on: `ui.h` (RxDecodeEntry), `station_types.h`
- Used by: core_api, main app loop (for RX/TX interaction)

**Audio/Decode Layer**
- Purpose: Receive audio from QMX/KH1, resample to 6 kHz, decode FT8/FT4 messages
- Location: `main/ft8_audio_pipeline.cpp`, `main/stream_uac.cpp`, `main/stream_mic.cpp`, `ft8_lib/`
- Contains: USB audio class stream, mic input routing, FFT & tone detection, FT8/FT4 decoding
- Depends on: FT8 library, audio driver APIs
- Used by: Waterfall display, decode_monitor_results callback

**Radio Control Layer**
- Purpose: Interface with radio via CAT (QMX, QDX) or serial (KH1)
- Location: `main/radio_control.cpp`, `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`, `main/radio_control_kh1_cat.cpp`
- Contains: CAT command encoding, serial I/O, VFO frequency sync
- Depends on: `radio_control_backend.h`, driver APIs (UART, USB)
- Used by: Main app loop (band selection, manual tune)

## Data Flow

### Primary Request Path: Receive, Decode, Reply

1. **Audio Acquisition** (`main/stream_uac.cpp:stream_read_callback()`)
   - Audio driver fills USB audio buffers at ~6 kHz sample rate
   - Resampling to 6000 Hz (from native sample rate)

2. **Symbol Processing** (`ft8_lib/ft8/decode.c`)
   - 79 FT8 symbols (≈12.8s reception window) accumulated
   - FFT analysis, tone detection

3. **Message Decode Complete** (`main/ft8_audio_pipeline.cpp`)
   - Calls `decode_monitor_results()` callback
   - Waterfall row pushed to UI

4. **Autoseq Processing** (`main/main.cpp:decode_monitor_results()`)
   - `autoseq_on_decodes()` processes RX messages addressed to us
   - Creates/updates QsoContext entries for new or continuing QSOs
   - Sets `g_qso_xmit` flag and `target_slot_parity` if reply ready

5. **Slot Boundary Event** (`main/main.cpp:check_slot_boundary()`)
   - 15s slot transition detected (RTC or GPS-synced)
   - If `was_txing`: call `autoseq_tick()` to pop completed TX context
   - If `g_qso_xmit && correct_parity`: call `tx_start()` to initiate transmission

6. **TX Execution** (`main/main.cpp:tx_tick()`)
   - Audio generation (DDS + modulation)
   - CAT commands to radio (tune, set mode, PTT)
   - TX runs ~12.8s (until next slot boundary)

7. **UI Refresh** (`components/ui/ui.cpp`)
   - `ui_draw_rx()` renders decoded messages
   - `ui_draw_waterfall()` shows spectral activity
   - `redraw_tx_view()` shows active QSOs and pending messages

### Secondary Flow: User Manual QSO (Tap RX)

1. User presses key 1–6 in RX mode to select a decoded message
2. `ui_handle_rx_key()` → `core_cmd_tap_rx(idx)` 
3. `autoseq_on_touch()` creates new QsoContext with dxcall/dxgrid
4. Follows primary path from "Autoseq Processing" onward

### Secondary Flow: Manual TX / FreeText

1. User enters MENU (M key), edits FreeText, selects "Send Once" (key 2)
2. `core_cmd_queue_freetext(text)` → `autoseq_schedule_freetext()`
3. FreeText context added with `is_freetext=true`, high priority
4. At next slot boundary, FT message transmitted (preempts regular QSOs)
5. After TX, context evicted (one-shot)

### Secondary Flow: Beacon CQ

1. User cycles beacon mode (STATUS key 1) to EVEN or ODD
2. `autoseq_set_cq_type()` sets beacon config
3. After each decode completes (Event 2), if no QSO TX ready:
   - `decode_monitor_results()` calls `autoseq_start_cq()`
   - Adds one-shot CQ with TX_NONE marker
4. CQ transmitted at next matching slot boundary

**State Management:**
- **RTC**: `rtc_now_ms()` provides monotonic millisecond time; compensated by `g_rtc_comp` ppm-like drift correction
- **Slot Parity**: `(rtc_now_ms() / 7500) % 2` determines even (0) / odd (1) slot
- **TX Countdown**: `ms_to_boundary = 7500 - (rtc_now_ms() % 7500)` updates on-screen timer
- **Autoseq Queue**: Sorted by state priority (higher = more advanced QSO); evicted when IDLE or after timeout

## Key Abstractions

**QsoContext (Autoseq):**
- Purpose: Represents one active or inactive QSO contact
- Examples: `main/autoseq.cpp:QsoContext`, `main/autoseq.h` struct
- Pattern: State machine with transitions: CALLING → REPLYING → REPORT → ROGER_REPORT → ROGERS → SIGNOFF → IDLE

**RxDecodeEntry / UiRxLine:**
- Purpose: Single decoded message for display and interaction
- Examples: `main/ui.h:RxDecodeEntry` (fixed-size heap-friendly), `main/ui.h:UiRxLine` (std::string based)
- Pattern: Lightweight snapshot; no state mutations, display-only until user taps

**Functional-Core API (core_api.h):**
- Purpose: UI-agnostic command/query interface
- Examples: `core_get_qso()`, `core_cmd_tap_rx()`, `core_on_rx_changed()`
- Pattern: Notify + pull: register callback, then pull fresh snapshot on fire

**Protocol Config (protocol.h):**
- Purpose: Runtime selection of FT8 vs FT4 encoding/decoding
- Examples: `g_protocol = &kProtocolFT8` or `&kProtocolFT4`
- Pattern: Global pointer; chosen at boot from Station.txt; never changed mid-session

## Entry Points

**app_main() → app_task_core0()**
- Location: `main/main.cpp:5601`
- Triggers: ESP-IDF boot
- Responsibilities: 
  - Initialize storage, board power, UI, autoseq, core_api
  - Load station config (call, grid, bands, radio type)
  - Main event loop: keyboard, slot boundary, TX state, UI redraw

**ft8_audio_pipeline_run()**
- Location: `main/ft8_audio_pipeline.cpp`
- Triggers: `audio_source_start()` in STATUS mode
- Responsibilities:
  - Continuous RX audio processing on background task
  - Decode completion callback to `decode_monitor_results()`

**core_init() / core_get_*() / core_cmd_*()**
- Location: `main/core_api.cpp`
- Triggers: Called by UI or future control surfaces
- Responsibilities:
  - Snapshot pulls (core_get_qso, core_get_config, core_get_rx_list)
  - Mutations (core_cmd_tap_rx, core_cmd_set_call, etc.)
  - Async callbacks when state changes

## Architectural Constraints

- **Threading:** Single-threaded main app loop on core 0; audio decode task on background (core 1 or pool)
- **Global state:** Significant module-level state in `main.cpp`: `g_call`, `g_grid`, `g_bands`, `g_autoseq_max_retry`, `g_beacon`, `g_radio`, etc. (see `core_api.cpp` extern list)
- **Circular imports:** None critical; header ordering: `core_api.h` → `autoseq.h` → `ui.h` → safe
- **Slot boundary timing:** Synchronized via RTC (or GPS if available). Parity = `(rtc_now_ms() / 7500) % 2`. Skew tolerance: ±1s before audio pipeline desynchronizes
- **Autoseq queue limit:** Max 30 contexts (AUTOSEQ_MAX_QUEUE); eviction by age + inactivity timeout if exceeded
- **Memory:** Heap fragmentation critical during USB audio startup; sized for 4608-byte DMA buffer + callsign hashtable (128 entries × 16 bytes)

## Anti-Patterns

### Polling Instead of Event-Driven

**What happens:** Code checks flags or states in a tight loop instead of reacting to discrete events (e.g., checking `g_decode_enabled` every iteration before calling decode).

**Why it's wrong:** Wastes CPU cycles, increases latency to event response, makes timing non-deterministic.

**Do this instead:** Use the three-event pattern: decode-complete callback fires Event 2, slot boundary check fires Event 3, UI main loop reacts with dirty flags. See `main/main.cpp:4688` (main loop structure) and `main/ft8_audio_pipeline.cpp:28` (decode_monitor_results callback).

### Blocking I/O in Main Loop

**What happens:** Calling `uart_write_bytes()` or `radio_control_ready()` without non-blocking timeout, potentially stalling the UI.

**Why it's wrong:** Blocks slot boundary detection, delays keyboard response, can cause audio decode to miss symbol boundaries.

**Do this instead:** Use timeouts (`vTaskDelay(pdMS_TO_TICKS(10))`) in main loop, queue work to background tasks. See `main/main.cpp:4799` (tx_tick is non-blocking, state machine).

### Shared Mutable State Without Synchronization

**What happens:** Audio task and main task access `g_decode_in_progress` or `g_qso_xmit` without atomic/spinlock protection.

**Why it's wrong:** Undefined behavior on SMP (race condition on fetch/store).

**Do this instead:** Use `volatile bool` for simple flags (load/store are atomic on ARM), or `std::atomic<bool>` for explicit intent. For complex structures, use spinlock (see `ft8_audio_pipeline.cpp:s_latest_waterfall_row_lock`).

## Error Handling

**Strategy:** Fail-safe with graceful degradation. No panics; log error, return false/null, continue operation.

**Patterns:**
- **Config load failure:** Log error, use hardcoded defaults (empty call, center of 40m)
- **Radio control failure:** Log warning, continue RX-only (no CAT sync)
- **Audio underrun:** Mute speaker, continue decoding (partial slot)
- **Storage unavailable:** Skip ADIF logging, keep QSO in memory

## Cross-Cutting Concerns

**Logging:** 
- ESP-IDF `ESP_LOG*` macros used throughout (ESP_LOGI, ESP_LOGE)
- Debug log file (RT[YYMMDD].txt) via `debug_log_line()`
- Console UART mirroring (if DEBUG pin enabled)

**Validation:** 
- Callsign: 3–11 alphanumeric + `/` characters, validated in `core_cmd_set_call()`
- Grid: 4, 6, or 8 character locator, validated in `core_cmd_set_grid()`
- Frequency: Range-checked per radio backend

**Authentication:** 
- None (local device). CAT radio control assumes trusted connection.

---

*Architecture analysis: 2026-07-04*
