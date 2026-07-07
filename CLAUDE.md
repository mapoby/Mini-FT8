<!-- GSD:project-start source:PROJECT.md -->
## Project

**Mini-FT8 — Yaesu FTX-1 Support**

Mini-FT8 is an FT8/FT4 amateur radio transceiver application running on an M5Stack Cardputer ADV (ESP32-S3), currently supporting the QMX, QDX, and (Elecraft) KH1 as external radios via CAT control plus USB/serial audio. This milestone adds the Yaesu FTX-1 as a new supported radio, controlled entirely over a single USB connection that carries both CAT (frequency/mode/PTT/power) and audio (mic in / speaker out).

**Core Value:** A user with a Yaesu FTX-1 can plug it into the Cardputer over USB and run full FT8/FT4 QSOs — RX decode, autoseq, and TX — exactly as they already can with QMX/QDX/KH1.

### Constraints

- **Tech stack**: Must integrate with existing ESP-IDF v5.5.1 / FreeRTOS / ESP32-S3 architecture; no new RTOS or threading model
- **New dependency**: Espressif `usb_host_cp210x_vcp` component (plain-C, not the C++ `usb_host_vcp` generic service) required for CP210x CAT support — first non-CDC-ACM, non-UAC USB dependency in the project
- **Hardware validation**: USB Audio descriptor specifics (sample rate, bit depth, channel layout) for the FTX-1 are unconfirmed from documentation alone and must be validated against the physical radio
- **USB bus bandwidth**: Existing FIFO partitioning in `stream_uac.cpp` was hand-tuned for QMX's simultaneous bidirectional 24-bit/48kHz/stereo audio + CDC CAT; adding a CP210x VCP endpoint alongside UAC may require re-tuning
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++ (Modern C++17/20) - Core application logic, audio processing, protocol implementation
- C - FT8/FT4 protocol library (ft8_lib), ESP-IDF components, driver-level code
- CMake - Build system configuration
## Runtime
- ESP-IDF (Espressif IoT Development Framework) v5.5.1 - Firmware development framework
- FreeRTOS - Real-time operating system kernel running on ESP32-S3
- ESP32-S3 (Xtensa processor) - Dual-core microcontroller at 240 MHz
- M5Stack Cardputer ADV - Development board with 2.7" LCD display, keyboard, ESP32-S3
- ESP-IDF Component Manager - Manages component dependencies
- Lockfile: `dependencies.lock` (present, locked to version 2.0.0)
## Frameworks
- ESP-IDF v5.5.1 - Provides WiFi, Bluetooth, peripheral drivers, memory management
- FreeRTOS - Task scheduling, queues, semaphores, real-time multitasking
- LVGL (Light and Versatile Graphics Library) v8.4.0 - Embedded GUI framework for display rendering
- M5GFX v0.1.15 - M5Stack graphics abstraction layer for the Cardputer display
- M5Unified - Unified hardware interface for M5Stack devices
- M5Cardputer - M5Stack Cardputer-specific hardware driver library
- USB Audio Class (UAC) - Host-side audio streaming via `espressif/usb_host_uac` v1.3.3
- USB Host CDC (Communications Device Class) - Serial CAT protocol support via `espressif/usb_host_cdc_acm` v2.2.0
- USB Device/Host Core - `espressif/usb` v1.1.0 for dual-role USB support
- ESP Codec Device v1.5.9 - Audio codec driver abstraction for ES8311 microphone (Cardputer built-in)
- Custom audio pipeline (`ft8_audio_pipeline.cpp`) - Audio source routing and resampling
- Custom DSP library - DDS (Direct Digital Synthesis) for tone generation (`dds_q15.cpp`)
- Audio resampling library - Frequency conversion between input sources and processing sample rates
- FFT implementation - Frequency domain analysis in FT8/FT4 protocol library
- CMake v3.16+ - Build configuration and compilation
- esptool - Binary merge and firmware flashing (`MiniFT8_Merged_Auto.bin` generation)
## Key Dependencies
- ft8_lib (custom component) - FT8/FT4 protocol encoding/decoding from Karlis Goba
- LVGL v8.4.0 - GUI rendering
- USB Audio Host (espressif/usb_host_uac v1.3.3) - Audio streaming from USB peripherals (QMX, KH1-USBC)
- USB CDC Host (espressif/usb_host_cdc_acm v2.2.0) - Serial CAT protocol for radio control
- FreeRTOS - Task management, concurrency, inter-task communication
- ESP-IDF Drivers - UART, I2C, I2S, GPIO, ADC, Timer
- ESP Codec Device v1.5.9 - Microphone capture (Cardputer built-in ES8311 codec)
- FATFS v3 - FAT/FAT32 filesystem for 3 MB internal partition and SD card support
- NVS (Non-Volatile Storage) - 24 KB partition for configuration/settings
- VFS (Virtual File System) - Abstraction layer for multiple storage backends
- `ui` - LVGL-based user interface (`components/ui/ui.cpp`)
- `storage_service` - File I/O service for ADIF logs, QSO records, configuration files
- `board_cardputer_adv` - M5Stack Cardputer ADV board support (`components/board_cardputer_adv/`)
- `external_rtc` - DS3231 RTC module support for persistent UTC time
- `ft8_lib` - FT8/FT4 protocol implementation (encode/decode, modulation)
## Configuration
- Build-time toggles via CMake:
- `sdkconfig` - ESP-IDF configuration (auto-generated)
- `partitions.csv` - Flash memory layout:
- `Station.txt` - Persistent user settings (callsign, grid, mode, offset, etc.)
- `[YYMMDD].adi` - ADIF QSO logs (Cabrillo format for Field Day)
- `RT[YYMMDD].txt` - RX/TX transaction logs (human-readable transcripts)
## Platform Requirements
- CMake v3.16 or later
- ESP-IDF v5.5.1 toolchain (xtensa-esp32s3-elf)
- Python 3.8+ (ESP-IDF dependency)
- Platform: Windows, Linux, or macOS (cross-platform)
- M5Stack Cardputer ADV with ESP32-S3 (required)
- Optional: UART GPS module (9600/115200 baud) on PORTA
- Optional: DS3231 RTC module on I2C
- Optional: USB Audio adapter (KH1-USBC RX)
- Optional: External radio equipment (QMX, QDX, KH1) via USB/Serial/Audio
- Deployment: Firmware binary flashing to ESP32-S3 flash
- Target device: M5Stack Cardputer ADV standalone
- Output binary: `MiniFT8_Merged_Auto.bin` (merged bootloader + app + partitions)
## Performance Characteristics
- RAM: ~320 KB heap available on ESP32-S3 (out of 512 KB total SRAM)
- Used for: Audio buffers, FT8 decode state machine, UI rendering, task stacks
- Storage: 3 MB FATFS partition (typical usage: ~1-2 MB after 50+ QSOs)
- Sample rate: 6 kHz (FT8 RX processing)
- UAC output: 48 kHz (USB audio host)
- Resampling: Online Q15 fixed-point resampler
- DDS: Integer-based tone generation for TX (no floating-point needed)
- FT8 slot period: 15 seconds per transmission slot
- FT4 slot period: 7.5 seconds (faster protocol)
- Display refresh: LVGL event loop at ~30 Hz
- GPS sync: Asynchronous UART input (9600 or 115200 baud)
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Naming Patterns
- Source files: `lowercase_with_underscores.cpp` or `.h`
- Example: `audio_source.cpp`, `autoseq.h`, `core_api_internal.h`
- All functions use snake_case
- Example: `autoseq_init()`, `hashtable_lookup()`, `storage_is_active_log_name()`, `core_cmd_set_call()`
- PascalCase for all type names
- Example: `QsoContext`, `ProtocolConfig`, `RxDecodeEntry`, `StationConfig`
- Enum class names: PascalCase
- Enum values: PascalCase
- Example: `enum class AutoseqState { CALLING, REPLYING, REPORT, IDLE }`
- Example: `enum class CoreCqType { CQ = 0, SOTA, POTA, QRP, FD, FREETEXT }`
- SCREAMING_SNAKE_CASE
- Example: `AUTOSEQ_MAX_QUEUE`, `CALLSIGN_HASHTABLE_SIZE`, `FT8_SAMPLE_RATE`, `ENABLE_FT4`
- Extern/public globals: `g_prefix_name`
- Example: `g_protocol`, `g_qso_xmit`, `g_decode_in_progress`, `g_band_sel`, `g_offset_hz`
- Accessed from multiple files (declared in main.cpp, extern in other modules)
- Module-static variables: `s_prefix_name`
- Example: `s_queue`, `s_active_count`, `s_tx_msg_buffer`, `s_pending_ft_text`, `s_my_call`
- Used for encapsulation within a single translation unit
## Code Style
- No automatic formatter configured (.clang-format not present)
- Manual formatting observed:
- No .clang-tidy or lint configuration files present
- Compiler flags in CMakeLists.txt: `-Wall -Wextra -Wno-unused-parameter`
- MSVC unsupported (C99 VLA requirement) per `tests/tx_e2e/CMakeLists.txt`
## Import Organization
#define DEBUG_LOG 1
#include <cstdio>
#include <cmath>
#include "esp_log.h"
#include "board_power.h"
#include "ui.h"
#include <vector>
#include <string>
#include "freertos/FreeRTOS.h"
- No custom path aliases used; imports are relative to include directories
- Include directories set via CMakeLists.txt `target_include_directories()`
## Error Handling
- Return `bool` for success/failure: `true` = success, `false` = failure
- Return `int` status codes for enum-style errors
- Input validation at function entry
- Early returns for error conditions (fail-fast pattern)
- Optional pointer validation before use
- No exceptions used (embedded system)
## Logging
- `ESP_LOGI(TAG, ...)` - Informational (common state transitions, normal operations)
- `ESP_LOGW(TAG, ...)` - Warning (recoverable errors, retry scenarios)
- `ESP_LOGE(TAG, ...)` - Error (critical failures; not found in analyzed code but pattern established)
- Each module defines a static `const char* TAG` at file top
- Log state transitions with relevant context
- Debug prints via `fprintf(stderr, ...)` for development
- Conditional debug logging with `DEBUG_LOG` macro
- State machine transitions (entering/leaving states)
- Queue operations (add, remove, clear)
- User actions (tap RX, drop QSO, set config)
- Error conditions with context (callsign, slot, retry count)
- Timing issues (slot boundaries, delays)
## Comments
- Invariants and preconditions (why a check is needed)
- Complex algorithms (state machine transitions, hash probe logic)
- Non-obvious data structure layouts
- Section headers and grouping
- Obvious code (don't repeat what code says)
- Standard patterns (well-known algorithms like linear probing)
- Not used; this is C/C++ embedded code
- Some header comments for API boundaries (see `core_api.h` lines 1-22)
- Multi-line comment block above #pragma once explaining module purpose
- Example (core_api.h:1-22): Explains "Functional-core API", "notify + pull" pattern, thread safety
## Function Design
- Most functions stay under 50 lines
- State machine handlers (autoseq) may be 100+ lines (acceptable due to complexity)
- Larger functions are broken into logical sections with comment separators
- Prefer value types for simple data (`int`, `bool`, `float`)
- Use const references for strings and vectors: `const std::string&`, `const std::vector<>`
- Output parameters passed as non-const references or pointers
- Prefer returning status via return code, not output parameters
- Use `bool` for success/failure
- Use `std::string` for text returns (RVO/move semantics handle efficiency)
- Return const pointers when returning static data (`const char*`, `const ProtocolConfig*`)
- Function pointer typedefs for callback registration
- Callbacks are registered once at init time; no removal API
## Module Design
- Header files use `#pragma once`
- Public functions declared in .h, implemented in .cpp
- Static module-level functions declared in .cpp (not in .h)
- Extern variables declared in .h, defined in one .cpp (usually main.cpp)
- Not used; each .h maps to one .cpp with same base name
- Heavy use of `static` for module-level state (autoseq.cpp has ~10 static variables)
- `extern` globals provide controlled access to main.cpp state
- Callbacks enable bidirectional communication without tight coupling
## Key Architectural Patterns
- `AutoseqState` enum for states
- `QsoContext` struct holds per-QSO state
- `TxMsgType` enum for message types
- `set_state()` and `generate_response()` handle transitions
- Comments explain invariants (e.g., next_tx may only be TX1-TX5, never TX6)
- Fixed-size array (`CALLSIGN_HASHTABLE_SIZE = 128`)
- Age tracking in upper 8 bits of hash value
- 22-bit hash payload stored (6000 unique callsigns in 128 slots)
- Comments explain full table scan due to deletion holes
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## System Overview
```text
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
- **Three-event ordering guarantee:** Event 2 (decode complete) ALWAYS happens before Event 3 (slot boundary check/TX trigger)
- **One-shot CQ/FreeText entries:** Added to autoseq queue with TX_NONE marker; transmit once then evict
- **Snapshot + callback pattern:** Core API consumers pull snapshots and register change callbacks; no polling
- **Display-first architecture:** UI changes driven by dirty flags, reducing redraw overhead
## Layers
- Purpose: Signal that 79 FT8 symbols have been received and decoding should begin
- Location: `main/ft8_audio_pipeline.cpp` (audio processing task feeds decoded symbols to `ft8_lib`)
- Contains: Symbol buffering, RX waterfall generation
- Depends on: FT8/FT4 library headers (`ft8_lib/ft8/decode.h`, `ft8_lib/common/monitor.h`)
- Used by: Audio task, waterfall display
- Purpose: Process decoded messages, update autoseq queue, prepare next TX message
- Location: `main/main.cpp:decode_monitor_results()` (called from audio pipeline callback)
- Contains: Message parsing, CQ addition (if beacon on), TX queue refresh
- Depends on: `autoseq.h`, `ui.h`, `core_api_internal.h`
- Used by: Main app loop
- Purpose: Detect even/odd slot transition and trigger TX if message ready
- Location: `main/main.cpp:check_slot_boundary()` (called from main app loop)
- Contains: Slot parity detection, TX initiation, autoseq tick() call
- Depends on: `autoseq.h`, `radio_control.h`, `audio_source.h`
- Used by: Main app loop (highest frequency)
- Purpose: UI-agnostic API exposing QSO state, configuration, and commands
- Location: `main/core_api.cpp/h`
- Contains: Snapshot accessors, change callbacks, mutation commands
- Depends on: `autoseq.h`, `station_types.h`, `core_api_internal.h`
- Used by: UI (currently); future control surfaces
- Purpose: Manage active QSO contexts, apply retry logic, generate TX messages
- Location: `main/autoseq.cpp/h`
- Contains: QsoContext state machine, queue sorting/eviction, ADIF/Cabrillo logging callbacks
- Depends on: `ui.h` (RxDecodeEntry), `station_types.h`
- Used by: core_api, main app loop (for RX/TX interaction)
- Purpose: Receive audio from QMX/KH1, resample to 6 kHz, decode FT8/FT4 messages
- Location: `main/ft8_audio_pipeline.cpp`, `main/stream_uac.cpp`, `main/stream_mic.cpp`, `ft8_lib/`
- Contains: USB audio class stream, mic input routing, FFT & tone detection, FT8/FT4 decoding
- Depends on: FT8 library, audio driver APIs
- Used by: Waterfall display, decode_monitor_results callback
- Purpose: Interface with radio via CAT (QMX, QDX) or serial (KH1)
- Location: `main/radio_control.cpp`, `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`, `main/radio_control_kh1_cat.cpp`
- Contains: CAT command encoding, serial I/O, VFO frequency sync
- Depends on: `radio_control_backend.h`, driver APIs (UART, USB)
- Used by: Main app loop (band selection, manual tune)
## Data Flow
### Primary Request Path: Receive, Decode, Reply
### Secondary Flow: User Manual QSO (Tap RX)
### Secondary Flow: Manual TX / FreeText
### Secondary Flow: Beacon CQ
- **RTC**: `rtc_now_ms()` provides monotonic millisecond time; compensated by `g_rtc_comp` ppm-like drift correction
- **Slot Parity**: `(rtc_now_ms() / 7500) % 2` determines even (0) / odd (1) slot
- **TX Countdown**: `ms_to_boundary = 7500 - (rtc_now_ms() % 7500)` updates on-screen timer
- **Autoseq Queue**: Sorted by state priority (higher = more advanced QSO); evicted when IDLE or after timeout
## Key Abstractions
- Purpose: Represents one active or inactive QSO contact
- Examples: `main/autoseq.cpp:QsoContext`, `main/autoseq.h` struct
- Pattern: State machine with transitions: CALLING → REPLYING → REPORT → ROGER_REPORT → ROGERS → SIGNOFF → IDLE
- Purpose: Single decoded message for display and interaction
- Examples: `main/ui.h:RxDecodeEntry` (fixed-size heap-friendly), `main/ui.h:UiRxLine` (std::string based)
- Pattern: Lightweight snapshot; no state mutations, display-only until user taps
- Purpose: UI-agnostic command/query interface
- Examples: `core_get_qso()`, `core_cmd_tap_rx()`, `core_on_rx_changed()`
- Pattern: Notify + pull: register callback, then pull fresh snapshot on fire
- Purpose: Runtime selection of FT8 vs FT4 encoding/decoding
- Examples: `g_protocol = &kProtocolFT8` or `&kProtocolFT4`
- Pattern: Global pointer; chosen at boot from Station.txt; never changed mid-session
## Entry Points
- Location: `main/main.cpp:5601`
- Triggers: ESP-IDF boot
- Responsibilities: 
- Location: `main/ft8_audio_pipeline.cpp`
- Triggers: `audio_source_start()` in STATUS mode
- Responsibilities:
- Location: `main/core_api.cpp`
- Triggers: Called by UI or future control surfaces
- Responsibilities:
## Architectural Constraints
- **Threading:** Single-threaded main app loop on core 0; audio decode task on background (core 1 or pool)
- **Global state:** Significant module-level state in `main.cpp`: `g_call`, `g_grid`, `g_bands`, `g_autoseq_max_retry`, `g_beacon`, `g_radio`, etc. (see `core_api.cpp` extern list)
- **Circular imports:** None critical; header ordering: `core_api.h` → `autoseq.h` → `ui.h` → safe
- **Slot boundary timing:** Synchronized via RTC (or GPS if available). Parity = `(rtc_now_ms() / 7500) % 2`. Skew tolerance: ±1s before audio pipeline desynchronizes
- **Autoseq queue limit:** Max 30 contexts (AUTOSEQ_MAX_QUEUE); eviction by age + inactivity timeout if exceeded
- **Memory:** Heap fragmentation critical during USB audio startup; sized for 4608-byte DMA buffer + callsign hashtable (128 entries × 16 bytes)
## Anti-Patterns
### Polling Instead of Event-Driven
### Blocking I/O in Main Loop
### Shared Mutable State Without Synchronization
## Error Handling
- **Config load failure:** Log error, use hardcoded defaults (empty call, center of 40m)
- **Radio control failure:** Log warning, continue RX-only (no CAT sync)
- **Audio underrun:** Mute speaker, continue decoding (partial slot)
- **Storage unavailable:** Skip ADIF logging, keep QSO in memory
## Cross-Cutting Concerns
- ESP-IDF `ESP_LOG*` macros used throughout (ESP_LOGI, ESP_LOGE)
- Debug log file (RT[YYMMDD].txt) via `debug_log_line()`
- Console UART mirroring (if DEBUG pin enabled)
- Callsign: 3–11 alphanumeric + `/` characters, validated in `core_cmd_set_call()`
- Grid: 4, 6, or 8 character locator, validated in `core_cmd_set_grid()`
- Frequency: Range-checked per radio backend
- None (local device). CAT radio control assumes trusted connection.
<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->
## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
