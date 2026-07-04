# External Integrations

**Analysis Date:** 2026-07-04

## APIs & External Services

**None - No Cloud/Network APIs Used**

Mini-FT8 is an embedded amateur radio application with no external HTTP/REST/cloud integrations. All functionality is local to the device.

## Hardware Interfaces

**Radio Equipment Integration:**
- QMX Radio (Micro QRP SDR) - Digital RF transceiver
  - Connection: USB Audio + Serial CAT
  - Implementation: `main/radio_control_qmx.cpp`
  - Audio: Receives at 48 kHz via USB Audio Class host
  - Control: Frequency/mode via serial CAT protocol

- QDX Radio (Micro SDR Transceiver) - Another compact SDR
  - Connection: USB Audio + Serial CAT
  - Implementation: `main/radio_control_qdx.cpp`
  - Protocol: Compatible QDX CAT command set

- KH1 Transceiver (by Dave Casler) - SSB/CW/FT8/FT4 capable
  - Connection: RS-232 serial CAT on PORTA
  - Implementation: `main/radio_control_kh1_cat.cpp`
  - Audio TX: Tones generated internally at 6 kHz sample rate
  - Audio RX: Either USB-C microphone adapter (KH1-USBC) or built-in Cardputer mic (KH1-MIC)
  - Features: Automatic TX power control (set to 2W)

**GPS/GNSS Receivers:**
- PORTA GPS (9600 or 115200 baud) - Any UART NMEA GPS module
  - Implementation: `main/gps.cpp`
  - Purpose: UTC date/time sync, grid square calculation
  - Auto-detects baud rate on startup
  - Can be removed after initial time/grid acquisition

- M5Stack LoRa-1262 cap GNSS (115200 baud UART2)
  - Connection: Pins G15 (RX), G13 (TX)
  - Runtime toggle: MENU P3 → Option 4 (`GNSS_LoRa: ON/OFF`)
  - Note: Only GNSS receiver is used; LoRa/SX1262 radio is unused

**External RTC Module:**
- DS3231 I2C RTC - Real-time clock for persistent UTC time
  - Connection: I2C bus (SDA=G8, SCL=G9) on Cardputer
  - Implementation: `components/external_rtc/`
  - Purpose: Retains date/time across power cycles (no GPS needed)
  - Auto-detected on boot; GPS/manual time updates write to RTC when present

**Audio Peripherals:**
- Cardputer Built-in ES8311 Microphone Codec
  - Implementation: Uses `espressif/esp_codec_dev` v1.5.9
  - Purpose: Microphone input for KH1-MIC mode (native audio recording)
  - Driver: ESP codec device abstraction layer

- USB Audio Adapter (Analog USB-C microphone) - For KH1-USBC mode
  - Standard audio device (class-compliant)
  - Implemented as generic USB Audio Class host consumer
  - Purpose: Receive audio from KH1 transceiver

## Data Storage

**Databases:**
- No traditional databases - File-based logging only

**File Storage:**
- FATFS (FAT/FAT32 filesystem) on internal flash (3 MB partition)
  - Location: `fatfs` partition (0x500000 offset, 0x300000 size in partitions.csv)
  - Purpose: QSO logs, transaction records, user settings
  - Formatted automatically on first boot if needed
  - Wear-levelling enabled in ESP-IDF configuration

- SD Card Support - Optional external storage
  - Connection: SD card via M5Stack slot
  - Implementation: `storage_service` component
  - Purpose: Backup logs and files to SD card
  - Feature: Copy-to-SD (MENU P3 → Option 5)

- NVS (Non-Volatile Storage) - Configuration partition
  - Size: 24 KB (0x6000 bytes in partitions.csv)
  - Purpose: Factory calibration, MAC address, etc.
  - Managed: Directly by ESP-IDF

**File Types:**
- `Station.txt` - User settings (callsign, grid, frequency, mode, etc.)
- `[YYYYMMDD].adi` - ADIF QSO logs (amateur radio standard format)
- `Cabrillo[YYYYMMDD].txt` - Cabrillo format logs for Field Day contests
- `RT[YYMMDD].txt` - RX/TX transaction logs (human-readable)
- Band configuration files

**Caching:**
- None - No persistent caching layer; files are read/written as needed

## Authentication & Identity

**Local Only:**
- No remote authentication required
- User callsign stored in `Station.txt` (file-based configuration)
- Amateur radio operator credentials are user-provided (self-identification via callsign)

## Monitoring & Observability

**Error Tracking:**
- None - No remote error reporting

**Logs:**
- Serial/USB Debug Output: `esp_log.h` framework (configurable log levels)
- File-based Logs:
  - `RT[YYMMDD].txt` - Transaction logs (RX/TX activity)
  - `[YYYYMMDD].adi` - QSO records with timestamps, signal reports, grids
- Performance Monitor: Added in v2.0.4 (MENU P3 → Option P)

**Telemetry:**
- No external telemetry
- GPS data logged locally (grid square in QSO records)
- Signal reports (SNR) stored with each QSO

## CI/CD & Deployment

**Hosting:**
- Standalone embedded device (M5Stack Cardputer ADV)
- No cloud deployment

**CI Pipeline:**
- None - Project is GitHub repository only
- Firmware built locally with `idf.py build`
- Binary flashing via USB Serial JTAG

**Firmware Updates:**
- Manual download of new firmware from GitHub releases
- USB flashing via esptool over Serial JTAG interface
- No over-the-air (OTA) update mechanism

**Build Artifacts:**
- `MiniFT8_Merged_Auto.bin` - Complete merged binary (generated post-build)
  - Contains bootloader + app + partition table + FATFS image
  - Created by custom CMake post-build command using esptool merge_bin

## Environment Configuration

**Hardware Pins & Peripherals:**
Configured at compile-time and runtime:

- **PORTA (Serial/GPIO interface):**
  - TX (G2) - Serial output
  - RX (G1) - Serial input
  - GND, 5V - Power
  - Micro switch selects: 5VOUT (GPS mode) or 5VIN (KH1 power)

- **PORTB (I2C):**
  - SDA (G8), SCL (G9) - I2C bus for RTC module

- **UART2 (LoRa-1262 cap GNSS):**
  - RX (G15), TX (G13) - Alternative GPS source

- **USB Host (OTG):**
  - USB-C connector - USB audio + CDC serial for radios

- **USB Device:**
  - Serial JTAG console - Debug output and command interface

**Required Configuration Files:**
- None - No external config files needed for basic operation
- All user settings stored in `Station.txt` (created on first boot)

**Secrets Location:**
- No secrets stored - Application is standalone
- Amateur radio callsign is user-provided (not a secret)
- No API keys or credentials needed

**Data Sharing with PC:**
- USB Drive Mode (MENU P3 → Option C) - Exposes FATFS to PC as USB mass storage
  - Allows file transfer without SD card
  - Safely mounts/unmounts via application menu

## Webhooks & Callbacks

**Incoming:**
- None - No network capabilities

**Outgoing:**
- None - No remote integrations

**Internal Callbacks:**
- ADIF logging callback: `autoseq_set_adif_callback()` (`main/autoseq.h`)
  - Called when QSO completes (TX4/TX5 transmission)
  - Passes: dxcall, dxgrid, SNR values
  - Implemented by main application to write ADIF files

## Inter-Device Communication

**Serial CAT Protocol (Amateur Radio Control):**
- Implementation: `main/radio_control.cpp`, `main/radio_control_*.cpp`
- Protocols:
  - QMX CAT commands (custom binary format)
  - QDX CAT commands (custom binary format)
  - KH1 CAT commands (RS-232 serial format)
- Purpose: Frequency tuning, mode selection, TX power control, status queries

**USB Audio Streaming (UAC):**
- Implementation: `main/stream_uac.cpp`
- Direction: Host-side audio consumer (Cardputer as host, radio as device)
- Sample rate: 48 kHz
- Channels: Mono
- Purpose: Receive audio from QMX/QDX/KH1-USBC

**GPS NMEA Protocol:**
- Implementation: `main/gps.cpp`
- Format: Standard NMEA-0183 sentences (RMC, GGA, GSA)
- Baud rates: 9600 or 115200 (auto-detected)
- Purpose: UTC time and WGS84 grid square calculation

## Offline Operation

Mini-FT8 operates **fully offline**:
- No internet or WiFi required
- No cloud synchronization
- All logs stored locally on device or SD card
- Perfect for portable/POTA (Parks On The Air) operations

---

*Integration audit: 2026-07-04*
