# Technology Stack

**Analysis Date:** 2026-07-04

## Languages

**Primary:**
- C++ (Modern C++17/20) - Core application logic, audio processing, protocol implementation
- C - FT8/FT4 protocol library (ft8_lib), ESP-IDF components, driver-level code

**Secondary:**
- CMake - Build system configuration

## Runtime

**Environment:**
- ESP-IDF (Espressif IoT Development Framework) v5.5.1 - Firmware development framework
- FreeRTOS - Real-time operating system kernel running on ESP32-S3

**Target Hardware:**
- ESP32-S3 (Xtensa processor) - Dual-core microcontroller at 240 MHz
- M5Stack Cardputer ADV - Development board with 2.7" LCD display, keyboard, ESP32-S3

**Package Manager:**
- ESP-IDF Component Manager - Manages component dependencies
- Lockfile: `dependencies.lock` (present, locked to version 2.0.0)

## Frameworks

**Core:**
- ESP-IDF v5.5.1 - Provides WiFi, Bluetooth, peripheral drivers, memory management
- FreeRTOS - Task scheduling, queues, semaphores, real-time multitasking

**UI/Graphics:**
- LVGL (Light and Versatile Graphics Library) v8.4.0 - Embedded GUI framework for display rendering
- M5GFX v0.1.15 - M5Stack graphics abstraction layer for the Cardputer display
- M5Unified - Unified hardware interface for M5Stack devices
- M5Cardputer - M5Stack Cardputer-specific hardware driver library

**Audio Processing:**
- USB Audio Class (UAC) - Host-side audio streaming via `espressif/usb_host_uac` v1.3.3
- USB Host CDC (Communications Device Class) - Serial CAT protocol support via `espressif/usb_host_cdc_acm` v2.2.0
- USB Device/Host Core - `espressif/usb` v1.1.0 for dual-role USB support

**Audio Codec:**
- ESP Codec Device v1.5.9 - Audio codec driver abstraction for ES8311 microphone (Cardputer built-in)
- Custom audio pipeline (`ft8_audio_pipeline.cpp`) - Audio source routing and resampling

**Signal Processing:**
- Custom DSP library - DDS (Direct Digital Synthesis) for tone generation (`dds_q15.cpp`)
- Audio resampling library - Frequency conversion between input sources and processing sample rates
- FFT implementation - Frequency domain analysis in FT8/FT4 protocol library

**Testing/Build:**
- CMake v3.16+ - Build configuration and compilation
- esptool - Binary merge and firmware flashing (`MiniFT8_Merged_Auto.bin` generation)

## Key Dependencies

**Critical:**
- ft8_lib (custom component) - FT8/FT4 protocol encoding/decoding from Karlis Goba
  - Location: `components/ft8_lib/` (C implementation with FFT, modulation/demodulation)
  - Why: Core amateur radio protocol functionality

- LVGL v8.4.0 - GUI rendering
  - Why: Provides all display UI elements and widgets

- USB Audio Host (espressif/usb_host_uac v1.3.3) - Audio streaming from USB peripherals (QMX, KH1-USBC)
  - Why: Enables RX/TX with external radio equipment over USB audio

- USB CDC Host (espressif/usb_host_cdc_acm v2.2.0) - Serial CAT protocol for radio control
  - Why: Computer-aided transceiver (CAT) protocol for KH1 frequency/mode control

**Infrastructure:**
- FreeRTOS - Task management, concurrency, inter-task communication
- ESP-IDF Drivers - UART, I2C, I2S, GPIO, ADC, Timer
- ESP Codec Device v1.5.9 - Microphone capture (Cardputer built-in ES8311 codec)

**Storage:**
- FATFS v3 - FAT/FAT32 filesystem for 3 MB internal partition and SD card support
- NVS (Non-Volatile Storage) - 24 KB partition for configuration/settings
- VFS (Virtual File System) - Abstraction layer for multiple storage backends

**Custom Components:**
- `ui` - LVGL-based user interface (`components/ui/ui.cpp`)
- `storage_service` - File I/O service for ADIF logs, QSO records, configuration files
- `board_cardputer_adv` - M5Stack Cardputer ADV board support (`components/board_cardputer_adv/`)
- `external_rtc` - DS3231 RTC module support for persistent UTC time
- `ft8_lib` - FT8/FT4 protocol implementation (encode/decode, modulation)

## Configuration

**Environment:**
- Build-time toggles via CMake:
  - `ENABLE_FT4=ON|OFF` (default: ON) - Compile FT4 protocol support
  - Protocol mode selection in `Station.txt` (runtime boot-time choice: FT8 or FT4)

**Build Configuration:**
- `sdkconfig` - ESP-IDF configuration (auto-generated)
  - Key settings:
    - `CONFIG_IDF_TARGET=esp32s3`
    - `CONFIG_FATFS_VOLUME_COUNT=2` (FATFS + SD card)
    - `CONFIG_FATFS_SECTOR_512=y` (512-byte sectors)
    - FreeRTOS task heap and stack configurations

- `partitions.csv` - Flash memory layout:
  - NVS: 24 KB (configuration storage)
  - Factory app: ~5 MB (application binary)
  - FATFS: 3 MB (logs, QSO records, user data)

**Firmware Configuration Files:**
- `Station.txt` - Persistent user settings (callsign, grid, mode, offset, etc.)
- `[YYMMDD].adi` - ADIF QSO logs (Cabrillo format for Field Day)
- `RT[YYMMDD].txt` - RX/TX transaction logs (human-readable transcripts)

## Platform Requirements

**Development:**
- CMake v3.16 or later
- ESP-IDF v5.5.1 toolchain (xtensa-esp32s3-elf)
- Python 3.8+ (ESP-IDF dependency)
- Platform: Windows, Linux, or macOS (cross-platform)

**Hardware:**
- M5Stack Cardputer ADV with ESP32-S3 (required)
- Optional: UART GPS module (9600/115200 baud) on PORTA
- Optional: DS3231 RTC module on I2C
- Optional: USB Audio adapter (KH1-USBC RX)
- Optional: External radio equipment (QMX, QDX, KH1) via USB/Serial/Audio

**Production:**
- Deployment: Firmware binary flashing to ESP32-S3 flash
- Target device: M5Stack Cardputer ADV standalone
- Output binary: `MiniFT8_Merged_Auto.bin` (merged bootloader + app + partitions)

## Performance Characteristics

**Memory:**
- RAM: ~320 KB heap available on ESP32-S3 (out of 512 KB total SRAM)
- Used for: Audio buffers, FT8 decode state machine, UI rendering, task stacks
- Storage: 3 MB FATFS partition (typical usage: ~1-2 MB after 50+ QSOs)

**Audio Processing:**
- Sample rate: 6 kHz (FT8 RX processing)
- UAC output: 48 kHz (USB audio host)
- Resampling: Online Q15 fixed-point resampler
- DDS: Integer-based tone generation for TX (no floating-point needed)

**Timing:**
- FT8 slot period: 15 seconds per transmission slot
- FT4 slot period: 7.5 seconds (faster protocol)
- Display refresh: LVGL event loop at ~30 Hz
- GPS sync: Asynchronous UART input (9600 or 115200 baud)

---

*Stack analysis: 2026-07-04*
