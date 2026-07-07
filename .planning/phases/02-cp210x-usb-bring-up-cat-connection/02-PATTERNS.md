# Phase 2: CP210x USB Bring-up & CAT Connection - Pattern Map

**Mapped:** 2026-07-05
**Files analyzed:** 7 (4 modified, plus 3 supporting files read for context/analogs)
**Analogs found:** 7 / 7 (all changes are same-file/sibling-function extensions of existing patterns — no cross-codebase search was needed; the closest "analog" for every new function is the existing sibling function in the same file)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `main/stream_uac.h` | config/enum (header) | N/A | `main/audio_source.h` (sibling enum) | exact |
| `main/stream_uac.cpp` — `cp210x_try_open()` (new) | utility / USB device-open | request-response (USB control transfer) | `cdc_try_open()` in same file, `main/stream_uac.cpp:198-272` | exact |
| `main/stream_uac.cpp` — `cdc_try_open()` guard (modify) | utility | request-response | itself (defensive one-line addition) | exact |
| `main/stream_uac.cpp` — `usb_lib_task()` install-gate (modify) | service/driver-lifecycle | event-driven | itself, `main/stream_uac.cpp:379-396` | exact |
| `main/stream_uac.cpp` — `uac_lib_task()` dispatch + `uac_on_block_processed()` (modify) | event-driven dispatcher | event-driven | itself, `main/stream_uac.cpp:568-571`, `817-822` | exact |
| `main/audio_source.h` / `.cpp` (modify) | service (backend router) | CRUD-like (enum → profile mapping) | itself, `AUDIO_SOURCE_USB_UAC_GENERIC` branch, `main/audio_source.cpp:14-16,41-45` | exact |
| `main/main.cpp` — `get_radio_profile_binding()` (modify) | config/routing | request-response | itself, `RadioType::QDX` case, `main/main.cpp:1615-1631` | exact |
| `main/radio_control_ftx1.cpp` (modify, CAT-only fields) | controller/backend (radio_control_ops_t vtable) | request-response | `main/radio_control_qmx.cpp` (full file) | exact |
| `main/idf_component.yml` (modify) | config (component manifest) | N/A | existing `espressif/usb_host_cdc_acm: ^2.2.0` entry, line 22 | exact |
| `main/CMakeLists.txt` (modify) | config (build) | N/A | existing `REQUIRES ... usb_host_cdc_acm ...` line 18 | exact |

## Pattern Assignments

### `main/stream_uac.h` (enum extension)

**Analog:** own file, `audio_source.h`'s parallel enum (both are plain C enums with `= N` sibling values)

**Current enum** (lines 29-32):
```c
typedef enum {
    UAC_PROFILE_QMX = 0,
    UAC_PROFILE_GENERIC_USB = 1,
} uac_stream_profile_t;
```
**Change:** append `UAC_PROFILE_FTX1 = 2,`.

**Analog in `audio_source.h`** (lines 10-14, same pattern, second enum needing a matching new value):
```c
typedef enum {
    AUDIO_SOURCE_QMX_UAC = 0,
    AUDIO_SOURCE_USB_UAC_GENERIC = 1,
    AUDIO_SOURCE_KH1_MIC = 2,
} audio_source_backend_t;
```
**Change:** append `AUDIO_SOURCE_FTX1_CP210X = 3,`.

---

### `main/stream_uac.cpp` — new `cp210x_try_open()` function

**Analog:** `cdc_try_open()`, `main/stream_uac.cpp:198-272` (exact structural sibling — same throttle guard, same handle assignment, same post-open side effects)

**Imports to add** (top of file, alongside existing `#include "usb/cdc_acm_host.h"` at line 18):
```cpp
#include "usb/vcp_cp210x.h"
```

**Guard + throttle pattern to copy** (`cdc_try_open()` lines 198-205):
```cpp
static void cdc_try_open(void) {
    if (!s_cdc_installed) return;
    if (s_cdc_handle) return;

    // Throttle attempts
    int64_t now_ms = rtc_now_ms();
    if (s_cdc_last_attempt_ms != 0 && (now_ms - s_cdc_last_attempt_ms) < 1000) return;
    s_cdc_last_attempt_ms = now_ms;
```
Reuse `s_cdc_last_attempt_ms`/`s_cdc_handle`/`s_cdc_iface` (all already file-scope statics at lines 86, 112, 114) — no new state variables needed for the handle itself.

**dev_cfg construction pattern to copy** (`cdc_try_open()` lines 207-214), noting the one required difference (RX buffer, per RESEARCH.md Assumption A3):
```cpp
cdc_acm_host_device_config_t dev_cfg = {
    .connection_timeout_ms = 1000,
    .out_buffer_size = 64,
    .in_buffer_size = 256,   // FTX-1 needs RX for query replies (Phase 3); QMX is TX-only (0)
    .event_cb = cdc_event_cb,   // reuse existing callback unchanged
    .data_cb = NULL,
    .user_arg = NULL,
};
```

**Open + success-path pattern to copy** (`cdc_try_open()`'s QMX branch, lines 219-232 — same shape: open call, `s_cdc_handle =`, `s_cdc_iface =`, log, `cdc_acm_host_desc_print(handle)`, set `g_cdc_initial_sync_pending = true`):
```cpp
if (s_profile == UAC_PROFILE_QMX) {
    cdc_acm_dev_hdl_t handle = NULL;
    esp_err_t err = cdc_acm_host_open(k_qmx_vid, k_qmx_pid, 0, &dev_cfg, &handle);
    if (err == ESP_OK) {
        s_cdc_handle = handle;
        s_cdc_iface = 0;
        ESP_LOGI(TAG, "CDC-ACM opened (QMX iface 0, VID 0x%04x PID 0x%04x)", k_qmx_vid, k_qmx_pid);
        cdc_acm_host_desc_print(handle);
        g_cdc_initial_sync_pending = true;
        return;
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "CDC open QMX iface 0 failed: %s", esp_err_to_name(err));
    }
}
```
For `cp210x_try_open()`, replace `cdc_acm_host_open(...)` with `cp210x_vcp_open(CP2105_PID, /*interface_idx=*/0, &dev_cfg, &handle)` (verified signature from RESEARCH.md's Standard Stack section, sourced from `usb/vcp_cp210x.h`) and add the CAT-03 line-state call immediately after success, before `return`:
```cpp
esp_err_t line_err = cdc_acm_host_set_control_line_state(handle, /*dtr=*/false, /*rts=*/false);
ESP_LOGI(TAG, "CP210x opened (FTX-1 CAT-1 iface 0); RTS/DTR deassert: %s",
         esp_err_to_name(line_err));
```

**Constants needed:** `CP2105_PID` comes from `usb/vcp_cp210x.h` (`SILICON_LABS_VID=0x10C4, CP210X_PID=0xEA60, CP2105_PID=0xEA70, CP2108_PID=0xEA71, CP210X_PID_AUTO=0` — confirmed in RESEARCH.md Standard Stack). Do NOT redeclare `k_qmx_vid`/`k_qmx_pid`-style local constants for these; they are already provided by the header include.

**No hint-scan / no blind interface loop:** `cp210x_try_open()` must NOT copy `cdc_try_open()`'s lines 234-266 (hinted-interface fallback + `for (int iface = 0; iface < max_iface_scan; ++iface)` blind loop). FTX-1's VID/PID/interface are known constants; the scan exists only for QMX's unknown-CDC-interface case.

**Error/not-found handling to copy** (`cdc_try_open()` lines 229-231 style — only warn on unexpected errors, stay silent on `ESP_ERR_NOT_FOUND` since that just means "device not plugged in yet"):
```cpp
if (err != ESP_OK) {
    if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "CP210x open (FTX-1 CAT-1) failed: %s", esp_err_to_name(err));
    }
    return;
}
```

**Forward declaration:** add `static void cp210x_try_open(void);` next to `static void cdc_try_open(void);` at line 130.

---

### `main/stream_uac.cpp` — hard-gate `cdc_try_open()` (CAT-02)

**Analog:** itself; one-line addition at function top

**Current** (`main/stream_uac.cpp:198-201`):
```cpp
static void cdc_try_open(void) {
    if (!s_cdc_installed) return;
    if (s_cdc_handle) return;
```
**Change — add third guard line:**
```cpp
static void cdc_try_open(void) {
    if (!s_cdc_installed) return;
    if (s_cdc_handle) return;
    if (s_profile != UAC_PROFILE_QMX) return;   // CAT-02 hard boundary
```
This makes the now-dead `s_profile == UAC_PROFILE_QMX` branch inside the function (lines 219-232) technically redundant but harmless to leave as-is (defense-in-depth, matches RESEARCH.md's exact recommendation) — do not delete it in this pass, only add the guard.

---

### `main/stream_uac.cpp` — `usb_lib_task()` CDC-ACM install gate (broaden condition)

**Analog:** itself, `main/stream_uac.cpp:379-396` and the FIFO-partition condition immediately above it at `main/stream_uac.cpp:353-366`

**Current install gate** (lines 379-396):
```cpp
if (s_profile == UAC_PROFILE_QMX) {
    // Install CDC-ACM driver for QMX/QDX CAT control before starting class drivers.
    const cdc_acm_host_driver_config_t cdc_cfg = {
        .driver_task_stack_size = 3072,
        .driver_task_priority = 4,
        .xCoreID = 0,
        .new_dev_cb = cdc_new_dev_cb,
    };
    err = cdc_acm_host_install(&cdc_cfg);
    if (err == ESP_OK) {
        s_cdc_installed = true;
        ESP_LOGI(TAG, "CDC-ACM driver installed");
    } else {
        ESP_LOGW(TAG, "CDC-ACM driver install failed: %s", esp_err_to_name(err));
    }
} else {
    ESP_LOGI(TAG, "CDC-ACM driver skipped profile=%s", profile_name(s_profile));
}
```
**Change:** widen the condition to `if (s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1) {` — exact text given in RESEARCH.md's Code Examples section. Everything inside the `if` block is unchanged (same `cdc_new_dev_cb`, same driver config — CDC-ACM base driver install is profile-agnostic; only the FIFO partitioning above it at lines 353-366 stays QMX-only per Pitfall 1/AUDIO-03, deferred to Phase 4). Do NOT touch the `host_config.fifo_settings_custom` block — leave the `if (s_profile == UAC_PROFILE_QMX)` condition there unchanged.

**Cleanup path** (lines 411-416) already keys off `s_cdc_installed`, not `s_profile`, so no change needed there — `cdc_acm_host_uninstall()` will correctly run for FTX-1 sessions too once `s_cdc_installed` is set true above.

---

### `main/stream_uac.cpp` — `uac_lib_task()` dispatch by profile

**Analog:** itself, the mic-connected handler's existing QMX-only call site (line 568-571) and the TX_CONNECTED handler's existing profile-gate pattern (lines 614-621, which already demonstrates the "if not this profile, skip" idiom to mirror)

**Current call site inside `RX_CONNECTED` handler** (lines 568-571):
```cpp
if (s_profile == UAC_PROFILE_QMX) {
    // Try to open companion CDC-ACM interface (CAT).
    cdc_try_open();
}
```
**Change — dispatch pattern to add:**
```cpp
if (s_profile == UAC_PROFILE_QMX) {
    cdc_try_open();
} else if (s_profile == UAC_PROFILE_FTX1) {
    cp210x_try_open();
}
```

**`profile_name()` also needs a case** (lines 160-169, currently only QMX/GENERIC_USB/default):
```cpp
static const char* profile_name(uac_stream_profile_t profile) {
    switch (profile) {
    case UAC_PROFILE_QMX:
        return "qmx_uac";
    case UAC_PROFILE_GENERIC_USB:
        return "usb_uac_generic";
    default:
        return "unknown";
    }
}
```
Add `case UAC_PROFILE_FTX1: return "ftx1_cp210x";` before `default`.

**`uac_on_block_processed()` dispatch** (currently unconditional QMX-shaped retry, lines 817-822):
```cpp
static void uac_on_block_processed(void* ctx) {
    (void)ctx;
    if (!s_cdc_handle) {
        cdc_try_open();
    }
}
```
**Change:** replace the unconditional `cdc_try_open()` with the same profile dispatch as above:
```cpp
static void uac_on_block_processed(void* ctx) {
    (void)ctx;
    if (s_cdc_handle) return;
    if (s_profile == UAC_PROFILE_QMX) {
        cdc_try_open();
    } else if (s_profile == UAC_PROFILE_FTX1) {
        cp210x_try_open();
    }
}
```
Note: `cdc_try_open()`'s own new CAT-02 guard (`if (s_profile != UAC_PROFILE_QMX) return;`) would already make the old unconditional call harmless for FTX-1, but this dispatch is what actually gets `cp210x_try_open()` retried if the first RX_CONNECTED-time attempt raced the device attach — mirrors the existing retry rationale for QMX.

---

### `main/audio_source.h` / `main/audio_source.cpp` (backend enum + routing)

**Analog:** itself — the existing `AUDIO_SOURCE_USB_UAC_GENERIC` branch is the direct template for the new `AUDIO_SOURCE_FTX1_CP210X` branch

**Enum extension** (`audio_source.h:10-14`):
```c
typedef enum {
    AUDIO_SOURCE_QMX_UAC = 0,
    AUDIO_SOURCE_USB_UAC_GENERIC = 1,
    AUDIO_SOURCE_KH1_MIC = 2,
} audio_source_backend_t;
```
Add `AUDIO_SOURCE_FTX1_CP210X = 3,`.

**`backend_is_uac()` update** (`audio_source.cpp:14-16`):
```cpp
static bool backend_is_uac(audio_source_backend_t backend) {
    return backend == AUDIO_SOURCE_QMX_UAC || backend == AUDIO_SOURCE_USB_UAC_GENERIC;
}
```
Change to: `return backend == AUDIO_SOURCE_QMX_UAC || backend == AUDIO_SOURCE_USB_UAC_GENERIC || backend == AUDIO_SOURCE_FTX1_CP210X;` (FTX-1 is UAC-family for `audio_source_start()`'s dispatch purposes even though Phase 2 doesn't wire real audio yet — RESEARCH.md Open Question 2 explicitly anticipates this call still firing and failing benignly).

**`audio_source_backend_name()` switch** (`audio_source.cpp:26-37`) needs a new case, following the existing pattern exactly:
```cpp
case AUDIO_SOURCE_KH1_MIC:
    return "kh1_mic";
default:
    return "unknown";
```
Add `case AUDIO_SOURCE_FTX1_CP210X: return "ftx1_cp210x";` before `default`.

**`audio_source_start()` profile-mapping branch** (`audio_source.cpp:39-47`, RESEARCH.md's Pattern 1 gives the exact diff already):
```cpp
uac_stream_profile_t profile = UAC_PROFILE_QMX;
if (s_backend == AUDIO_SOURCE_USB_UAC_GENERIC) {
    profile = UAC_PROFILE_GENERIC_USB;
} else if (s_backend == AUDIO_SOURCE_FTX1_CP210X) {
    profile = UAC_PROFILE_FTX1;
}
```

---

### `main/main.cpp` — `get_radio_profile_binding()` (fix Phase-1 placeholder)

**Analog:** itself — the `RadioType::QDX` case immediately above (`main.cpp:1621-1622`) is the exact one-line-return shape to copy

**Current** (`main.cpp:1615-1631`, FTX1 case at 1623-1626):
```cpp
static RadioProfileBinding get_radio_profile_binding(RadioType r) {
  switch (canonical_radio_type(r)) {
    case RadioType::KH1_USBC:
      return {AUDIO_SOURCE_USB_UAC_GENERIC, RADIO_CONTROL_KH1_CAT};
    case RadioType::KH1_MIC:
      return {AUDIO_SOURCE_KH1_MIC, RADIO_CONTROL_KH1_CAT};
    case RadioType::QDX:
      return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QDX};
    case RadioType::FTX1:
      // AUDIO_SOURCE_QMX_UAC is a Phase-1 placeholder; revisit in Phase 4 (AUDIO-01/02/03)
      // once the FTX-1's real USB audio profile is validated against hardware.
      return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_FTX1};
    case RadioType::QMX:
    default:
      return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QMX};
  }
}
```
**Change — FTX1 case becomes:**
```cpp
    case RadioType::FTX1:
      return {AUDIO_SOURCE_FTX1_CP210X, RADIO_CONTROL_FTX1};
```
(Delete the Phase-1 placeholder comment; it is now resolved.) No other case in this function changes. `RADIO_CONTROL_FTX1` itself is unchanged — Phase 1 already wired this correctly; only the audio-backend half of the pair was wrong.

---

### `main/radio_control_ftx1.cpp` (CAT wiring: `ready()` + control-line deassert)

**Analog:** `main/radio_control_qmx.cpp` (full file) — same `radio_control_ops_t` vtable shape, same `cat_cdc_ready()`/`cat_cdc_send()` reuse

**Imports pattern to copy** (`radio_control_qmx.cpp:1-11`, note the QMX file's comment that must be removed from the FTX1 file):
```cpp
#include "radio_control_backend.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "stream_uac.h"

static const char* TAG = "RADIO_QMX";
```
Current FTX-1 file (`radio_control_ftx1.cpp:1-9`) has an explicit comment blocking this include for Phase 1 (`// NOTE: do NOT include "stream_uac.h" in Phase 1 -- no CAT/UAC I/O yet (Phase 2 dependency, CAT-01)`). Phase 2 must delete that comment and add `#include "stream_uac.h"`, matching QMX's include block exactly (`<cstdio>`/`<cstring>` already present in FTX1 file; only `stream_uac.h` is new for this phase — CAT-only fields, not the full sync/PTT set which is Phase 3 scope).

**`ready()` pattern to copy** (`radio_control_qmx.cpp:13-15`):
```cpp
static bool qmx_ready(void) {
    return cat_cdc_ready();
}
```
FTX-1's current stub (`radio_control_ftx1.cpp:11-14`):
```cpp
static bool ftx1_ready(void) {
    ESP_LOGI(TAG, "FTX-1 backend selected (stub, not yet implemented)");
    return false;  // stub pending Phase 2 hardware bring-up
}
```
**Change to:**
```cpp
static bool ftx1_ready(void) {
    return cat_cdc_ready();
}
```
This is the ONLY vtable hook Phase 2 needs to make real per CAT-01/02/03's scope (frequency/mode sync, PTT, tune are explicitly Phase 3 — SYNC-01/02/03, PTT-01/02 in REQUIREMENTS.md traceability). All other `ftx1_*` stubs (`on_audio_start`, `sync_frequency_mode`, `begin_tx`, `set_tone_hz`, `end_tx`, `set_tune`, `set_time`) remain `ESP_ERR_INVALID_STATE` stubs in this phase — do not implement CAT command encoding yet; that is Phase 3's SYNC/PTT scope. The `k_ops` static struct (`radio_control_ftx1.cpp:54-64`) and `radio_control_ftx1_get_ops()` accessor need no changes.

**RTS/DTR deassertion (CAT-03) does NOT belong in this file.** Per RESEARCH.md Pattern 2, the deassert call happens once, immediately after `cp210x_vcp_open()` succeeds inside `stream_uac.cpp`'s `cp210x_try_open()` — not in `radio_control_ftx1.cpp`. This file's `ready()` only needs to report the already-established connection state via `cat_cdc_ready()`, identical to QMX.

---

### `main/idf_component.yml` (dependency addition)

**Analog:** itself — existing `espressif/usb_host_cdc_acm: ^2.2.0` entry, line 22

**Current** (lines 17-24):
```yaml
  lvgl/lvgl: ^8.3.9
  # USB Host UAC for audio streaming
  espressif/usb_host_uac: ^1.2.0
  espressif/usb: '*'
  # USB Host CDC for CAT
  espressif/usb_host_cdc_acm: ^2.2.0
  # Cardputer-Adv ES8311 mic capture
  espressif/esp_codec_dev: ^1.5.9
```
**Change — insert new dependency line after the CDC-ACM entry, following the existing one-line-comment-then-dependency style:**
```yaml
  # USB Host CDC for CAT
  espressif/usb_host_cdc_acm: ^2.2.0
  # CP210x CAT for FTX-1 (CAT-1 / Enhanced COM only)
  espressif/usb_host_cp210x_vcp: ^2.2.0
  # Cardputer-Adv ES8311 mic capture
  espressif/esp_codec_dev: ^1.5.9
```

---

### `main/CMakeLists.txt` (REQUIRES addition)

**Analog:** itself — existing `REQUIRES` line 18

**Current** (line 18):
```cmake
    REQUIRES ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib usb_host_uac usb_host_cdc_acm storage_service external_rtc efuse
```
**Change — append `usb_host_cp210x_vcp` after `usb_host_cdc_acm` (matches existing ordering: UAC then CDC then app components):**
```cmake
    REQUIRES ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib usb_host_uac usb_host_cdc_acm usb_host_cp210x_vcp storage_service external_rtc efuse
```
No new `.cpp` needs to be added to `SRCS` — `usb_host_cp210x_vcp` is header+prebuilt-pattern consumed directly from `stream_uac.cpp` via `#include "usb/vcp_cp210x.h"`, same as `usb_host_cdc_acm` today (confirmed in RESEARCH.md Standard Stack section).

---

## Shared Patterns

### CDC-ACM handle reuse (no new adapter boundary)
**Source:** `main/stream_uac.cpp:1071-1080` (`cat_cdc_ready()` / `cat_cdc_send()`)
**Apply to:** `radio_control_ftx1.cpp` unchanged; `cp210x_try_open()`'s success path assigns to the same `s_cdc_handle` static QMX uses
```cpp
bool cat_cdc_ready(void) {
    return s_cdc_handle != NULL;
}

esp_err_t cat_cdc_send(const uint8_t* data, size_t len, uint32_t timeout_ms) {
    if (!s_cdc_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    return cdc_acm_host_data_tx_blocking(s_cdc_handle, data, len, timeout_ms);
}
```
Because `cdc_acm_dev_hdl_t` is identical regardless of whether it came from `cdc_acm_host_open()` (QMX) or `cp210x_vcp_open()` (FTX-1), no new functions are needed here for Phase 2 — only `ready()` in `radio_control_ftx1.cpp` needs to start calling the existing `cat_cdc_ready()`.

### Profile-gated dispatch idiom
**Source:** `main/stream_uac.cpp:614-621` (TX_CONNECTED handler's existing "if not this profile, skip and log" shape)
**Apply to:** `cdc_try_open()`'s new guard, `usb_lib_task()`'s install condition, `uac_lib_task()`'s two call sites, `profile_name()`
```cpp
if (s_profile != UAC_PROFILE_QMX) {
    ESP_LOGI(TAG, "Speaker connected ignored profile=%s addr=%u iface=%u", ...);
    continue;
}
```
This is the project's established idiom for "this profile does not participate in this code path" — the CAT-02 guard in `cdc_try_open()` and the dispatch branches in `uac_lib_task()` should read the same way (explicit profile check, early return/skip, log the profile name via `profile_name()`).

### Error handling — `ESP_ERR_NOT_FOUND` is silent, everything else warns
**Source:** `main/stream_uac.cpp:229-231`, `246-249`, `263-265` (three repetitions of the same idiom in `cdc_try_open()`)
**Apply to:** `cp210x_try_open()`
```cpp
} else if (err != ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "CDC open QMX iface 0 failed: %s", esp_err_to_name(err));
}
```
"Device not attached yet" is expected/frequent (throttled retry every 1s) and must not spam warnings; any other `esp_err_t` is unexpected and should warn with `esp_err_to_name()`.

## No Analog Found

None — every file/change in this phase's scope has a direct, exact-match analog already in the codebase (all are extensions of an existing sibling code path: QMX's CDC-ACM open flow, QDX's profile-binding case, or the existing `usb_host_cdc_acm` manifest/CMake entries).

## Metadata

**Analog search scope:** `main/stream_uac.cpp`, `main/stream_uac.h`, `main/audio_source.cpp`, `main/audio_source.h`, `main/main.cpp` (get_radio_profile_binding + call sites), `main/radio_control_ftx1.cpp`, `main/radio_control_qmx.cpp`, `main/idf_component.yml`, `main/CMakeLists.txt`
**Files scanned:** 9 (all read in full via research + this pass; no large-file truncation needed, largest file `stream_uac.cpp` is 1081 lines, read in one pass)
**Pattern extraction date:** 2026-07-05
