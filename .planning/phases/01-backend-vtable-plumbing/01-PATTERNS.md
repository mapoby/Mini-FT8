# Phase 1: Backend Vtable Plumbing - Pattern Map

**Mapped:** 2026-07-04
**Files analyzed:** 7 (1 new, 6 modified)
**Analogs found:** 7 / 7

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `main/radio_control_ftx1.cpp` (NEW) | service (radio backend impl) | request-response (CAT stub, no I/O) | `main/radio_control_qdx.cpp` | exact (simplest backend, no shared-hardware lifecycle, matches research recommendation) |
| `main/radio_control_backend.h` | config/header (vtable struct + accessor decls) | N/A (declarations only) | itself (existing file, additive edit) | exact |
| `main/radio_control.h` | config/header (dispatch enum + public API decls) | N/A (declarations only) | itself (existing file, additive edit) | exact |
| `main/radio_control.cpp` | service (central dispatcher) | request-response (vtable dispatch) | itself (existing file, additive edit — two switch statements) | exact |
| `main/station_types.h` | model (persisted enum) | CRUD (Station.txt persistence via int cast) | itself (existing file, additive edit) | exact |
| `main/main.cpp` | controller (menu/config glue) | CRUD + request-response (enum translation, persistence, menu cycling) | itself (existing file, 6 additive edit sites) | exact |
| `main/CMakeLists.txt` | config (build) | N/A | itself (existing file, additive `SRCS` entry) | exact |

## Pattern Assignments

### `main/radio_control_ftx1.cpp` (NEW) (service, request-response stub)

**Analog:** `main/radio_control_qdx.cpp` (108 lines, read in full this session)

QDX is the correct analog per RESEARCH.md: it has no shared-hardware enable/disable lifecycle (unlike KH1, which arbitrates UART1 with GPS), and its 8-function shape maps 1:1 onto `radio_control_ops_t`. For Phase 1, FTX-1 needs the same *shape* but every body stubbed (no `stream_uac.h`/`cat_cdc_*` calls — that's Phase 2 CP210x work).

**Imports pattern** (QDX lines 1-9, mirror minus `stream_uac.h` since no I/O yet):
```cpp
#include "radio_control_backend.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
// NOTE: do NOT include "stream_uac.h" in Phase 1 — no CAT/UAC I/O yet (Phase 2 dependency, CAT-01)

static const char* TAG = "RADIO_FTX1";
```

**Core pattern — stub function bodies** (all return safe no-op values, mirroring the shape of QDX's real functions at lines 11-91 but with bodies replaced per RESEARCH.md Pattern 1):
```cpp
static bool ftx1_ready(void) { return false; }  // stub pending Phase 2 hardware bring-up

static esp_err_t ftx1_on_audio_start(void) { return ESP_ERR_INVALID_STATE; }
static esp_err_t ftx1_sync_frequency_mode(int freq_hz) { (void)freq_hz; return ESP_ERR_INVALID_STATE; }
static esp_err_t ftx1_begin_tx(int freq_hz, int tx_base_hz) { (void)freq_hz; (void)tx_base_hz; return ESP_ERR_INVALID_STATE; }
static esp_err_t ftx1_set_tone_hz(float tone_hz) { (void)tone_hz; return ESP_ERR_INVALID_STATE; }
static esp_err_t ftx1_end_tx(void) { return ESP_ERR_INVALID_STATE; }
static esp_err_t ftx1_set_tune(bool enable, int freq_hz, int tone_hz) {
    (void)enable; (void)freq_hz; (void)tone_hz; return ESP_ERR_INVALID_STATE;
}
static esp_err_t ftx1_set_time(int hour, int minute, int second) {
    (void)hour; (void)minute; (void)second; return ESP_ERR_INVALID_STATE;
}
```

**Registration pattern** (QDX lines 93-107, mirror exactly with `k_ops.name = "ftx1"`):
```cpp
static const radio_control_ops_t k_ops = {
    .name = "ftx1",
    .ready = ftx1_ready,
    .on_audio_start = ftx1_on_audio_start,
    .sync_frequency_mode = ftx1_sync_frequency_mode,
    .begin_tx = ftx1_begin_tx,
    .set_tone_hz = ftx1_set_tone_hz,
    .end_tx = ftx1_end_tx,
    .set_tune = ftx1_set_tune,
    .set_time = ftx1_set_time,
};

const radio_control_ops_t* radio_control_ftx1_get_ops(void) {
    return &k_ops;
}
```

**Error handling pattern:** QDX uses `ESP_ERR_INVALID_STATE` for "not ready/no CAT connection" (see `qdx_send_cmd` line 16: `if (!cat_cdc_ready()) return ESP_ERR_INVALID_STATE;`). FTX-1's stubs should return the same sentinel unconditionally in Phase 1 — this is consistent with how `radio_control.cpp`'s dispatcher functions already treat a null/missing hook (lines 54, 60, 66, 72, 78, 84 all `return ESP_ERR_INVALID_STATE` when `!ops->func`).

**No `.h` file needed:** Per RESEARCH.md Anti-Patterns and Open Question 1, do NOT create `radio_control_ftx1.h`. QMX and QDX have no dedicated header — their single accessor is declared directly in `radio_control_backend.h` (lines 19-21, confirmed this session). Only KH1 has an extra header-like public surface (`radio_control_kh1_set_enabled`, `radio_control_kh1_is_enabled`, `radio_control_kh1_diag_test` — `radio_control_backend.h` lines 22-24) because it arbitrates shared UART1 hardware with GPS. FTX-1 has no such contention in Phase 1.

---

### `main/radio_control_backend.h` (header, additive)

**Current state** (full file, 25 lines, read this session):
```cpp
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    const char* name;
    bool (*ready)(void);
    esp_err_t (*on_audio_start)(void);
    esp_err_t (*sync_frequency_mode)(int freq_hz);
    esp_err_t (*begin_tx)(int freq_hz, int tx_base_hz);
    esp_err_t (*set_tone_hz)(float tone_hz);
    esp_err_t (*end_tx)(void);
    esp_err_t (*set_tune)(bool enable, int freq_hz, int tone_hz);
    esp_err_t (*set_time)(int hour, int minute, int second);
} radio_control_ops_t;

const radio_control_ops_t* radio_control_qmx_get_ops(void);
const radio_control_ops_t* radio_control_qdx_get_ops(void);
const radio_control_ops_t* radio_control_kh1_get_ops(void);
void radio_control_kh1_set_enabled(bool enabled);
bool radio_control_kh1_is_enabled(void);
esp_err_t radio_control_kh1_diag_test(char test_key, int freq_hz, int offset_hz, bool* out_fa_sent);
```

**Edit:** append one line after line 20 (`radio_control_qdx_get_ops` declaration), grouping with the plain accessors (not the KH1 extended API):
```cpp
const radio_control_ops_t* radio_control_ftx1_get_ops(void);
```

---

### `main/radio_control.h` (header, additive enum)

**Current state** (full file, 34 lines, read this session):
```cpp
typedef enum {
    RADIO_CONTROL_QMX = 0,
    RADIO_CONTROL_KH1_CAT = 1,
    RADIO_CONTROL_QDX = 2,
} radio_control_backend_t;
```

**Edit:** add `RADIO_CONTROL_FTX1 = 3,` after `RADIO_CONTROL_QDX = 2,`. Note this enum is NOT currently persisted to disk directly (only `RadioType` in station_types.h is saved to Station.txt as `radio=<int>`; `radio_control_backend_t` is a pure runtime/session-local dispatch value derived via `get_radio_profile_binding()`), so value stability here is less critical than for `RadioType`, but follow RESEARCH.md's recommendation of `= 3` (next available) for consistency.

---

### `main/radio_control.cpp` (dispatcher, two switch statements — additive)

**Current state, `current_ops()`** (lines 11-21, full function read this session):
```cpp
static const radio_control_ops_t* current_ops(void) {
    switch (s_backend) {
    case RADIO_CONTROL_KH1_CAT:
        return radio_control_kh1_get_ops();
    case RADIO_CONTROL_QDX:
        return radio_control_qdx_get_ops();
    case RADIO_CONTROL_QMX:
    default:
        return radio_control_qmx_get_ops();
    }
}
```
**Edit:** insert a `case RADIO_CONTROL_FTX1: return radio_control_ftx1_get_ops();` branch before the `case RADIO_CONTROL_QMX:` fallthrough (mirrors the QDX case at line 15-16).

**Current state, `radio_control_backend_name()`** (lines 33-44, full function read this session):
```cpp
const char* radio_control_backend_name(radio_control_backend_t backend) {
    switch (backend) {
    case RADIO_CONTROL_QMX:
        return "qmx";
    case RADIO_CONTROL_KH1_CAT:
        return "kh1_cat";
    case RADIO_CONTROL_QDX:
        return "qdx";
    default:
        return "unknown";
    }
}
```
**Edit:** insert `case RADIO_CONTROL_FTX1: return "ftx1";` (any position among the named cases, before `default:`).

**No other edits needed in this file** — all other dispatcher functions (`radio_control_ready`, `radio_control_on_audio_start`, etc., lines 46-92) call `current_ops()` generically and require zero changes; they will automatically work with the new backend once `current_ops()` is updated.

---

### `main/station_types.h` (persisted enum, additive)

**Current state** (lines 24-33, full enum read this session):
```cpp
// Supported radios. Keep explicit values stable for station.txt radio= saves.
enum class RadioType {
    NONE = 0,
    TRUSDX = 1,
    QMX = 2,
    KH1_USBC = 3,
    KH1 = KH1_USBC, // Backward-compatible alias for old KH1 USB-C mode.
    KH1_MIC = 4,
    QDX = 5,
};
```
**Edit:** append `FTX1 = 6,` after `QDX = 5,`. This value IS persisted to Station.txt (`radio=<int>` line, confirmed via `main.cpp` line 4385: `out << "radio=" << (int)canonical_radio_type(g_radio) << "\n";`) — must stay stable once shipped, per the file's own comment.

---

### `main/main.cpp` (controller, 6 additive edit sites — all read in full this session)

**Analog:** itself; every site below already has a QDX-shaped branch to mirror (QDX was itself a prior single-value addition to a 3-radio switch, same append pattern needed now for a 5-radio -> 6-radio addition).

**Site 1 — `canonical_radio_type()` allow-list** (lines 1546-1550):
```cpp
static RadioType canonical_radio_type(RadioType r) {
  if (r == RadioType::QDX ||
      r == RadioType::KH1_USBC || r == RadioType::KH1_MIC) return r;
  return RadioType::QMX;
}
```
Edit: add `r == RadioType::FTX1 ||` to the `if` condition. **Do this first** — RESEARCH.md Pitfall 1 confirms every other site downstream depends on this allow-list; missing it silently downgrades FTX-1 to QMX everywhere.

**Site 2 — `radio_type_from_saved_int()`** (lines 1568-1580):
```cpp
static RadioType radio_type_from_saved_int(int value) {
  switch (value) {
    case (int)RadioType::KH1_USBC:
      return RadioType::KH1_USBC;
    case (int)RadioType::KH1_MIC:
      return RadioType::KH1_MIC;
    case (int)RadioType::QDX:
      return RadioType::QDX;
    case (int)RadioType::QMX:
    default:
      return RadioType::QMX;
  }
}
```
Edit: insert `case (int)RadioType::FTX1: return RadioType::FTX1;` before the `case (int)RadioType::QMX:` fallthrough.

**Site 3 — `parse_radio_config_value()`** (lines 1582-1608, string-token branch only needs a new token check):
```cpp
  if (token == "QDX") {
    return RadioType::QDX;
  }
  return RadioType::QMX;
```
Edit: insert `if (token == "FTX1" || token == "FTX-1") { return RadioType::FTX1; }` before the final `return RadioType::QMX;`.

**Site 4 — `get_radio_profile_binding()`** (lines 1610-1622):
```cpp
static RadioProfileBinding get_radio_profile_binding(RadioType r) {
  switch (canonical_radio_type(r)) {
    case RadioType::KH1_USBC:
      return {AUDIO_SOURCE_USB_UAC_GENERIC, RADIO_CONTROL_KH1_CAT};
    case RadioType::KH1_MIC:
      return {AUDIO_SOURCE_KH1_MIC, RADIO_CONTROL_KH1_CAT};
    case RadioType::QDX:
      return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QDX};
    case RadioType::QMX:
    default:
      return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_QMX};
  }
}
```
Edit: insert `case RadioType::FTX1: return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_FTX1};` before the `case RadioType::QMX:` fallthrough. NOTE: the `AUDIO_SOURCE_*` value here is a placeholder for Phase 1 (audio backend selection is out of scope until Phase 4, AUDIO-01/02/03); using `AUDIO_SOURCE_QMX_UAC` (same as QDX) keeps `apply_radio_profile_binding()` from erroring, but the planner should flag this line for revisit in Phase 4.

**Site 5 — `radio_name()`** (lines 1624-1633):
```cpp
static const char* radio_name(RadioType r) {
  switch (canonical_radio_type(r)) {
    case RadioType::QMX: return "QMX";
    case RadioType::QDX: return "QDX";
    case RadioType::KH1_USBC: return "KH1-USBC";
    case RadioType::KH1_MIC: return "KH1-MIC";
    default: break;
  }
  return "None";
}
```
Edit: insert `case RadioType::FTX1: return "FTX-1";` among the other cases (before `default:`).

**Site 6 — menu cycle switch** (lines 5474-5488, inside the `c == '3'` keyboard handler starting line 5470):
```cpp
                  switch (canonical_radio_type(g_radio)) {
                    case RadioType::QMX:
                      g_radio = RadioType::QDX;
                      break;
                    case RadioType::QDX:
                      g_radio = RadioType::KH1_USBC;
                      break;
                    case RadioType::KH1_USBC:
                      g_radio = RadioType::KH1_MIC;
                      break;
                    case RadioType::KH1_MIC:
                    default:
                      g_radio = RadioType::QMX;
                      break;
                  }
```
Edit: change `case RadioType::KH1_MIC:` to fall through to a new `g_radio = RadioType::FTX1; break;`, and add `case RadioType::FTX1: default: g_radio = RadioType::QMX; break;` as the new terminal case (closes the cycle QMX -> QDX -> KH1_USBC -> KH1_MIC -> FTX1 -> QMX):
```cpp
                    case RadioType::KH1_MIC:
                      g_radio = RadioType::FTX1;
                      break;
                    case RadioType::FTX1:
                    default:
                      g_radio = RadioType::QMX;
                      break;
```

**No changes needed:** `is_kh1_radio()` (line 1552), `radio_type_uses_display_only()` (line 1557) — both intentionally radio-agnostic/KH1-specific and correctly exclude FTX-1. `core_api.h`'s `CoreRadioType` (main/core_api.h, main/core_api.cpp lines ~152-160) — confirmed by RESEARCH.md (grepped, zero call sites for `core_cmd_set_radio`) — leave unchanged per Pitfall 3/Assumption A2.

---

### `main/CMakeLists.txt` (build config, additive)

**Current state** (full file, 19 lines, read this session):
```cmake
idf_component_register(
    SRCS "main.cpp"
         "autoseq.cpp"
         "audio_source.cpp"
         "core_api.cpp"
         "gps.cpp"
         "radio_control.cpp"
         "radio_control_qmx.cpp"
         "radio_control_qdx.cpp"
         "radio_control_kh1_cat.cpp"
         "ft8_audio_pipeline.cpp"
         "stream_uac.cpp"
         "stream_mic.cpp"
         "dds_q15.cpp"
         "resample.cpp"
    INCLUDE_DIRS "."
    REQUIRES ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib usb_host_uac usb_host_cdc_acm storage_service external_rtc efuse
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
```
**Edit:** insert `"radio_control_ftx1.cpp"` after `"radio_control_kh1_cat.cpp"` (line 10) in the `SRCS` list. No `REQUIRES` change needed in Phase 1 (no new component dependency — `usb_host_cp210x_vcp` is Phase 2 per RESEARCH.md).

**Note on `-Werror`:** this project compiles with `-Werror` (line 19), so unused-parameter warnings in stub functions matter. Existing convention: all backend files (QMX/QDX/KH1) use `(void)param;` casts for unused parameters — the FTX-1 stub bodies above already follow this (`(void)freq_hz; (void)tx_base_hz;` etc.), consistent with `-Wno-unused-parameter` also being set globally, but `(void)` casts remain the established local idiom in these backend files.

---

## Shared Patterns

### Vtable registration (single source of truth: no dynamic registration)
**Source:** `main/radio_control_qdx.cpp` lines 93-107, `main/radio_control_backend.h` lines 7-24
**Apply to:** `radio_control_ftx1.cpp` (new file)
Every backend is a file-static `const radio_control_ops_t k_ops` plus one accessor function. No table, no self-registration, no dynamic lookup — mirror exactly.

### Central dispatch switch, always with `default:` fallback to QMX
**Source:** `main/radio_control.cpp` lines 11-21, 33-44
**Apply to:** `radio_control.cpp` (existing file being extended)
Both switches in this file use `default:` (not exhaustive-enum warnings) — RESEARCH.md Pitfall 2 confirms this means missing a case compiles silently. Manually verify both switches got the new case; do not rely on `-Wswitch`.

### Two decoupled enums, one translation function
**Source:** `main/station_types.h` lines 24-33 (`RadioType`, persisted), `main/radio_control.h` lines 11-15 (`radio_control_backend_t`, runtime dispatch), `main/main.cpp` lines 1610-1622 (`get_radio_profile_binding`, the single translator)
**Apply to:** All three files together — a new `RadioType::FTX1` value is meaningless to the CAT dispatcher until `get_radio_profile_binding()` maps it to `RADIO_CONTROL_FTX1`.

### Persisted-enum stability discipline
**Source:** `main/station_types.h` line 24 comment: `// Keep explicit values stable for station.txt radio= saves.`
**Apply to:** `RadioType::FTX1 = 6` — must never be renumbered once shipped, since `main.cpp` line 4385 persists it as a raw int in Station.txt.

## No Analog Found

None — every file in scope has an exact analog already present in the codebase (this phase is a pure mechanical pattern extension, per RESEARCH.md's own assessment).

## Metadata

**Analog search scope:** `main/radio_control*.{h,cpp}`, `main/station_types.h`, `main/main.cpp` (radio-enumeration sites only), `main/CMakeLists.txt`
**Files scanned:** 7 (all read in full or targeted-range this session; no file exceeded 2,000 lines requiring partial reads except `main.cpp`, which was targeted via grep + offset/limit reads)
**Pattern extraction date:** 2026-07-04
