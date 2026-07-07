# Phase 2: CP210x USB Bring-up & CAT Connection - Research

**Researched:** 2026-07-05
**Domain:** ESP-IDF USB host CDC-ACM/CP210x integration, profile-gated driver install, existing `stream_uac.cpp` orchestration
**Confidence:** HIGH for code-level integration points (all read directly from the current repo); MEDIUM for exact FTX-1 VID/PID/interface-index values (must be confirmed on physical hardware per phase description)

## Summary

Phase 2's job is narrow and almost entirely mechanical: add one new component dependency (`espressif/usb_host_cp210x_vcp`), add one new `uac_stream_profile_t`/`audio_source_backend_t` value pair so FTX-1 stops silently aliasing the QMX profile, write one new "open the CP210x port" function that parallels the existing `cdc_try_open()`, and — critically — add an explicit profile guard inside `cdc_try_open()` itself so its blind CDC-ACM interface-scan can never run for a non-QMX profile. The `usb_host_cp210x_vcp` component is a thin, plain-C patch on top of the CDC-ACM driver already vendored in this project (`espressif/usb_host_cdc_acm@2.2.0`): it calls the same `cdc_acm_host_open()` the QMX code already calls, then overwrites 4 function pointers so `cdc_acm_host_line_coding_set/get`, `cdc_acm_host_set_control_line_state`, and `cdc_acm_host_send_break` issue CP210x AN571 vendor requests instead of standard CDC class requests. Everything downstream (`cdc_acm_host_data_tx_blocking`, `cdc_acm_host_close`, the `cdc_acm_dev_hdl_t` handle type, the `event_cb`/`data_cb` model) is unchanged — this is a sibling code path, not a new API surface.

The single most important non-obvious finding from reading the actual code (not just prior research) is this: **Phase 1 left `get_radio_profile_binding()` mapping FTX-1 to `AUDIO_SOURCE_QMX_UAC` as a placeholder.** Today, if a user selects FTX-1 and plugs in any USB device, `audio_source_start()` calls `uac_start_with_profile(UAC_PROFILE_QMX)`, which installs the CDC-ACM driver and, on QMX-VID/PID mismatch, falls through to `cdc_try_open()`'s hint-scan and then its **blind interface-scan loop (`iface = 0..11`, `cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, iface, ...)`)** — exactly the CDC-ACM misclaim risk CAT-02 exists to prevent. This is reachable dead code today, not a hypothetical. Fixing it requires both (a) giving FTX-1 its own profile so it never enters the QMX code path, and (b) hardening `cdc_try_open()` with an explicit `if (s_profile != UAC_PROFILE_QMX) return;` guard so this class of bug cannot recur if a fifth profile is ever added without equal care.

**Primary recommendation:** Add `UAC_PROFILE_FTX1` and route it through a brand-new `cp210x_try_open()` function (not a modification of `cdc_try_open()`) that calls `cp210x_vcp_open(CP2105_PID, /*interface_idx=*/0, &dev_cfg, &handle)` and immediately calls `cdc_acm_host_set_control_line_state(handle, /*dtr=*/false, /*rts=*/false)` before any other traffic. Reuse the existing `s_cdc_handle` variable and `cat_cdc_ready()`/`cat_cdc_send()` adapter functions unchanged — the handle type is identical (`cdc_acm_dev_hdl_t`) regardless of whether it was opened via `cdc_acm_host_open()` (QMX) or `cp210x_vcp_open()` (FTX-1), so no new C++ boundary or adapter pair is needed, and `radio_control_ftx1.cpp` can call `cat_cdc_ready()`/`cat_cdc_send()` exactly like `radio_control_qmx.cpp` does.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CAT-01 | Firmware opens a CAT connection to the FTX-1's CP210x USB virtual COM port via `espressif/usb_host_cp210x_vcp` | Component confirmed as plain-C, drop-in sibling to existing `cdc_acm_host` usage; exact `cp210x_vcp_open()` signature verified against the component's header (see Code Examples). Dependency addition point identified (`main/idf_component.yml`, `main/CMakeLists.txt` REQUIRES). |
| CAT-02 | CDC-ACM install/scan logic remains strictly profile-gated so it never misclaims the FTX-1's vendor-class interface | Exact defect identified in current code: `cdc_try_open()`'s hint-scan/blind-loop is reachable for any profile once `cdc_acm_host_install()` succeeds (which Phase 2 must do for FTX-1 too, since `cp210x_vcp_open()` depends on it). Fix: explicit profile guard inside `cdc_try_open()`, a separate `cp210x_try_open()` for FTX-1, and correct call-site dispatch in `uac_lib_task()` and `uac_on_block_processed()`. |
| CAT-03 | Firmware explicitly deasserts RTS/DTR immediately after opening the CP210x port | `cdc_acm_host_set_control_line_state(hdl, dtr, rts)` confirmed present in the already-vendored `cdc_acm_host.h` (line 242-244) and is the correct call — CP210x's vtable patch redirects it to the AN571 `SET_MHS` vendor request transparently. Call `cdc_acm_host_set_control_line_state(handle, false, false)` immediately after a successful `cp210x_vcp_open()`. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| USB host library lifecycle (`usb_host_install`, FIFO partitioning) | Firmware — USB host orchestration (`stream_uac.cpp: usb_lib_task`) | — | Single ESP32-S3 USB-OTG peripheral; one task pair owns the whole lifecycle regardless of radio type (existing pattern, unchanged) |
| CDC-ACM driver install (shared base for QMX genuine-CDC and FTX-1 CP210x-patched-CDC) | Firmware — USB host orchestration (`stream_uac.cpp: usb_lib_task`) | — | `usb_host_cp210x_vcp` extends `cdc_acm_host`, it does not replace it; the driver must be installed for FTX-1 profile too, not just QMX |
| CP210x port open + RTS/DTR deassert | Firmware — USB host orchestration (`stream_uac.cpp`, new `cp210x_try_open()`) | — | Mirrors `cdc_try_open()`'s existing location/shape; keeps all USB-stack types file-local |
| CAT command encoding (not in this phase's scope, but the transport it rides on is) | Firmware — radio backend (`radio_control_ftx1.cpp`) | Firmware — USB orchestration (`cat_cdc_send()`) | Existing C-boundary convention: radio backends never touch USB types directly |
| Radio/profile selection persistence | Firmware — station config (`main.cpp`, `station_types.h`) | — | Already exists for QMX/QDX/KH1/FTX1 (Phase 1); Phase 2 only changes which audio/profile enum value FTX-1 resolves to |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `espressif/usb_host_cp210x_vcp` | `^2.2.0` | CP210x AN571 vendor-request patch on top of a standard `cdc_acm_dev_hdl_t` | Official Espressif component; pure C, no exceptions; drop-in sibling to the CDC-ACM code already used for QMX `[VERIFIED: npm/component registry via prior STACK.md research + direct header fetch this session]` |
| `espressif/usb_host_cdc_acm` | `^2.2.0` (already pinned) | Base CDC-ACM host driver; owns `cdc_acm_dev_hdl_t`, `cdc_acm_host_open/close`, `cdc_acm_host_set_control_line_state` | No version change needed — `usb_host_cp210x_vcp@2.2.0` requires `^2.1.0`, already satisfied `[VERIFIED: read managed_components/espressif__usb_host_cdc_acm/idf_component.yml and header directly this session]` |
| ESP-IDF | v5.5.1 (unchanged) | USB Host Library, FreeRTOS | Project constraint; no IDF changes needed |

### Version verification
```bash
# Confirms header signature and PID constants (done this session via direct GitHub fetch):
# cp210x_vcp_open(uint16_t pid, uint8_t interface_idx,
#                 const cdc_acm_host_device_config_t *dev_config,
#                 cdc_acm_dev_hdl_t *cdc_hdl_ret)
# SILICON_LABS_VID=0x10C4, CP210X_PID=0xEA60, CP2105_PID=0xEA70, CP2108_PID=0xEA71, CP210X_PID_AUTO=0
```
`[VERIFIED: WebFetch of raw.githubusercontent.com/espressif/esp-usb/master/host/class/cdc/usb_host_cp210x_vcp/include/usb/vcp_cp210x.h this session]`

**Installation** — add to `main/idf_component.yml` (confirmed current file content read this session):
```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  lvgl/lvgl: ^8.3.9
  espressif/usb_host_uac: ^1.2.0
  espressif/usb: '*'
  espressif/usb_host_cdc_acm: ^2.2.0
  # CP210x CAT for FTX-1 (CAT-1 / Enhanced COM only)
  espressif/usb_host_cp210x_vcp: ^2.2.0
  espressif/esp_codec_dev: ^1.5.9
```

And `main/CMakeLists.txt`'s `REQUIRES` list (confirmed current content read this session — currently `ui M5Unified M5GFX M5Cardputer board_cardputer_adv ft8_lib usb_host_uac usb_host_cdc_acm storage_service external_rtc efuse`) needs `usb_host_cp210x_vcp` appended. No new `.cpp` file is required in SRCS for this component (it's header+prebuilt-pattern consumed directly from `stream_uac.cpp`, same as `usb_host_cdc_acm` today).

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `usb_host_cp210x_vcp` plain-C `cp210x_vcp_open()` | `usb_host_vcp` C++ `VCP::open()` / `esp_usb::CP210x` wrapper | Rejected in PROJECT.md's Out of Scope table — requires `CONFIG_COMPILER_CXX_EXCEPTIONS`, throws `esp_err_t` from constructors; incompatible with this codebase's plain-`esp_err_t` idiom used everywhere else. Do not use. |
| Fixed `CP2105_PID` | `CP210X_PID_AUTO` | Use only if hardware bring-up shows the FTX-1 reports a PID other than the three built-in constants — `CP210X_PID_AUTO` transparently tries all three with the fixed Silicon Labs VID, zero code restructuring needed |

## Architecture Patterns

### System Architecture Diagram

```
FTX-1 (single USB port, composite device)
   │  CP2105 dual-UART bridge (2x vendor-class 0xFF interfaces: CAT-1 Enhanced, CAT-2 Standard)
   │  + USB Audio Class interfaces (mic/speaker) — NOT touched in this phase
   ▼
ESP32-S3 USB-OTG (single peripheral, single usb_host_install())
   ▼
stream_uac.cpp: usb_lib_task()
   ├─ usb_host_install()                              (unchanged)
   ├─ cdc_acm_host_install(cdc_cfg)                    ★ CHANGED: now runs for
   │     new_dev_cb = cdc_new_dev_cb                     BOTH UAC_PROFILE_QMX
   │     (unconditional CDC-ACM base driver;             and UAC_PROFILE_FTX1
   │      required because cp210x_vcp_open()             (cp210x depends on it)
   │      builds on top of it)
   ▼
device attaches → cdc_new_dev_cb() interface scan (harmless: CP210x reports
   class 0xFF, never matches USB_CLASS_COMM, so s_cdc_iface_hint stays -1
   for the FTX-1 — no change needed here)
   ▼
uac_lib_task() mic RX_CONNECTED handler
   ├─ if profile == UAC_PROFILE_QMX  → cdc_try_open()        (existing, UNCHANGED
   │                                                           behavior, but now
   │                                                           hard-gated so it
   │                                                           can NEVER run for
   │                                                           any other profile)
   └─ if profile == UAC_PROFILE_FTX1 → cp210x_try_open()      ★ NEW
          cp210x_vcp_open(CP2105_PID, iface=0, &dev_cfg, &handle)
          → on success: cdc_acm_host_set_control_line_state(handle, false, false)  (CAT-03)
          → s_cdc_handle = handle   (reused variable/type — no new adapter needed)
   ▼
radio_control_ftx1.cpp (Phase 3 territory — not touched by Phase 2 except
   that `ready()` can now legitimately flip to reflecting cat_cdc_ready())
   calls cat_cdc_ready() / cat_cdc_send() — IDENTICAL functions QMX already uses
```

### Recommended Project Structure (files touched, no new files)
```
main/
├── idf_component.yml       # + espressif/usb_host_cp210x_vcp: ^2.2.0
├── CMakeLists.txt          # + usb_host_cp210x_vcp in REQUIRES
├── stream_uac.h            # + UAC_PROFILE_FTX1 enum value
├── stream_uac.cpp          # + #include "usb/vcp_cp210x.h"
│                           # + cp210x_try_open() (new function, mirrors cdc_try_open shape)
│                           # + profile_name() case for UAC_PROFILE_FTX1
│                           # + usb_lib_task(): install cdc_acm_host for QMX OR FTX1
│                           # + cdc_try_open(): explicit `if (s_profile != UAC_PROFILE_QMX) return;` guard
│                           # + uac_lib_task() RX_CONNECTED handler: dispatch cdc_try_open()
│                           #   vs cp210x_try_open() by profile
│                           # + uac_on_block_processed(): dispatch by profile, not unconditional cdc_try_open()
├── audio_source.h          # + AUDIO_SOURCE_FTX1_CP210X enum value (or similarly named)
├── audio_source.cpp        # + backend_is_uac() include new value; audio_source_start()
│                           #   maps new value -> UAC_PROFILE_FTX1
└── main.cpp                # get_radio_profile_binding(): FTX1 case returns the new
                             #   audio backend instead of the AUDIO_SOURCE_QMX_UAC placeholder
```

### Pattern 1: New profile enum values, not new task pairs

**What:** `uac_stream_profile_t` (currently `UAC_PROFILE_QMX = 0`, `UAC_PROFILE_GENERIC_USB = 1`) gets a third value `UAC_PROFILE_FTX1 = 2`. `audio_source_backend_t` (currently `AUDIO_SOURCE_QMX_UAC = 0`, `AUDIO_SOURCE_USB_UAC_GENERIC = 1`, `AUDIO_SOURCE_KH1_MIC = 2`) gets a fourth value, e.g. `AUDIO_SOURCE_FTX1_CP210X = 3`.
**When to use:** Confirmed as the existing project convention (`ARCHITECTURE.md` Anti-Pattern 3) — never fork `stream_uac.cpp` into a second file/task-pair per radio.
**Example (exact current code being extended, `main/main.cpp:1615-1631`):**
```cpp
// CURRENT (Phase 1 placeholder):
case RadioType::FTX1:
  // AUDIO_SOURCE_QMX_UAC is a Phase-1 placeholder; revisit in Phase 4 (AUDIO-01/02/03)
  // once the FTX-1's real USB audio profile is validated against hardware.
  return {AUDIO_SOURCE_QMX_UAC, RADIO_CONTROL_FTX1};

// PHASE 2 CHANGE:
case RadioType::FTX1:
  return {AUDIO_SOURCE_FTX1_CP210X, RADIO_CONTROL_FTX1};
```
`audio_source.cpp`'s `audio_source_start()` then needs a third branch mapping `AUDIO_SOURCE_FTX1_CP210X` to `UAC_PROFILE_FTX1` (current code only branches `AUDIO_SOURCE_USB_UAC_GENERIC` vs. default-QMX):
```cpp
// Source: main/audio_source.cpp:39-47 (current), extend with:
uac_stream_profile_t profile = UAC_PROFILE_QMX;
if (s_backend == AUDIO_SOURCE_USB_UAC_GENERIC) {
    profile = UAC_PROFILE_GENERIC_USB;
} else if (s_backend == AUDIO_SOURCE_FTX1_CP210X) {
    profile = UAC_PROFILE_FTX1;
}
```

### Pattern 2: CP210x open + RTS/DTR deassert, mirroring `cdc_try_open()`'s shape exactly

**What:** A new function with the same throttle/guard structure as the existing `cdc_try_open()` (`main/stream_uac.cpp:198-272`), but with no hint-scan and no blind interface loop — FTX-1's VID/PID/interface are known, so there is never a reason to probe.
**When to use:** Called only from the FTX-1 branch of `uac_lib_task()`'s mic-connected handler and from `uac_on_block_processed()`, exactly where `cdc_try_open()` is called today for QMX.
**Example:**
```cpp
// Source: adapted from cdc_try_open() (main/stream_uac.cpp:198-272) and
// verified cp210x_vcp_open() signature (esp-usb vcp_cp210x.h, fetched this session)
#include "usb/vcp_cp210x.h"

// Hardware-validate: FTX-1 confirmed CP2105 dual-UART per two independent
// web sources (STACK.md), NOT yet confirmed against the physical unit.
// interface_idx=0 assumed to be CAT-1 (Enhanced COM) — MUST be verified
// via cdc_acm_host_desc_print() during bring-up; see Open Questions.
static void cp210x_try_open(void) {
    if (!s_cdc_installed) return;
    if (s_cdc_handle) return;

    int64_t now_ms = rtc_now_ms();
    if (s_cdc_last_attempt_ms != 0 && (now_ms - s_cdc_last_attempt_ms) < 1000) return;
    s_cdc_last_attempt_ms = now_ms;

    cdc_acm_host_device_config_t dev_cfg = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 64,
        .in_buffer_size = 256,   // FTX-1 needs RX for query replies (Phase 3); unlike QMX's TX-only CAT
        .event_cb = cdc_event_cb,   // reuse existing callback — same disconnect/error handling
        .data_cb = NULL,            // Phase 3 wires a real data_cb once query commands exist
        .user_arg = NULL,
    };

    cdc_acm_dev_hdl_t handle = NULL;
    esp_err_t err = cp210x_vcp_open(CP2105_PID, /*interface_idx=*/0, &dev_cfg, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "CP210x open (FTX-1 CAT-1) failed: %s", esp_err_to_name(err));
        }
        return;
    }

    // CAT-03: defense-in-depth RTS/DTR deassert, immediately, before any
    // CAT traffic and regardless of the FTX-1's RPTT SELECT menu state.
    esp_err_t line_err = cdc_acm_host_set_control_line_state(handle, /*dtr=*/false, /*rts=*/false);
    ESP_LOGI(TAG, "CP210x opened (FTX-1 CAT-1 iface 0); RTS/DTR deassert: %s",
             esp_err_to_name(line_err));

    s_cdc_handle = handle;
    s_cdc_iface = 0;
    cdc_acm_host_desc_print(handle);
    g_cdc_initial_sync_pending = true;  // Phase 3 will consume this the same way QMX does
}
```

### Pattern 3: Hard-gate `cdc_try_open()` against ever running for a non-QMX profile

**What:** Add one line at the top of the existing function.
**Why:** Today, `cdc_try_open()`'s hint-scan and 12-interface blind loop execute for ANY profile as long as `s_cdc_installed` is true — and Phase 2 must set `s_cdc_installed = true` for `UAC_PROFILE_FTX1` too (since CP210x depends on the CDC-ACM base driver). Without this guard, enabling CDC-ACM install for FTX-1 reopens exactly the misclaim path CAT-02 requires closed.
**Example:**
```cpp
// Source: main/stream_uac.cpp:198-201, MODIFIED
static void cdc_try_open(void) {
    if (!s_cdc_installed) return;
    if (s_cdc_handle) return;
    if (s_profile != UAC_PROFILE_QMX) return;   // ★ NEW — hard boundary, CAT-02
    ...
```

### Anti-Patterns to Avoid
- **Registering a second class driver for CP210x:** `usb_host_cp210x_vcp` is not a separate installable driver — it is a function (`cp210x_vcp_open()`) that patches an already-open `cdc_acm_dev_hdl_t`. Do not look for a `cp210x_install()`-style call; none exists. (This corrects `ARCHITECTURE.md`'s earlier framing, written before `STACK.md`'s course-correction away from the C++ `usb_host_vcp` service — see Assumptions Log.)
- **Reusing `cdc_try_open()` for FTX-1 by adding an `if (s_profile == UAC_PROFILE_FTX1) {...}` branch inside it:** keep FTX-1's open logic in a fully separate function. Mixing the two invites exactly the kind of "one profile's fallback accidentally reachable for another profile" bug this phase exists to fix.
- **Skipping `cdc_acm_host_install()` for the FTX-1 profile** on the theory that CP210x is "not CDC-ACM": wrong — `cp210x_vcp_open()` calls `cdc_acm_host_open()` internally and will fail/misbehave if the base driver was never installed.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| CP210x AN571 vendor-request command set (SET_BAUDRATE, SET_LINE_CTL, SET_MHS, IFC_ENABLE) | A custom vendor-request driver | `espressif/usb_host_cp210x_vcp`'s `cp210x_vcp_open()` | Already implements this exactly, actively maintained; PROJECT.md already ruled out hand-rolling |
| RTS/DTR line control on a CP210x | A raw `usb_host_transfer_submit()` control request | `cdc_acm_host_set_control_line_state(handle, dtr, rts)` | Already the correct public API — the CP210x driver's vtable patch makes this call issue the right vendor request transparently; no new function needed |

**Key insight:** There is no new API surface to design in this phase — every piece needed (open, line control, send/close) already exists as a public `cdc_acm_host_*` function this codebase already calls for QMX. The entire phase is: get the component installed, call the right open function with the right known VID/PID/interface, deassert lines, and make absolutely sure the QMX-specific fallback scan can't run.

## Common Pitfalls

> Full detail already captured in `.planning/research/PITFALLS.md` Pitfalls 1, 2, 3, 5 — summarized here with this session's code-level confirmation of exactly where each one lives and how to close it.

### Pitfall 1: FIFO/channel budget (deferred risk, not a Phase 2 blocker)
**What goes wrong:** `usb_lib_task()`'s custom FIFO partition (`rx=91/nptx=18/ptx=91` lines) is currently applied only when `s_profile == UAC_PROFILE_QMX`. For FTX-1 in Phase 2, this branch is NOT entered (FTX-1 gets Kconfig default FIFO sizing) since no bidirectional 24-bit/48k audio is active yet in this phase.
**Why it happens:** FTX-1's actual endpoint MPS/audio descriptor is unconfirmed until Phase 4.
**How to avoid (this phase):** Do nothing yet — default Kconfig FIFO sizing should be adequate for CP210x bulk CAT traffic alone plus a failed/skipped mic negotiation attempt. Log `usb_host_lib_handle_events`/device-open return codes loudly during bring-up so any exhaustion is visible immediately, not silently swallowed.
**Warning signs:** `cp210x_vcp_open()` returning `ESP_ERR_NOT_FOUND`/`ESP_ERR_NO_MEM` specifically only when the (currently-failing) mic negotiation is also active.
**Phase to address:** Fully — Phase 4 (AUDIO-03). Phase 2 only needs to not make it worse.

### Pitfall 2: CDC-ACM misclaim (THIS PHASE'S CORE RISK — see Pattern 3 above)
Already detailed above with exact line numbers. This is the single pitfall Phase 2 exists to close.

### Pitfall 3: Dual-driver install confusion
**Resolution specific to this codebase (differs from PITFALLS.md's more general framing):** Because `usb_host_cp210x_vcp` extends `cdc_acm_host` rather than being a separate driver, there is only ONE driver to install (`cdc_acm_host_install()`), for BOTH `UAC_PROFILE_QMX` and `UAC_PROFILE_FTX1`. There is no second `usb_host_vcp` driver-install call to accidentally leave active — this simplifies the pitfall considerably versus what `PITFALLS.md`/`ARCHITECTURE.md` anticipated when they assumed the C++ `usb_host_vcp` service (since superseded, see Assumptions Log). The only residual risk is forgetting to broaden the `if (s_profile == UAC_PROFILE_QMX)` install-guard to also include `UAC_PROFILE_FTX1` — verify the boot log shows exactly one `"CDC-ACM driver installed"` line for FTX-1 sessions, same as QMX.

### Pitfall 5: CP210x auto-asserting RTS/DTR on open
Addressed directly by Pattern 2's `cdc_acm_host_set_control_line_state(handle, false, false)` call, placed immediately after successful open, before any CAT traffic. **Must be verified on real hardware** per the phase's own success criteria #2 — confirm no spurious keyup on the FTX-1's own display across multiple cold-boot/replug cycles.

## Code Examples

### Full open-path sequence for FTX-1 (combines Patterns 1-3)
```c
// Source: main/stream_uac.cpp integration, using verified cp210x_vcp_open()
// signature (esp-usb vcp_cp210x.h) and existing cdc_acm_host.h API
// (managed_components/espressif__usb_host_cdc_acm/include/usb/cdc_acm_host.h:242)
#include "usb/vcp_cp210x.h"

// usb_lib_task(), extended install condition:
if (s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1) {
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

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ARCHITECTURE.md`'s framing of a C++ `usb_host_vcp`/`VCP::register_driver<T>()` service sitting "on top of" `cdc_acm_host` | Plain-C `usb_host_cp210x_vcp`'s `cp210x_vcp_open()`, which patches an already-open `cdc_acm_dev_hdl_t` in place | Superseded within the same research pass — `STACK.md` (same date) and `PROJECT.md`'s Out of Scope table both explicitly reject the C++ path in favor of the plain-C component | Simpler: no new driver-install call, no C++ exceptions requirement, no `CdcAcmDevice*`/adapter-function boundary needed — FTX-1's CAT-1 handle is a plain `cdc_acm_dev_hdl_t`, identical in type to QMX's, and can flow through the exact same `cat_cdc_ready()`/`cat_cdc_send()` functions |

**Deprecated/outdated:** Any plan or code drafted from `ARCHITECTURE.md`'s "Pattern 2" (VCP C++ object, `cat_vcp_ready()`/`cat_vcp_send()` as a *new* adapter pair) should be discarded — that framing predates `STACK.md`'s and `PROJECT.md`'s explicit decision to use the plain-C component instead. No new adapter functions are needed; `cat_cdc_ready()`/`cat_cdc_send()` already work unchanged.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | FTX-1 reports VID `0x10C4` / PID `0xEA70` (CP2105, dual-port) | Standard Stack, Code Examples | If the physical unit reports a different PID, `CP2105_PID` hardcode fails to open (`ESP_ERR_NOT_FOUND`); mitigated by falling back to `CP210X_PID_AUTO` (no code restructuring needed) — flagged in prior STACK.md as MEDIUM confidence, web-sourced only |
| A2 | FTX-1's CAT-1 (Enhanced COM) is `interface_idx=0` on the composite descriptor | Pattern 2, Code Examples | If CAT-1 is actually interface 1 (with CAT-2 at 0), the firmware would open the wrong virtual port; must be confirmed via `cdc_acm_host_desc_print()` during first hardware bring-up before committing to a fixed index |
| A3 | `dev_cfg.in_buffer_size = 256` is a safe placeholder for Phase 2 even though no `data_cb`/read logic is wired until Phase 3 | Pattern 2 | Low risk — an unread RX buffer with no consumer simply accumulates/drops; does not block CAT-01/02/03's success criteria, but should be revisited once Phase 3 adds real query-command handling |
| A4 | Default (non-custom) Kconfig FIFO sizing is sufficient for CP210x-alone traffic in Phase 2, with the mic negotiation attempt failing harmlessly | Common Pitfalls (Pitfall 1) | If FIFO/channel exhaustion occurs even without audio active, CAT-01's "opens reliably across replug cycles" criterion could intermittently fail — would need same-session hardware diagnosis, not deferrable to Phase 4 in that case |

## Open Questions (HARDWARE-GATED — resolved in 02-03-PLAN.md)

1. **CONFIRMED on physical hardware (2026-07-06) — Exact FTX-1 CP210x variant and CAT-1 interface index**
   - What we know: Two independent web sources describe a "Silicon Labs Dual CP210x" (CP2105) with Windows Device Manager showing separate "Enhanced COM" (CAT-1) and "Standard COM" (CAT-2) ports.
   - Confirmed via Windows PnP enumeration (`Get-PnpDevice`) with the physical FTX-1 connected directly to a PC: VID `0x10C4`, PID `0xEA70` (CP2105) — exact match to the assumed `CP2105_PID`. Interface `MI_00` = "Enhanced COM Port" (CAT-1), interface `MI_01` = "Standard COM Port" (CAT-2) — confirming interface index `0` = CAT-1, exactly as assumed.
   - Outcome: No correction needed to `cp210x_try_open()` — the assumed `CP2105_PID` / interface index `0` are both correct as committed in 02-02. Firmware-side enumeration (via `cdc_acm_host_desc_print()` on the Cardputer) is still pending as part of 02-03's checkpoint, but the PID/interface-index unknowns are now closed.

2. **RESOLVED (hardware-gated) — Behavior of the mic-negotiation attempt against FTX-1's actual UAC descriptor during Phase 2**
   - What we know: `uac_lib_task()`'s RX_CONNECTED handler will still fire for FTX-1's UAC interfaces even though Phase 2 doesn't need audio; using the existing QMX-shaped hardcoded candidate (since FTX-1 isn't yet added to the `UAC_PROFILE_GENERIC_USB` candidate-scan branch) will likely fail and log "Format not supported."
   - What's unclear: Whether this failure is silent/harmless or produces log noise that could be mistaken for a CAT-related bug during Phase 2 bring-up.
   - Resolution: Called out as expected/benign in 02-03-PLAN.md's verification steps, so a human verifying hardware doesn't chase it as a regression. Adding `UAC_PROFILE_FTX1` to the candidate-scan branch to suppress the noise remains discretionary and out of scope for CAT-01/02/03 — deferred to Phase 4 (AUDIO-01).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| ESP-IDF v5.5.1 toolchain (`idf.py`) | Build verification | Unknown in this research session (not probed; prior phase's executor sandbox lacked it) | — | Manual code/grep audit, as Phase 1 did, with orchestrator running the real build afterward |
| `espressif/usb_host_cp210x_vcp` component | CAT-01 | Not yet fetched into `managed_components/` (no entry found this session) | ^2.2.0 (registry-confirmed by prior STACK.md research) | None needed — `idf.py reconfigure` after adding the dependency will fetch it |
| Physical FTX-1 hardware | CAT-01, CAT-02, CAT-03 hardware verification | User-confirmed available per phase brief | — | None — this phase's success criteria are explicitly hardware-verifiable and cannot be validated by code review alone |

**Missing dependencies with no fallback:** None blocking — the CP210x component fetch is a standard `idf_component.yml` addition with no alternative path needed.

## Security Domain

> `security_enforcement: true` in config.json (ASVS Level 1, block on high). This phase is firmware-only USB/CAT transport bring-up with no network surface; most ASVS categories do not apply. Documented per project convention (`ARCHITECTURE.md` Security cross-cutting note: "None (local device). CAT radio control assumes trusted connection.").

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | No auth surface — local USB peripheral |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Partial | USB descriptor parsing (`cdc_new_dev_cb`, `cp210x_vcp_open`'s internal descriptor walk) is bounds-checked by the vendored ESP-IDF/esp-usb components, not custom code in this phase; no new parsing code is introduced by Phase 2 itself |
| V6 Cryptography | No | N/A — CAT link is plaintext ASCII by design (Kenwood protocol), consistent with existing QMX/QDX/KH1 backends |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed/spoofed USB device descriptor causing the CDC-ACM/CP210x parsing path to misbehave | Tampering | Rely on the vendored, actively-maintained `espressif/usb_host_cdc_acm`/`usb_host_cp210x_vcp` components' own bounds-checked descriptor parsing; do not add custom descriptor-walking code in this phase beyond the existing `cdc_new_dev_cb` logging helper, which only reads already-validated `usb_config_desc_t`/`usb_intf_desc_t` structures the ESP-IDF USB host stack has already parsed |
| A future CAT response-read path (Phase 3, not this phase) trusting unvalidated bytes from `data_cb` | Tampering / Denial of Service | Out of scope for Phase 2 (`in_buffer_size`/`data_cb` are placeholder-only here); flag for Phase 3 research to apply the same input-length/range validation discipline `CONCERNS.md` already recommends project-wide |

## Sources

### Primary (HIGH confidence)
- `C:\GitHub\Mini-FT8\main\stream_uac.cpp` — direct read, full file, this session
- `C:\GitHub\Mini-FT8\main\stream_uac.h` — direct read, this session
- `C:\GitHub\Mini-FT8\main\audio_source.cpp` / `.h` — direct read, this session
- `C:\GitHub\Mini-FT8\main\main.cpp` (lines 1595-1675, `get_radio_profile_binding`/`apply_radio_profile_binding`) — direct read, this session
- `C:\GitHub\Mini-FT8\main\radio_control_ftx1.cpp`, `radio_control_qmx.cpp` — direct read, this session
- `C:\GitHub\Mini-FT8\main\idf_component.yml`, `main\CMakeLists.txt` — direct read, this session
- `C:\GitHub\Mini-FT8\managed_components\espressif__usb_host_cdc_acm\include\usb\cdc_acm_host.h` (lines 232-249) — direct read, this session, confirms `cdc_acm_host_set_control_line_state(hdl, dtr, rts)` exists and is the correct RTS/DTR call
- https://raw.githubusercontent.com/espressif/esp-usb/master/host/class/cdc/usb_host_cp210x_vcp/include/usb/vcp_cp210x.h — WebFetch this session, confirms `cp210x_vcp_open()` signature and PID constants
- `.planning/research/STACK.md`, `PITFALLS.md`, `ARCHITECTURE.md` — prior project-wide research, HIGH confidence per their own sourcing (component source/headers read directly)
- `.planning/phases/01-backend-vtable-plumbing/01-01-SUMMARY.md` — confirms exact Phase 1 state (`RADIO_CONTROL_FTX1=3`, `RadioType::FTX1=6`, `AUDIO_SOURCE_QMX_UAC` placeholder)

### Secondary (MEDIUM confidence)
- FTX-1 exact VID/PID/CP2105 identity — carried over from prior STACK.md web research (two independent sources), not re-verified against physical hardware this session (correctly, since hardware validation is this phase's execution-time job, not research's)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — component API verified via direct header fetch this session, dependency compatibility verified via direct file reads
- Architecture: HIGH — every integration point cites exact current line numbers from files read this session; the one open design question (FTX-1 PID/interface index) is explicitly hardware-gated, not a research gap
- Pitfalls: HIGH for the CDC-ACM misclaim mechanism (traced through actual current code, not inferred) — MEDIUM for FIFO/audio-negotiation side effects (deferred to Phase 4 by design, flagged not ignored)

**Research date:** 2026-07-05
**Valid until:** Should remain valid through Phase 2 execution (no external moving parts expected to change); re-check `usb_host_cp210x_vcp` version if Phase 2 execution is delayed more than ~30 days, since it is an actively-maintained Espressif component.

---
*Phase 2 research: CP210x USB Bring-up & CAT Connection*
*Researched: 2026-07-05*
