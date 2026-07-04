# Codebase Structure

**Analysis Date:** 2026-07-04

## Directory Layout

```
Mini-FT8/
├── main/                          # Core application source
│   ├── main.cpp                   # Main app loop, UI modes, slot boundary detection
│   ├── core_api.cpp               # Functional-core API implementation
│   ├── autoseq.cpp                # Auto-sequencer: QSO state machine, queue management
│   ├── ft8_audio_pipeline.cpp     # FT8/FT4 audio processing, decode callback
│   ├── audio_source.cpp           # Audio input abstraction (QMX UAC, KH1 mic)
│   ├── stream_uac.cpp             # USB audio class (UAC) streaming
│   ├── stream_mic.cpp             # Built-in microphone input
│   ├── radio_control.cpp          # Generic radio control dispatcher
│   ├── radio_control_qmx.cpp      # QMX CAT implementation
│   ├── radio_control_qdx.cpp      # QDX CAT implementation
│   ├── radio_control_kh1_cat.cpp  # KH1 serial interface
│   ├── gps.cpp                    # GPS/GNSS time & grid sync
│   ├── dds_q15.cpp                # DDS oscillator for TX audio synthesis
│   ├── resample.cpp               # Audio resampling
│   ├── mic_selftest.cpp           # Microphone calibration
│   ├── *.h                        # Headers for above
│   ├── CMakeLists.txt             # Compilation config
│   └── idf_component.yml          # ESP-IDF component manifest
│
├── components/                    # ESP-IDF reusable components
│   ├── ui/                        # UI rendering and screen management
│   │   ├── include/ui.h           # Public UI API
│   │   ├── ui.cpp                 # Display logic, waterfall, RX list, menus
│   │   ├── mic_input.cpp          # Keyboard/mic input handling
│   │   └── CMakeLists.txt
│   │
│   ├── storage_service/           # Configuration and log persistence
│   │   ├── storage_service.cpp    # FATFS and SD card management
│   │   ├── storage_service.h      # Public API
│   │   └── CMakeLists.txt
│   │
│   ├── external_rtc/              # DS3231 RTC (I2C)
│   │   ├── external_rtc.cpp
│   │   ├── external_rtc.h
│   │   └── CMakeLists.txt
│   │
│   ├── board_cardputer_adv/       # M5Stack Cardputer ADV board support
│   │   ├── board_power.cpp        # Power management, battery
│   │   ├── board_power.h
│   │   └── CMakeLists.txt
│   │
│   ├── ft8_lib/                   # FT8/FT4 encoding/decoding (Karlis Goba)
│   │   ├── ft8/                   # Protocol implementation
│   │   │   ├── decode.c
│   │   │   ├── encode.c
│   │   │   ├── constants.h
│   │   │   ├── message.c
│   │   │   └── ...
│   │   ├── fft/                   # FFT & windowing
│   │   ├── common/                # Shared utilities
│   │   │   └── monitor.h          # Decode results container
│   │   └── CMakeLists.txt
│   │
│   ├── M5Cardputer/               # M5Stack Cardputer device library
│   ├── M5Unified/                 # M5Stack unified HAL
│   ├── M5GFX/                     # M5Stack graphics driver
│   └── README.md
│
├── managed_components/            # Auto-downloaded ESP-IDF components
│   ├── espressif__cmake_utilities/
│   ├── espressif__usb/
│   └── ...
│
├── tests/                         # Unit tests (currently minimal)
├── test_apps/                     # Test applications
├── docs/                          # Architecture documentation
│   ├── AUTOSEQ_ARCHITECTURE.md    # State machine design
│   ├── AUTOSEQ_INACTIVE_QUEUE.md  # Retry and timeout logic
│   ├── RTC_COMPENSATION.md        # Time synchronization
│   └── FT8_Free-Text_Reference_Extension.md
│
├── .planning/                     # GSD planning documents
│   └── codebase/                  # Codebase analysis (ARCHITECTURE.md, etc.)
│
├── CMakeLists.txt                 # Top-level build config
├── partitions.csv                 # Flash partition table
├── sdkconfig                      # ESP-IDF configuration (FT4 enable, etc.)
├── dependencies.lock              # Locked component versions
├── README.md                       # User-facing manual
├── LICENSE                        # Apache 2.0
└── .gitignore
```

## Directory Purposes

**main/**
- Purpose: Core FT8 transceiver logic (no UI dependencies except types)
- Contains: Application state machine, autoseq engine, audio pipeline, radio control
- Key files: `main.cpp` (entry point), `autoseq.cpp` (QSO queue), `core_api.cpp` (functional core)
- Why separate: Allows future alternate UIs (web, CLI, remote) to consume `core_api` without UI.cpp

**components/ui/**
- Purpose: Screen rendering and user input
- Contains: Display layout, RX/TX/MENU mode drawing, waterfall, keyboard handling
- Key files: `ui.cpp` (all drawing logic), `ui.h` (public API for text/lines/waterfall)
- Threading: Called from main app loop (single-threaded)

**components/storage_service/**
- Purpose: Flash persistence (FATFS, partition management)
- Contains: Station.txt (config), ADIF (logs), RT[YYMMDD].txt (RX/TX transcripts)
- Key files: `storage_service.cpp`, `storage_service.h`
- Exposed: `storage_service_init()`, `storage_read_station()`, `storage_write_station()`, `storage_copy_to_sd()`

**components/ft8_lib/**
- Purpose: FT8/FT4 codec implementation (unchanged from upstream ft8_lib)
- Contains: Symbol encoding, Turbo-code decoding, FFT analysis, tone detection
- Key files: `ft8/decode.c`, `ft8/encode.c`, `fft/` (radix-2 FFT), `common/monitor.h`
- Threading: Decoding happens on background task (audio_pipeline_run)

**components/external_rtc/**
- Purpose: Optional DS3231 I2C real-time clock (for retained time without GPS)
- Contains: I2C driver, time get/set, compensation storage
- Used by: `main/main.cpp:app_task_core0()` to initialize RTC if present

**components/board_cardputer_adv/**
- Purpose: M5Stack Cardputer ADV board-specific setup
- Contains: GPIO config, power control, LED, display init
- Key files: `board_power.cpp`, `board_power.h`

## Key File Locations

**Entry Points:**
- `main/main.cpp:app_main()` (line 5601) → `app_task_core0()` (line 4579): ESP-IDF app entry; creates main task
- `main/main.cpp:decode_monitor_results()`: Called by audio pipeline when decode completes (Event 2)
- `main/main.cpp:check_slot_boundary()`: Detects 15s slot transition, triggers TX (Event 3)

**Configuration:**
- `CMakeLists.txt` (line 15): `ENABLE_FT4` option toggles FT4 support at build time
- `sdkconfig`: ESP-IDF settings (UART pins, heap size, power management)
- `main/feature_flags.h`: Compile-time toggles (DEBUG_LOG, etc.)

**Core Logic:**
- `main/autoseq.cpp`: QsoContext state machine, queue sort/evict, message generation
- `main/core_api.cpp`: Command routing (core_cmd_*) and snapshot generation (core_get_*)
- `main/ft8_audio_pipeline.cpp`: Audio task loop, decode callback, waterfall row push

**UI:**
- `components/ui/ui.cpp`: All screen rendering (RX, TX, MENU, waterfall, countdown)
- `components/ui/ui.h`: Public API for text lines, waterfall, RX list

**Storage:**
- `components/storage_service/storage_service.cpp`: FATFS mount, Station.txt read/write, ADIF logging
- Station.txt format: Key=Value pairs (call=, grid=, radio=, protocol_mode=, bands=, etc.)

**Radio Control:**
- `main/radio_control.cpp`: Dispatcher; routes to QMX/QDX/KH1 backends
- `main/radio_control_qmx.cpp`: QMX CAT commands (frequency, mode, PTT)
- `main/radio_control_kh1_cat.cpp`: KH1 serial interface (microphone auto-gain, PTT)

**Audio:**
- `main/stream_uac.cpp`: USB Audio Class input; feed to decode
- `main/stream_mic.cpp`: Built-in microphone fallback (KH1-MIC)
- `main/audio_source.cpp`: Audio abstraction layer (selects UAC or mic)
- `main/ft8_audio_pipeline.cpp`: Decoding task, FFT → decode → callback

## Naming Conventions

**Files:**
- Snake_case with .cpp (C++) or .c (C)
- Paired .h headers (e.g., `autoseq.cpp` + `autoseq.h`)
- Component headers in `include/` subdirectory (`components/ui/include/ui.h`)

**Functions:**
- Camel_case with leading module prefix or static (private)
  - `autoseq_get_qso_states()` (public)
  - `static void sort_and_clean()` (private in autoseq.cpp)
- C++ member functions use class naming conventions (e.g., QsoContext methods)

**Variables:**
- Global/static: Prefix `g_` or `s_`
  - `g_call`, `g_grid`, `g_autoseq_max_retry` (module globals in main.cpp)
  - `s_pending_ft_text` (static in autoseq.cpp)
- Local: snake_case
  - `int slot_id`, `std::string dxcall`
- Enums: PascalCase or SCREAMING_SNAKE_CASE
  - `enum class AutoseqState { CALLING, REPLYING, ... }`
  - `#define AUTOSEQ_MAX_QUEUE 30`

**Types:**
- Classes/structs: PascalCase
  - `struct QsoContext`, `struct RxDecodeEntry`, `class CoreAdifHandle`
- Enums: PascalCase (unscoped) or PascalCase within `enum class`
  - `enum class CoreQsoState { ... }`

## Where to Add New Code

**New Feature (e.g., new CQ type):**
- Primary code: `main/autoseq.cpp` (add state, message generation)
- UI: `components/ui/ui.cpp` (add menu item)
- Persistence: `Station.txt` field (add key in `storage_service.cpp`)
- Tests: `tests/` (new test file)

**New Component/Module:**
- Create `components/<name>/` with `CMakeLists.txt` and `include/<name>.h`
- Link in top-level `CMakeLists.txt` (project components list)
- Follow pattern: `components/<name>/include/<name>.h` (public), `components/<name>/<name>.cpp` (impl)

**Utilities:**
- Shared helpers: `main/<helper>.cpp` if radio-specific, else `components/<lib>/` if reusable
- Example: Resampling is specific to audio, lives in `main/resample.cpp`; could move to `components/dsp/` if shared with multiple apps

**Radio Backend:**
- New radio (e.g., Xiegu X5105):
  - Create `main/radio_control_x5105.cpp`
  - Implement `radio_control_backend_t` interface (frequency, mode, PTT functions)
  - Register in `main/radio_control.cpp` dispatcher
  - Add enum value to `station_types.h:RadioType`

**UI Mode:**
- Add `UIMode::NEWMODE` enum in `main/main.cpp`
- Implement drawing function `static void draw_newmode_view()`
- Add key handler in main loop (search `if (c == 'r'...)` pattern)
- Add redraw dirty flag (e.g., `g_newmode_dirty`)

## Special Directories

**main/**
- Purpose: Application logic (no UI, no persistence, no board-specific code)
- Generated: No
- Committed: Yes (all source)
- Rules: No direct `#include "board_*.h"` or `#include "ui.h"` except types (ui.h:RxDecodeEntry for core_api)

**.planning/codebase/**
- Purpose: GSD analysis documents (ARCHITECTURE.md, STRUCTURE.md, etc.)
- Generated: Yes (by `/gsd-map-codebase`)
- Committed: Yes (read-only snapshots for reference)
- Rules: Do not edit manually; regenerate with `/gsd-map-codebase`

**managed_components/**
- Purpose: Auto-downloaded third-party ESP-IDF components (Espressif USB, etc.)
- Generated: Yes (by `idf.py add-dependency`)
- Committed: No (git ignored; restored from `dependencies.lock`)
- Rules: Do not edit; update via `idf.py add-dependency` commands

**docs/**
- Purpose: Architecture documentation (not user manual)
- Generated: No
- Committed: Yes
- Key docs: AUTOSEQ_ARCHITECTURE.md (state machine), RTC_COMPENSATION.md (timing sync)

**build/**
- Purpose: CMake build output (binaries, intermediates)
- Generated: Yes (by `idf.py build`)
- Committed: No (git ignored)

---

*Structure analysis: 2026-07-04*
