<!-- generated-by: gsd-doc-writer -->
# Development

Mini-FT8 is ESP-IDF firmware for the M5Stack Cardputer ADV (ESP32-S3) — there is no
`package.json`, no dev server, and no hot reload. "Development" here means building the
firmware image with `idf.py`, flashing it to a physical Cardputer over USB, and monitoring
its serial log. Several features (USB audio/CAT radio control, GPS, RTC, keyboard input)
can only be exercised against real hardware; see [Hardware-in-the-loop](#hardware-in-the-loop-caveat) below.

## Local setup

1. Install [ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/) for ESP32-S3. This repo pins that exact version — other IDF versions are not tested.
2. Clone the repo and initialize submodules/managed components:
   ```bash
   git clone <repo-url>
   cd Mini-FT8
   idf.py set-target esp32s3
   idf.py reconfigure
   ```
   Managed components are declared in `main/idf_component.yml` and resolved into
   `managed_components/` on first configure, pinned by `dependencies.lock`.
3. Build:
   ```bash
   idf.py build
   ```
4. Connect the Cardputer ADV over USB and flash + monitor:
   ```bash
   idf.py -p <PORT> flash monitor
   ```
   On Windows `<PORT>` is a COM port (e.g. `COM12`); on Linux/macOS it is a device path
   (e.g. `/dev/ttyUSB0` or `/dev/cu.usbmodem*`). Exit the serial monitor with `Ctrl+]`.

The repo also includes convenience scripts that hardcode one developer's IDF install path
and COM port — treat these as examples to adapt, not as generic tooling:
- `build.ps1`, `build_and_flash.py`, `dobuild.cmd` — set `IDF_PATH`/tool paths, then run
  `idf.py build` followed by `idf.py -p COM12 flash`.

<!-- VERIFY: repo clone URL for step 2 -->

## Build commands

All builds go through the standard ESP-IDF CLI (`idf.py`), invoked from the repo root
(where the top-level `CMakeLists.txt` lives):

| Command | Description |
|---|---|
| `idf.py build` | Compile the firmware and produce `MiniFT8_Merged_Auto.bin` (bootloader + app + partition table merged, via a `POST_BUILD` step in `CMakeLists.txt`). |
| `idf.py -p <PORT> flash` | Flash the built image to a connected Cardputer ADV. |
| `idf.py -p <PORT> monitor` | Open the serial console to view `ESP_LOGI`/`ESP_LOGW`/`ESP_LOGE` output at runtime. |
| `idf.py -p <PORT> flash monitor` | Flash, then immediately attach the monitor. |
| `idf.py menuconfig` | Open the interactive ESP-IDF/FreeRTOS configuration menu (writes `sdkconfig`). |
| `idf.py fullclean` | Remove the `build/` directory entirely; use when switching targets or after a corrupted build. |
| `idf.py build -DENABLE_FT4=OFF` | Build a smaller FT8-only image without the runtime FT8/FT4 mode-switch menu (see `CMakeLists.txt` `option(ENABLE_FT4 ...)`). |

There is no separate lint or format script — see [Code style](#code-style).

### Host-side test suite (no hardware required)

`tests/tx_e2e/` is a standalone CMake project (its own `CMakeLists.txt`, `project(tx_e2e C CXX)`)
that compiles `ft8_lib` and TX/RX pipeline logic natively (MinGW/GCC or Clang — MSVC is
explicitly rejected due to a C99 VLA requirement) and runs golden-file and state-machine
tests on the desktop, independent of the ESP32 target:

```bash
cd tests/tx_e2e
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Test sources include `test_golden_rx.cpp`, `test_l1_encoder.cpp`, `test_l2_state_machine.cpp`,
`test_l3_tx_poll_timing.cpp`, `test_l4_tx_timer_isolation.cpp`, `test_kh1_tone_map.cpp`, and
`test_ta_format.cpp`. See [`docs/TESTING.md`](TESTING.md) for individual test invocation and
target names.

### Hardware smoke-test app

`test_apps/cardputer_adv_audio_keyboard/` is a separate ESP-IDF project (own
`CMakeLists.txt`, references `components/` via `EXTRA_COMPONENT_DIRS`) for exercising the
Cardputer's audio and keyboard drivers in isolation. Build/flash it the same way as the
main firmware, from within that directory.

## Code style

No formatter is configured for this project's own code — there is no `.clang-format` or
`.editorconfig` at the repo root (some vendored dependencies under `components/` and
`managed_components/` carry their own, e.g. `components/M5Cardputer/.clang-format`, but those
apply only within those vendored trees, not to this project's code). No lint step runs in CI
(there is no CI configured at all; see [PR process](#pr-process) below). Style is enforced only
by convention and code review.
Observed conventions (verified by grepping `main/*.cpp` and `main/*.h`):

- **Functions**: `snake_case`, e.g. `autoseq_drop_index()`, `storage_is_active_log_name()`, `core_cmd_set_call()`.
- **Types**: `PascalCase` structs/classes and `enum class` names, e.g. `QsoContext`, `AutoseqState`, `CoreRadioType`.
- **Enum values**: `PascalCase`, e.g. `enum class TxMsgType { ... }`, `CoreBeaconMode::EVEN`.
- **Extern/public globals**: `g_` prefix, e.g. `g_protocol`, `g_call`, `g_beacon`.
- **Module-static (file-local) globals**: `s_` prefix, e.g. `s_queue`, `s_active_count` (this repo also uses a bare `g_was_txing` static in `main.cpp` — the `s_` convention is a strong norm, not a hard rule).
- **Files**: `lowercase_with_underscores.cpp` / `.h`, one header per translation unit (e.g. `autoseq.cpp` / `autoseq.h`).
- **Compiler flags**: the host-side test suite builds with `-Wall -Wextra -Wno-unused-parameter` (see `tests/tx_e2e/CMakeLists.txt`); MSVC is unsupported there. The root `CMakeLists.txt` (firmware target) does not set these flags directly — it goes through the standard ESP-IDF build system's own warning configuration instead.
- No exceptions (embedded target); error handling uses `bool`/`int` return codes and early returns, not exception throwing.

## Branch conventions

No branch naming convention is documented in [`CONTRIBUTING.md`](../CONTRIBUTING.md) or
anywhere else in the repository (no `.github/` templates exist). Follow whatever pattern the
maintainer requests, or use short, descriptive branch names (e.g. `ftx1-cat-support`) as a
reasonable default.

## PR process

See [`CONTRIBUTING.md`](../CONTRIBUTING.md) for contribution guidelines. There is no
`.github/` directory in this repository — no pull request template, no issue templates, and
no GitHub Actions CI workflow. In practice:

- There is no automated build/lint/test gate on pull requests; changes are reviewed manually.
- Given the hardware-in-the-loop nature of this project (see below), prefer describing what
  hardware was used to validate a change, since no CI can do this for you.
- If you add `.github/PULL_REQUEST_TEMPLATE.md` or a CI workflow, this section should be
  updated to reflect it — do not assume a process exists beyond what is in the repo.

## Hardware-in-the-loop caveat

Large parts of this codebase cannot be meaningfully verified without a physical M5Stack
Cardputer ADV and, for radio-specific work, the actual external radio (QMX, QDX, KH1, or
FTX-1). USB Audio Class descriptors, CAT command timing, PTT sequencing, GPS/RTC sync, and
keyboard/display behavior are all hardware-dependent and are not exercised by the host-side
`tests/tx_e2e` suite, which covers only the protocol/DSP layer (`ft8_lib`, TX/RX state
machines) on the desktop. When changing anything under `main/radio_control*.cpp`,
`main/stream_uac.cpp`, `main/stream_mic.cpp`, or `main/gps.cpp`, plan to flash and test on
real hardware — `idf.py -p <PORT> flash monitor` — before considering the change validated.
