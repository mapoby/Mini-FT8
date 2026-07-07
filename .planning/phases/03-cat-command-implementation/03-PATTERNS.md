# Phase 3: CAT Command Implementation - Pattern Map

**Mapped:** 2026-07-06
**Files analyzed:** 2 (1 modified, 1 touched at one call site)
**Analogs found:** 2 / 2

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `main/radio_control_ftx1.cpp` | radio backend (vtable-dispatched controller, CAT command encoder) | request-response (fire-and-forget ASCII command over already-open serial handle) | `main/radio_control_qdx.cpp` (structural/TX-audio-model match) + `main/radio_control_kh1_cat.cpp` (post-TX restore pattern, lines 161-185) + `main/radio_control_qmx.cpp` (send_cmd wrapper shape) | exact (role) / partial (per-command syntax differs — Yaesu vs Kenwood dialect) |
| `main/stream_uac.cpp` (`cp210x_try_open()`, one new call) | transport/config (USB CDC line-coding setup) | request-response (one-shot vendor-request configuration call at device-open time) | same function, existing RTS/DTR deassert call immediately above the insertion point (lines 310-315) | exact (same file, same function, adjacent pattern) |

No new files are created this phase — `radio_control_ftx1.cpp` already exists (Phase 1 stub) and is being filled in; `stream_uac.cpp` gets one additional call inside an existing function.

## Pattern Assignments

### `main/radio_control_ftx1.cpp` (radio backend, request-response)

**Current state (Phase 1 stub, to be replaced):** `C:\GitHub\Mini-FT8\main\radio_control_ftx1.cpp` lines 1-65 — every op except `ready()` returns `ESP_ERR_INVALID_STATE` unconditionally. All function signatures and the `k_ops`/`radio_control_ftx1_get_ops()` vtable wiring (lines 51-65) are final and must NOT change — only the function bodies change.

**Analogs:** `main/radio_control_qdx.cpp` (primary — audio-tone-TX model match), `main/radio_control_kh1_cat.cpp` (post-TX restore pattern), `main/radio_control_qmx.cpp` (send_cmd wrapper shape, same as QDX's).

**Imports pattern** — identical across all three analogs, copy verbatim (`radio_control_qdx.cpp:1-9`):
```cpp
#include "radio_control_backend.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "stream_uac.h"

static const char* TAG = "RADIO_QDX";   // -> "RADIO_FTX1" for the new file
```
FTX-1's version needs no `<cmath>` (unlike QMX, which needs `floorf`/`lrintf` for its `TA` tone-fraction math) since `ftx1_set_tone_hz()` is a no-op — do not add `<cmath>` speculatively.

**send_cmd wrapper pattern** (`radio_control_qmx.cpp:17-20`, identical in QDX at `radio_control_qdx.cpp:15-19`) — copy this exact shape, rename `qmx_send_cmd`/`qdx_send_cmd` to `ftx1_send_cmd`:
```cpp
static esp_err_t qmx_send_cmd(const char* cmd, uint32_t timeout_ms) {
    if (!cat_cdc_ready()) return ESP_ERR_INVALID_STATE;
    return cat_cdc_send(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), timeout_ms);
}
```
Transport call (`cat_cdc_ready()`/`cat_cdc_send()`) is unchanged, handle-type-agnostic, defined in `main/stream_uac.cpp:1128-1137`:
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

**Core "sync frequency/mode" pattern — DO NOT copy field widths, only the call shape** (`radio_control_qdx.cpp:21-36`):
```cpp
static esp_err_t qdx_sync_frequency_mode(int freq_hz) {
    esp_err_t err = qdx_send_cmd("MD6;", 200);
    if (err != ESP_OK) return err;
    err = qdx_send_cmd("FR0;", 200);
    if (err != ESP_OK) return err;
    err = qdx_send_cmd("FT0;", 200);
    if (err != ESP_OK) return err;

    char command[32];
    snprintf(command, sizeof(command), "FA%011d;", freq_hz);   // <-- 11-digit Kenwood field; FTX-1 uses 9 digits, no FR0/FT0
    err = qdx_send_cmd(command, 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QDX sync ok freq=%d", freq_hz);
    }
    return err;
}
```
**FTX-1 must instead use** the verified field widths from RESEARCH.md Pattern 1 (`FA%09d;`, then `MD0C;` — no `FR0;`/`FT0;`, those are QDX/QMX-specific VFO-select commands with no FTX-1 equivalent needed for this phase):
```cpp
static esp_err_t ftx1_sync_frequency_mode(int freq_hz) {
    s_rx_freq_hz = freq_hz;   // cache for restore, mirrors KH1's s_rx_freq10 caching

    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", freq_hz);
    esp_err_t err = ftx1_send_cmd(fa, 200);
    if (err != ESP_OK) return err;

    err = ftx1_send_cmd("MD0C;", 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "FTX-1 sync ok freq=%d mode=DATA-U", freq_hz);
    }
    return err;
}
```

**Core "begin_tx / set_tone_hz / end_tx" pattern — copy QDX's shape exactly, not QMX's or KH1's** (`radio_control_qdx.cpp:38-63`):
```cpp
static esp_err_t qdx_begin_tx(int freq_hz, int tx_base_hz) {
    (void)freq_hz;
    (void)tx_base_hz;

    esp_err_t err = qdx_send_cmd("MD6;", 200);   // <-- FTX-1 must NOT resend MD; already synced (SYNC-02)
    if (err != ESP_OK) return err;
    err = qdx_send_cmd("TX;", 200);              // <-- FTX-1 uses "TX1;" (PTT-01), not "TX;"
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QDX TX start");
    }
    return err;
}

static esp_err_t qdx_set_tone_hz(float tone_hz) {
    (void)tone_hz;
    return ESP_OK;   // <-- copy verbatim for FTX-1; audio-tone TX rides UAC OUT (Phase 4)
}

static esp_err_t qdx_end_tx(void) {
    uac_tx_end();                                // <-- PTT-02 drain-before-unkey; copy verbatim
    esp_err_t err = qdx_send_cmd("RX;", 200);    // <-- FTX-1 uses "TX0;" then restore (SYNC-03), not bare "RX;"
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "QDX TX stop");
    }
    return err;
}
```
**Explicit anti-pattern warning:** REQUIREMENTS.md attributes the drain-before-unkey pattern to QMX; the actual code lives in QDX (`qdx_end_tx()` above). QMX's `qmx_end_tx()` (`radio_control_qmx.cpp:83-90`) has **no** `uac_tx_end()` call — copying from QMX here would silently drop PTT-02's drain requirement.

**Post-TX restore pattern (SYNC-03) — copy from KH1, adapt fields** (`radio_control_kh1_cat.cpp:161-185`):
```cpp
static esp_err_t kh1_end_tx(void) {
    if (!s_tx_active) return ESP_OK;

    esp_err_t err = kh1_send_cmd("HK0;", 200);
    if (err != ESP_OK) return err;

    s_tx_active = false;

    // RX decode expects KH1 back on the band dial frequency after every TX.
    err = kh1_send_fa_forced(s_rx_freq10, "tx-rx");
    esp_err_t fo_err = kh1_send_cmd("FO99;", 200);
    if (err != ESP_OK) {
        if (fo_err != ESP_OK) {
            ESP_LOGW(TAG, "KH1 TX stop restore FA failed and FO99 also failed: FA=%s FO=%s",
                     esp_err_to_name(err), esp_err_to_name(fo_err));
        }
        return err;
    }
    if (fo_err != ESP_OK) {
        return fo_err;
    }

    ESP_LOGI(TAG, "KH1 TX stop restore FA=%07d", s_rx_freq10);
    return ESP_OK;
}
```
**FTX-1's adaptation** (force-resend cached `s_rx_freq_hz` + defensive `MD0C;`, called from `ftx1_end_tx()` after `TX0;` succeeds):
```cpp
static esp_err_t ftx1_restore_rx_state(void) {
    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", s_rx_freq_hz);
    esp_err_t fa_err = ftx1_send_cmd(fa, 200);
    esp_err_t md_err = ftx1_send_cmd("MD0C;", 200);
    return (fa_err != ESP_OK) ? fa_err : md_err;
}
```
Note: KH1's restore is functionally required (its VFO shifts per-TX for split operation); FTX-1's is defensive-only (non-split single VFO) per RESEARCH.md's Pattern 3 rationale — keep the same code shape but the comment should say "defensive," not "required," to avoid a future reader assuming FTX-1 needs split-VFO logic it doesn't have.

**`set_time` pattern** — QDX's is the closest analog (`radio_control_qdx.cpp:86-90`); FTX-1 should return `ESP_ERR_NOT_SUPPORTED` per RESEARCH.md (no `DT`-command wiring this phase, discretionary):
```cpp
static esp_err_t qdx_set_time(int hour, int minute, int second) {
    char command[32];
    snprintf(command, sizeof(command), "TM%02d%02d%02d;",
             hour, minute, second);
    return qdx_send_cmd(command, 200);
}
```

**Vtable wiring — unchanged, already correct in the Phase 1 stub** (`radio_control_ftx1.cpp:51-65`); only the function bodies above it change.

---

### `main/stream_uac.cpp` — `cp210x_try_open()` (transport/config)

**Analog:** same function, immediately-preceding RTS/DTR deassert call (`stream_uac.cpp:283-321`, insertion point after line 315, before line 317):
```cpp
    // CAT-03: defense-in-depth RTS/DTR deassert (dtr=false, rts=false),
    // immediately, before any CAT traffic and regardless of the FTX-1's
    // RPTT SELECT menu state.
    esp_err_t line_err = cdc_acm_host_set_control_line_state(handle, false, false);
    ESP_LOGI(TAG, "CP210x opened (FTX-1 CAT-1 iface 0); RTS/DTR deassert: %s",
             esp_err_to_name(line_err));

    s_cdc_handle = handle;
    s_cdc_iface = 0;
    cdc_acm_host_desc_print(handle);
    g_cdc_initial_sync_pending = true;
```
**New code to insert** (per RESEARCH.md Pattern 4), between the `ESP_LOGI` line-state log and `s_cdc_handle = handle;`:
```cpp
    cdc_acm_line_coding_t line_coding = {
        .dwDTERate = 38400,
        .bCharFormat = 0,   // 1 stop bit
        .bParityType = 0,   // none
        .bDataBits = 8,
    };
    esp_err_t lc_err = cdc_acm_host_line_coding_set(handle, &line_coding);
    ESP_LOGI(TAG, "CP210x CAT-1 line coding set (38400 8N1): %s", esp_err_to_name(lc_err));
```
Follows the exact same "call the API, log the `esp_err_to_name()` result" convention already used one line above for RTS/DTR — no new error-handling shape needed.

---

## Shared Patterns

### CAT transport (send/ready) — reuse verbatim, do not reimplement
**Source:** `main/stream_uac.cpp:1128-1137` (`cat_cdc_ready()`, `cat_cdc_send()`)
**Apply to:** `ftx1_send_cmd()` wrapper only — same functions QMX/QDX call today, handle-type-agnostic.

### send_cmd wrapper shape
**Source:** `main/radio_control_qmx.cpp:17-20` / `main/radio_control_qdx.cpp:15-19` (identical)
**Apply to:** `ftx1_send_cmd()` — copy verbatim, rename only.

### Audio-tone TX model (no CAT-side tone command)
**Source:** `main/radio_control_qdx.cpp:51-54` (`qdx_set_tone_hz`), `:56-63` (`qdx_end_tx` with `uac_tx_end()`)
**Apply to:** `ftx1_set_tone_hz()` (no-op), `ftx1_end_tx()` (drain via `uac_tx_end()` before unkey). Explicitly do NOT use QMX's `qmx_set_tone_hz()` (`radio_control_qmx.cpp:58-81`, per-symbol `TA` command with float-formatting edge cases) or KH1's `kh1_set_tone_hz()` (`radio_control_kh1_cat.cpp:152-159`, `FO` command) — both are per-symbol CAT-tone models FTX-1 does not use.

### Post-TX restore
**Source:** `main/radio_control_kh1_cat.cpp:161-185` (`kh1_end_tx`)
**Apply to:** `ftx1_restore_rx_state()`, called from `ftx1_end_tx()` after `"TX0;"` succeeds, and from `ftx1_set_tune(false, ...)`'s disable branch (mirroring how `kh1_set_tune()`'s disable branch delegates to `kh1_end_tx()`, `radio_control_kh1_cat.cpp:187-189`).

### Line-coding config at transport layer
**Source:** `main/stream_uac.cpp:283-321` (`cp210x_try_open()`, existing RTS/DTR deassert call as adjacency pattern)
**Apply to:** One new `cdc_acm_host_line_coding_set()` call in the same function, per RESEARCH.md Pattern 4 — this is transport-tier work, not `radio_control_ftx1.cpp` work, despite the phase's "CAT Command" charter.

## No Analog Found

None — every file touched this phase has a direct, strong analog already read and excerpted above. RESEARCH.md's own Code Examples section (lines 264-380) additionally provides a full synthesized skeleton combining all patterns above; treat that skeleton as the target shape, and this document's per-pattern excerpts as the "why this line looks like this" justification tracing back to specific existing files.

## Metadata

**Analog search scope:** `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`, `main/radio_control_kh1_cat.cpp`, `main/radio_control_ftx1.cpp`, `main/radio_control_backend.h`, `main/stream_uac.cpp`
**Files scanned:** 6 (all read in full or targeted-section this session; no new search needed beyond RESEARCH.md's own citations, which were independently verified by direct read)
**Pattern extraction date:** 2026-07-06
