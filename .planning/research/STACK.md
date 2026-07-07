# Stack Research

**Domain:** ESP-IDF USB host support for a Silicon Labs CP210x (dual) USB-UART bridge, added alongside existing `usb_host_uac` + `usb_host_cdc_acm` usage
**Researched:** 2026-07-04
**Confidence:** HIGH (component source/headers/changelogs read directly from the `espressif/esp-usb` GitHub repo and the ESP Component Registry; MEDIUM for FTX-1-specific USB descriptor details, which are corroborated by two independent web sources but not yet validated against real hardware)

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| `espressif/usb_host_cp210x_vcp` | `^2.2.0` (latest published) | CP210x-specific line-coding/control-line/break vtable patched onto a standard `cdc_acm_dev_hdl_t` | This is the actual, minimal, official CP210x driver. It is a **pure C** component (single `.c` file, no C++/exceptions) that internally calls the *existing* `cdc_acm_host_open()` and then overwrites four function pointers (`intf_func.line_coding_set/get`, `set_control_line_state`, `send_break`) with CP210x vendor-request implementations (AN571 command set: `SET_BAUDRATE`, `SET_LINE_CTL`, `SET_MHS`, etc.). Everything else — `cdc_acm_host_data_tx_blocking()`, `cdc_acm_host_close()`, `cdc_acm_host_desc_print()`, the `event_cb`/`data_cb` model — is the **exact same API already used** in `stream_uac.cpp` for the QMX. This is a drop-in sibling to the existing CDC-ACM code path, not a new API to learn. |
| `espressif/usb_host_cdc_acm` | `^2.2.0` (already a project dependency; latest is `2.4.0`, both satisfy the range) | Base CDC-ACM host driver; owns the `cdc_acm_dev_hdl_t` type, transfer plumbing, and the "interface" vtable (`cdc_acm_host_interface.h`) that CP210x/FTDI/CH34x drivers patch | No change needed — `usb_host_cp210x_vcp@2.2.0` declares `espressif/usb_host_cdc_acm: ^2.1.0` as its dependency, which the project's pinned `^2.2.0` already satisfies. No version bump, no `idf_component.yml` conflict. |
| ESP-IDF | v5.5.1 (unchanged) | USB Host Library, FreeRTOS | Project constraint; no USB-host API changes required for this addition. |

### Supporting Libraries — what NOT to add

| Library | Why it looks relevant | Why to skip it |
|---------|----------------------|-----------------|
| `espressif/usb_host_vcp` (the generic "VCP service", currently `1.0.0~5`) | Sounds like "the" official multi-chip VCP abstraction (CP210x/FTDI/CH34x behind one `VCP::open()` call); this is what PROJECT.md's Context section refers to as "usb_host_vcp" | It is a **C++-only** template/factory service (`esp_usb::VCP`, `esp_usb::CP210x : public CdcAcmDevice`). Its header hard-errors at compile time if `CONFIG_COMPILER_CXX_EXCEPTIONS` is not set (`vcp.hpp`/`vcp_cp210x.hpp`: `#ifndef CONFIG_COMPILER_CXX_EXCEPTIONS #error ...`), and its `CP210x` constructor `throw`s the `esp_err_t` on open failure. `stream_uac.cpp`'s existing pattern is plain C, error-code-based (`esp_err_t err = cdc_acm_host_open(...); if (err == ESP_OK) ...`), with no exception handling anywhere in the codebase. Pulling this in would mean either (a) enabling C++ exceptions project-wide (extra flash/RAM cost, new failure-handling paths to design) or (b) wrapping every VCP open call in try/catch as a one-off. Since the FTX-1's VID/PID is already known (Silicon Labs `0x10C4` + CP2105 `0xEA70`), the generic multi-driver auto-detection this service exists for is not needed — a hard-coded VID/PID/PID-list open via `usb_host_cp210x_vcp`'s plain C `cp210x_vcp_open()` function is strictly simpler and fits the existing style. **Recommendation: do not depend on `usb_host_vcp`. Depend only on `usb_host_cp210x_vcp`, and call its C function, not the C++ wrapper class.** |
| `espressif/usb_host_ftdi_vcp`, `espressif/usb_host_ch34x_vcp` | Sibling drivers in the same `esp-usb` family | Not needed — FTX-1 is confirmed CP210x (Silicon Labs "Dual CP210x" bridge), not FTDI or CH34x. |
| Hand-rolled CP210x vendor-request driver | PROJECT.md flagged this as the fallback if no official component existed | Not needed — the official driver already implements exactly this (AN571 command set) and is actively maintained (last CHANGELOG entries for the family show cdc_acm_host active releases through 2026-04). Reinventing it duplicates well-tested vendor-request logic for no benefit. |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| `idf.py add-dependency "espressif/usb_host_cp210x_vcp^2.2.0"` | Registers the dependency in `main/idf_component.yml` and pulls it via the component manager | Equivalent to hand-editing `main/idf_component.yml` (see Installation below); either works. |

## Installation

Add to `main/idf_component.yml` (alongside the existing `usb_host_uac` / `usb_host_cdc_acm` entries):

```yaml
dependencies:
  # ...existing entries unchanged...
  espressif/usb_host_uac: ^1.2.0
  espressif/usb: '*'
  espressif/usb_host_cdc_acm: ^2.2.0
  # CP210x CAT for FTX-1 (Enhanced COM / CAT-1 port only)
  espressif/usb_host_cp210x_vcp: ^2.2.0
  espressif/esp_codec_dev: ^1.5.9
```

Then `idf.py reconfigure` (or a clean build) to have the component manager fetch it into `managed_components/`. No changes to `espressif/usb_host_cdc_acm`'s pinned version are required — `usb_host_cp210x_vcp@2.2.0` requires `^2.1.0`, and the project's `^2.2.0` (resolved locally to `2.2.0`, compatible up to but excluding `3.0.0`) already satisfies it.

## API Surface: CP210x driver vs. existing CDC-ACM (QMX) usage

**Opening the device** — new call, same handle type:

```c
#include "usb/vcp_cp210x.h"   // brings in SILICON_LABS_VID, CP210X_PID, CP2105_PID, CP2108_PID, cp210x_vcp_open()

cdc_acm_dev_hdl_t s_ftx1_cat_handle = NULL;
cdc_acm_host_device_config_t dev_cfg = {
    .connection_timeout_ms = 1000,
    .out_buffer_size = 64,
    .in_buffer_size = 256,        // FTX-1 CAT-1 needs RX (unlike QMX's send-only CAT); see Pitfalls
    .event_cb = cdc_event_cb,     // same callback shape as existing QMX code
    .data_cb  = cat_data_cb,      // new: FTX-1 needs RX for `PC;`/`FA;` query replies
    .user_arg = NULL,
};
// Explicit PID (CP2105, dual-port) and interface 0 = CAT-1 (Enhanced COM):
esp_err_t err = cp210x_vcp_open(CP2105_PID, /*interface_idx=*/0, &dev_cfg, &s_ftx1_cat_handle);
// or: cp210x_vcp_open(CP210X_PID_AUTO, 0, &dev_cfg, &s_ftx1_cat_handle) to probe all CP210x PIDs
```

`cp210x_vcp_open()` internally calls the same `cdc_acm_host_open(SILICON_LABS_VID, pid, interface_idx, dev_config, cdc_hdl_ret)` the project already links against, then patches 4 function pointers and issues one `CP210X_CMD_IFC_ENABLE` vendor request (CP210x interfaces must be explicitly enabled — see Pitfalls). The returned `cdc_hdl_ret` is a plain `cdc_acm_dev_hdl_t`, identical in type to what `cdc_acm_host_open()` returns for the QMX today.

**Setting baud rate / line coding** — identical call, now backed by CP210x vendor requests instead of a CDC `SET_LINE_CODING` class request:

```c
cdc_acm_line_coding_t line_coding = {
    .dwDTERate = 38400,   // FTX-1 CAT-1 default per CAT manual
    .bDataBits = 8,
    .bParityType = 0,     // none
    .bCharFormat = 0,     // 1 stop bit (or 1 per menu config)
};
cdc_acm_host_line_coding_set(s_ftx1_cat_handle, &line_coding);   // same function used for any CDC-ACM device
```

No new function name — `cdc_acm_host_line_coding_set()`/`_get()` are the same public API; only the underlying transport differs (CP210x driver replaced the vtable entries so these calls issue AN571 vendor commands `SET_BAUDRATE`/`SET_LINE_CTL` on the control endpoint instead of the standard CDC `SET_LINE_CODING`/`SET_CONTROL_LINE_STATE` requests QMX's genuine CDC-ACM class uses).

**Reading / writing bytes** — no change at all from the QMX pattern:

```c
cdc_acm_host_data_tx_blocking(s_ftx1_cat_handle, (const uint8_t*)"FA;", 3, 100);   // same fn as cat_cdc_send() today
// RX: register data_cb in dev_cfg (QMX's CAT is currently TX-only / in_buffer_size=0;
// FTX-1 needs replies to query commands, e.g. reading back FA/PC, so in_buffer_size
// must be >0 and data_cb must be implemented — this is new relative to the QMX path)
```

**Practical integration point:** in `main/stream_uac.cpp`, `cdc_try_open()` currently special-cases `UAC_PROFILE_QMX` with a hardcoded VID/PID `cdc_acm_host_open(k_qmx_vid, k_qmx_pid, 0, &dev_cfg, &handle)`. A new `UAC_PROFILE_FTX1` (or a dedicated FTX-1 CAT path outside `stream_uac.cpp` entirely, given FTX-1 also needs bidirectional UAC — see PROJECT.md) should call `cp210x_vcp_open(CP2105_PID, 0, &dev_cfg, &handle)` instead, and must NOT scan/hint on `USB_CLASS_COMM` — CP210x interfaces report `bInterfaceClass` in the **vendor-specific** range (0xFF), not `USB_CLASS_COMM` (0x02), because CP210x is not itself CDC-ACM-descriptor-compliant. The existing `cdc_new_dev_cb()` interface-class scan (`intf->bInterfaceClass == USB_CLASS_COMM`) will silently find nothing for the FTX-1 and must be extended (or bypassed via the known VID/PID/interface path, which is simpler and matches the QMX code's own known-VID/PID fast path).

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|--------------------------|
| `espressif/usb_host_cp210x_vcp` C API (`cp210x_vcp_open`) | `espressif/usb_host_vcp` C++ `VCP::open()` service | Only if the radio's VID/PID were unknown/variable across units and you needed automatic driver selection among CP210x/FTDI/CH34x at runtime, AND the project already used C++ exceptions elsewhere. Neither is true here. |
| Fixed VID `0x10C4` + PID `0xEA70` (CP2105) | `CP210X_PID_AUTO` probe loop | Use `CP210X_PID_AUTO` only if hardware validation reveals the FTX-1 reports a different/OEM-customized PID than the three built into the driver (`0xEA60`/`0xEA70`/`0xEA71`) — auto-probe is a safe fallback since it just tries all three known PIDs with the fixed Silicon Labs VID. |
| Interface index 0 for CAT-1 (Enhanced COM) | Interface index 1 or `CDC_HOST_ANY` | Never open with "any" interface on this composite device — it exposes 2 CDC interfaces (CAT-1, CAT-2) plus UAC interfaces; unconstrained matching risks claiming CAT-2 (Standard COM, out of scope per PROJECT.md) or racing the UAC driver for the same USB device. |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| `espressif/usb_host_vcp` (generic C++ VCP factory/service) | Requires `CONFIG_COMPILER_CXX_EXCEPTIONS`; throws `esp_err_t` from constructors; entirely different error-handling idiom than the rest of the codebase (plain `esp_err_t` returns, no C++ exceptions anywhere in `stream_uac.cpp`/`radio_control_*.cpp`) | `espressif/usb_host_cp210x_vcp`'s plain-C `cp210x_vcp_open()` |
| Custom vendor-request CP210x driver (bypassing `cdc_acm_host`) | Duplicates already-published, actively maintained AN571 command implementation for no benefit; higher long-term maintenance burden than a component-manager dependency | `espressif/usb_host_cp210x_vcp` |
| `CDC_HOST_ANY_VID`/`CDC_HOST_ANY_PID` scan-based open (as used for the generic/fallback UAC profile path today) for the FTX-1's CAT port | CP210x reports vendor-class (0xFF) interfaces, not `USB_CLASS_COMM`; the existing scan logic (`cdc_new_dev_cb`'s `bInterfaceClass == USB_CLASS_COMM` hint) will not find it, and blind interface scanning on a 2-CDC+UAC composite device risks claiming the wrong (CAT-2) interface | Known VID (`SILICON_LABS_VID`)/PID (`CP2105_PID`)/interface (`0`) open via `cp210x_vcp_open()`, mirroring the QMX fast-path already in `cdc_try_open()` |

## Stack Patterns by Variant

**If FTX-1 hardware validation shows a non-standard PID (OEM-customized CP210x):**
- Use `cp210x_vcp_open(CP210X_PID_AUTO, 0, &dev_cfg, &handle)` instead of a hardcoded `CP2105_PID`
- Because `CP210X_PID_AUTO` (added in driver v2.2.0) transparently tries all three built-in PIDs (`CP210X_PID`/`CP2105_PID`/`CP2108_PID`) with the fixed Silicon Labs VID — no code restructuring needed, just drop the hardcoded PID constant

**If the FTX-1's CAT-1 port turns out single-endpoint/simple (not actually the dual CP2105):**
- Confirm via `cdc_acm_host_desc_print()` at connection time (same debug helper already used for QMX) before assuming CP2105; if descriptors show a single CDC interface, `CP210X_PID` (`0xEA60`) applies instead of `CP2105_PID`

**Regarding USB FIFO/DMA partitioning with simultaneous UAC:**
- Reuse the existing `UAC_PROFILE_QMX`-style custom FIFO split in `usb_lib_task()` (`stream_uac.cpp`) as the starting point, since CP210x's bulk transfers are non-periodic (`nptx`/bulk-OUT and bulk-IN), functionally equivalent in FIFO terms to QMX's CDC-ACM bulk-OUT CAT traffic — just now also needing bulk-IN capacity (QMX's CAT is TX-only today; FTX-1 needs bidirectional CAT for query replies like `FA;`/`PC;`)
- Because the ESP32-S3 USB-DWC FIFO is a fixed 200-line budget shared across periodic (ISOC, used by UAC IN+OUT) and non-periodic (bulk/control, used by CDC-ACM) traffic; the existing QMX split (`rx=91/nptx=18/ptx=91` lines) already reserves headroom for CDC-ACM bulk-OUT — but it does not currently budget for CDC-ACM bulk-**IN**, since QMX's CAT path never receives. Adding FTX-1 bulk-IN (CAT-1 replies) alongside bidirectional UAC (RX+TX audio, matching QMX's audio topology per PROJECT.md) will likely require re-tuning `nptx_fifo_lines` upward (bulk-IN uses the `rx_fifo_lines` on ESP32-S3's DWC controller — periodic and non-periodic RX share the `rx_fifo_lines` pool) — **this needs empirical validation on hardware**, not just descriptor math, because the DWC FIFO sizing behavior for mixed ISOC+bulk RX is not fully documented by Espressif; flag this specific interaction for hands-on hardware testing during implementation, it is exactly the kind of thing PROJECT.md already calls out as needing re-tuning.

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| `espressif/usb_host_cp210x_vcp@2.2.0` | `espressif/usb_host_cdc_acm@2.2.0` (already pinned in this project) | `usb_host_cp210x_vcp`'s manifest requires `^2.1.0`; project's `^2.2.0` is within range — no bump needed, confirmed via both components' `idf_component.yml`. |
| `espressif/usb_host_cdc_acm@2.2.0`–`2.4.0` | ESP-IDF v5.5.1 | `cdc_acm_host` 2.1.1 added ESP32-H4 support and IDF 6.0 support; 2.3.0 added the `cdc_acm_host_interface.h` vtable (which `usb_host_cp210x_vcp` depends on) and `cdc_acm_host_open_config_t`/USB-address-aware open; all are backward compatible with the plain `cdc_acm_host_open(vid, pid, iface, ...)` form already used for QMX. No breaking changes affect existing QMX code if the component manager resolves to a newer patch/minor within `^2.2.0`. |
| `espressif/usb_host_cp210x_vcp` | `esp_usb::VCP`/`esp_usb::CP210x` (C++ wrapper in `usb_host_vcp`) | NOT a dependency of this project's recommended path — the C++ wrapper is a separate, optional consumer of the same underlying `cp210x_vcp_open()` C function; do not add `usb_host_vcp` unless a future milestone specifically wants driver auto-detection across multiple VCP chip families. |

## Hardware/Domain Notes (feeds PITFALLS.md and FEATURES.md)

- The FTX-1 exposes a **Silicon Labs CP2105 "Dual CP210x"** bridge (VID `0x10C4`, PID `0xEA70`), confirmed by two independent sources describing Windows Device Manager showing "Silicon Labs Dual CP210x USB to UART Bridge: Enhanced COM Port" (CAT-1) and "...: Standard COM Port" (CAT-2) — MEDIUM confidence (web-sourced, not yet hardware-validated per PROJECT.md's own caveat).
- The FTX-1's single USB port is a **composite device**: CP2105 (2 CDC interfaces) + USB Audio Class interfaces, all in one descriptor set — directly analogous to the existing QMX composite descriptor (1 CDC interface + UAC), just with one extra CDC interface to explicitly avoid claiming.
- CP210x interfaces require an explicit **`CP210X_CMD_IFC_ENABLE`** vendor request before use — the driver's `cp210x_vcp_open()` already issues this automatically, so no extra code is needed in the FTX-1 backend, but it explains why raw `cdc_acm_host_open()` (without the CP210x vtable patch) would open the device handle but data would not flow.

## Sources

- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_cp210x_vcp/usb_host_cp210x_vcp.c — full driver source (HIGH confidence, read directly)
- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_cp210x_vcp/idf_component.yml — version + dependency declaration (HIGH)
- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_cp210x_vcp/CHANGELOG.md — version history, confirms 2.1.0 added C API, 2.2.0 added PID autodetection (HIGH)
- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_vcp/include/usb/vcp.hpp and .../usb_host_cp210x_vcp/include/usb/vcp_cp210x.hpp — C++ wrapper source, confirms C++ exceptions requirement (HIGH)
- https://components.espressif.com/components/espressif/usb_host_cp210x_vcp — registry page, version 2.2.0, install command (HIGH)
- Project's own `C:\GitHub\Mini-FT8\managed_components\espressif__usb_host_cdc_acm\include\usb\cdc_acm_host.h`, `idf_component.yml`, and `main\idf_component.yml`/`dependencies.lock` — confirmed currently-pinned `^2.2.0` satisfies `usb_host_cp210x_vcp`'s `^2.1.0` requirement (HIGH, read directly from repo)
- https://github.com/espressif/esp-usb/blob/master/host/class/cdc/usb_host_cdc_acm/CHANGELOG.md — confirms `cdc_acm_host_interface.h` vtable added in 2.3.0, actively maintained through 2.4.0 (2026-04-14) (HIGH)
- WebSearch: "Yaesu FTX-1 USB CP2105 VID PID enhanced standard COM port CAT" — FTX-1 dual-CP210x composite descriptor, CAT-1/CAT-2 port roles, baud rates (MEDIUM — web-sourced, corroborated by Yaesu's own USB Driver Installation Manual references in results, but not yet confirmed against real hardware per PROJECT.md)
- WebSearch: "esp32 usb host CDC-ACM composite device interface claiming order enumeration timing issue" — general ESP32 USB host channel/enumeration constraints (LOW-MEDIUM, general guidance not FTX-1-specific)

---
*Stack research for: ESP-IDF USB host CP210x (Yaesu FTX-1 CAT) integration*
*Researched: 2026-07-04*
