# Architecture Research

**Domain:** Adding a new CAT+USB-audio radio backend (Yaesu FTX-1) to an existing ESP32-S3 FT8/FT4 firmware with a fixed three-event single-threaded architecture
**Researched:** 2026-07-04
**Confidence:** MEDIUM (backend vtable/CAT integration pattern is HIGH — directly modeled on existing code; `usb_host_vcp`/CP210x driver internals are MEDIUM — confirmed from official component docs/headers but not full source, and must be validated against real FTX-1 hardware)

This file does not re-document the overall Mini-FT8 architecture (see `.planning/codebase/ARCHITECTURE.md`). It focuses narrowly on where a new radio backend slots in and what changes shape (vs. what stays load-bearing and must not move).

## Standard Architecture (as it applies here)

### System Overview — where FTX-1 support attaches

```
┌─────────────────────────────────────────────────────────────────────────┐
│  main/main.cpp — app_task_core0 (UNCHANGED)                             │
│  Three-event loop: decode-complete → autoseq → slot-boundary/TX         │
│  Radio selection: g_radio picks ops table + uac profile at boot         │
└───────────────────────────┬───────────────────────────────────────────--┘
                            │  calls through radio_control_ops_t (vtable)
                            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  radio_control_backend.h  (UNCHANGED SHAPE — one new getter added)      │
│    radio_control_ftx1_get_ops()  ← NEW, mirrors qmx/qdx/kh1             │
└───────┬─────────────────────────────────────────┬───────────────────────┘
        │                                         │
        ▼                                         ▼
┌──────────────────────────┐          ┌────────────────────────────────────┐
│ radio_control_ftx1.cpp   │          │ stream_uac.cpp                    │
│ (NEW — CAT encode only)  │          │  + UAC_PROFILE_FTX1 (NEW)         │
│ FA/MD0C/TX/PC/TM cmds    │◄────────►│  - VCP/CP210x driver install      │
│ Kenwood-ASCII, `;`-term  │  cat_vcp_│  - VCP::open(vid,pid) → CdcAcmDev │
│ same shape as QMX file   │  send/   │  - bidirectional UAC negotiation  │
│                          │  ready() │    (mic + speaker, like QMX path) │
└──────────────────────────┘          └───────────────┬────────────────────┘
                                                       │
                                                       ▼
                                        ┌────────────────────────────────┐
                                        │ ESP-IDF USB Host stack          │
                                        │  usb_host_cdc_acm (shared base) │
                                        │  usb_host_vcp + cp210x driver   │
                                        │    (registers ON TOP of        │
                                        │     cdc_acm_host, NEW dep)      │
                                        │  usb_host_uac (existing, reused)│
                                        └────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Typical Implementation |
|-----------|----------------|------------------------|
| `radio_control_ftx1.cpp` (new) | Encode/send Kenwood-style CAT ASCII commands (`FA`, `MD0C`, `TX0/1`, `PC`, `TM`), expose `radio_control_ops_t` | 1:1 structural copy of `radio_control_qmx.cpp`; C linkage only, no C++ USB types visible |
| `stream_uac.cpp` — FTX-1 profile branch (extended, not rewritten) | Own all USB host lifecycle for FTX-1: install VCP+CP210x driver, open CAT device, negotiate bidirectional UAC audio | New `case` branches keyed off `uac_stream_profile_t`, following the existing QMX/GENERIC_USB branching pattern already in the file |
| CAT adapter shim (new, small — can live in `stream_uac.cpp` or a new `stream_vcp.cpp`) | Bridge C++ `CdcAcmDevice*` (VCP) object to plain C functions (`cat_vcp_ready()`, `cat_vcp_send()`) | Mirrors existing `cat_cdc_ready()` / `cat_cdc_send()` free functions that already do this for QMX's CDC-ACM handle |
| `usb_host_vcp` + `usb_host_cp210x_vcp` (new external component) | Recognize the FTX-1's Silicon Labs CP210x chip (vendor-class, not CDC-ACM class 0x02) and expose it through a CDC-ACM-compatible object | Espressif-maintained component, added via `idf_component.yml`; per official docs it **extends** `cdc_acm_host` rather than replacing it |
| Station config / radio selection (existing, extended) | Add `"ftx1"` as a selectable radio type alongside qmx/qdx/kh1 | Existing enum + `g_radio` dispatch in `main.cpp`; one new case, no new mechanism |

## Recommended Integration Structure

```
main/
├── radio_control_backend.h        # + radio_control_ftx1_get_ops() declaration
├── radio_control_ftx1.cpp         # NEW — CAT command encoding only (C, no USB types)
├── stream_uac.cpp                 # + UAC_PROFILE_FTX1 case in profile_name(),
│                                  #   usb_lib_task() (driver install choice),
│                                  #   uac_lib_task() (audio candidate list,
│                                  #   speaker pre-open reuse), FIFO partitioning
├── stream_uac.h                   # + UAC_PROFILE_FTX1 enum value,
│                                  #   cat_vcp_ready()/cat_vcp_send() decls
└── main.cpp                       # + "ftx1" radio-type case in existing
                                    #   g_radio / uac_start_with_profile() dispatch
```

### Structure Rationale

- **No new architectural layer.** FTX-1 is a fourth radio backend, not a new subsystem. It reuses the exact seam (`radio_control_ops_t`) and the exact USB orchestration file (`stream_uac.cpp`) that QMX/QDX already use. Adding a fifth radio later (or removing FTX-1) should touch the same two files and nothing else.
- **CAT encoding stays pure C, no exceptions.** `radio_control_qmx.cpp` and `radio_control_kh1_cat.cpp` are plain C-style `.cpp` files with no C++ class usage — they call free functions (`cat_cdc_ready()`, `cat_cdc_send()`). `radio_control_ftx1.cpp` must follow the same convention: it should never directly touch `usb_host_vcp`'s C++ `VCP`/`CdcAcmDevice` classes. Keep that C++ surface entirely inside `stream_uac.cpp`, exposed to the rest of the codebase only via small `extern "C"`-callable functions (`cat_vcp_ready()`, `cat_vcp_send()`), exactly mirroring the existing `cat_cdc_ready()/cat_cdc_send()` pair. This preserves the existing convention that `radio_control_backend.h` is includable from plain-C-style code and keeps USB-stack churn contained to one file.
- **Profile enum, not a parallel code path.** `uac_stream_profile_t` already exists specifically to let one orchestration file (`stream_uac.cpp`) branch its behavior per radio (`UAC_PROFILE_QMX` vs `UAC_PROFILE_GENERIC_USB`). Adding `UAC_PROFILE_FTX1` as a third value keeps one state machine, one task set, one event queue — instead of standing up a second parallel USB-host task pair. This is the single most important structural decision: **do not fork `stream_uac.cpp` into a new file for FTX-1.**

## Architectural Patterns

### Pattern 1: Radio backend as a pure-data vtable, no polymorphism at the call site

**What:** `radio_control_ops_t` is a C struct of function pointers. `main.cpp` holds one `const radio_control_ops_t*` and calls through it uniformly; it never branches on "which radio."
**When to use:** Any time a new radio is added. `radio_control_ftx1_get_ops()` must return a `static const radio_control_ops_t` with all 8 members populated (or explicitly `NULL` for genuinely unsupported ops — QMX populates all 8 today).
**Trade-offs:** Very cheap to add a backend; but every backend must independently reimplement retry/timeout semantics — there's no shared CAT transport base class. FTX-1's CAT is Kenwood-ASCII like QMX, so most of `qmx_send_cmd`'s shape (blocking send, `;`-terminated string, small fixed timeout) can be copied near-verbatim.

**Example (structure to follow, from `radio_control_qmx.cpp`):**
```cpp
static esp_err_t ftx1_send_cmd(const char* cmd, uint32_t timeout_ms) {
    if (!cat_vcp_ready()) return ESP_ERR_INVALID_STATE;
    return cat_vcp_send(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), timeout_ms);
}
static esp_err_t ftx1_sync_frequency_mode(int freq_hz) {
    char fa[32];
    snprintf(fa, sizeof(fa), "FA%09d;", freq_hz);   // FTX-1: 9-digit Hz, not QMX's 11-digit
    esp_err_t err = ftx1_send_cmd(fa, 200);
    if (err != ESP_OK) return err;
    return ftx1_send_cmd("MD0C;", 200);              // DATA-U mode
}
```

### Pattern 2: USB host orchestration owns exactly one profile-selected driver combination per session

**What:** `stream_uac.cpp`'s `usb_lib_task()` conditionally installs `cdc_acm_host` only when `s_profile == UAC_PROFILE_QMX`. For FTX-1, this branch must instead (a) still install `cdc_acm_host` (VCP extends it per Espressif docs) **and** (b) additionally register the CP210x class driver through the VCP service, then use `VCP::open(vid, pid, &dev_config)` in place of `cdc_acm_host_open()` in the equivalent of `cdc_try_open()`.
**When to use:** Any radio whose CAT chip is not a standard CDC-ACM class device. This is the FTX-1-specific novelty in this codebase — QMX/QDX are genuine CDC-ACM (class 0x02); KH1 uses UART directly (no USB CAT at all). FTX-1 (CP210x) is the first case needing the VCP layer.
**Trade-offs:** `usb_host_vcp`/`usb_host_cp210x_vcp` is C++ (templated `VCP::register_driver<T>()`, returns `CdcAcmDevice*`), a first for this project's USB code, which is otherwise plain C ESP-IDF API calls. This is fine because `stream_uac.cpp` is already `.cpp`; it just means the FTX-1 branch inside it will look stylistically different (C++ object, not a raw handle) from the QMX branch right next to it — acceptable as long as it's fully contained (see Pattern 1's C boundary).

### Pattern 3: Bidirectional UAC negotiation reuses the QMX "pre-open speaker at enum time" trick, generalized

**What:** QMX's speaker (UAC OUT) is opened and claimed once at `UAC_HOST_DRIVER_EVENT_TX_CONNECTED` time (while heap is least fragmented) and then only resumed/suspended per TX cycle — never reallocated. The mic (UAC IN) format is negotiated candidate-by-candidate (`add_candidate` loop), currently strict-only for QMX and format-scanning for GENERIC_USB.
**When to use:** FTX-1 needs both behaviors simultaneously: bidirectional like QMX (so reuse the speaker pre-open/resume/suspend machinery) but with unknown/negotiated format like GENERIC_USB (so reuse the candidate-scanning loop, not QMX's single hardcoded candidate). Concretely: extend the `if (s_profile == UAC_PROFILE_GENERIC_USB)` candidate-list branch to also cover `UAC_PROFILE_FTX1`, and extend the `if (s_profile != UAC_PROFILE_QMX) continue;` speaker-connect guard to also accept `UAC_PROFILE_FTX1`.
**Trade-offs:** The FIFO partitioning in `usb_lib_task()` (`fifo_settings_custom`) is currently hardcoded for QMX's known 24-bit/48kHz/stereo MPS. Until FTX-1's actual USB Audio descriptor is measured on hardware, this partitioning is a guess; expect a re-tune pass once real endpoint sizes are known (flagged in PROJECT.md already).

## Data Flow

### CAT command flow (frequency sync, PTT, tune, time)

```
main.cpp (tx_tick / check_slot_boundary / sync)
    ↓ calls through radio_control_ops_t
radio_control_ftx1.cpp: ftx1_sync_frequency_mode() / ftx1_begin_tx() / ftx1_end_tx()
    ↓ builds ASCII command string, calls cat_vcp_send()
stream_uac.cpp: cat_vcp_send() — thin adapter
    ↓ CdcAcmDevice::tx_blocking() (C++ VCP object, wraps cdc_acm_host transport)
ESP-IDF usb_host_vcp / usb_host_cp210x_vcp / usb_host_cdc_acm
    ↓ bulk-OUT transfer
FTX-1 CP210x CAT-1 (Enhanced COM) port
```
No CAT response parsing exists today for QMX either (fire-and-forget sends with fixed timeouts) — FTX-1 should follow the same fire-and-forget pattern initially; do not introduce a response-parsing state machine unless a specific FTX-1 command requires read-back (none of the in-scope commands do).

### Audio flow (RX and TX)

```
FTX-1 USB Audio IN (mic)  →  usb_host_uac  →  uac_host_device_read()
    →  stream_uac_task() / uac_read_ft8_samples()  →  resample to 6 kHz
    →  ft8_audio_pipeline_run()  →  ft8_lib decode  →  (existing pipeline, UNCHANGED)

DDS/CPFSK tone generation (existing, UNCHANGED)
    →  spk_writer_task()  →  uac_host_device_write()
    →  usb_host_uac  →  FTX-1 USB Audio OUT (speaker)
```
This entire path is profile-agnostic already (`s_format` read at runtime, not hardcoded) — FTX-1 needs zero changes here beyond being allowed into the speaker-pre-open and candidate-negotiation branches noted in Pattern 3.

### Key Data Flows

1. **CAT command flow:** one-way, main-loop-initiated, fire-and-forget, blocking send with short timeout (200 ms) — matches existing QMX/QDX pattern exactly.
2. **Audio RX/TX flow:** fully reused from existing QMX-style bidirectional UAC path; only the format-candidate list and enum guard conditions need broadening to include FTX-1.
3. **Driver install flow (new):** at `usb_lib_task()` startup, profile determines which USB class drivers get installed before the host event loop starts — QMX installs plain `cdc_acm_host`; FTX-1 must install `cdc_acm_host` **and** register the CP210x VCP driver on top of it (see Anti-Pattern 1 below for the risk here).

## Anti-Patterns

### Anti-Pattern 1: Assuming VCP replaces CDC-ACM instead of extending it

**What people might do:** Write FTX-1's `usb_lib_task()` branch to skip `cdc_acm_host_install()` entirely on the theory that "FTX-1 isn't CDC-ACM, it's vendor-class CP210x."
**Why it's wrong:** Espressif's own component description states `usb_host_vcp` "extends the CDC-ACM driver" — the VCP service and CP210x driver are built on top of `cdc_acm_host`, not a replacement stack. Skipping `cdc_acm_host_install()` will likely make `VCP::open()` fail or misbehave.
**Do this instead:** Keep the existing `cdc_acm_host_install()` call for the FTX-1 profile too, and additionally call `VCP::register_driver<UsbCp210x>()` (or equivalent per-chip registration) before entering the host event loop, then use `VCP::open(vid, pid, &dev_config)` in place of `cdc_acm_host_open()`. Validate this sequencing against the official `cdc_acm_vcp` example in `esp-idf/examples/peripherals/usb/host/cdc/cdc_acm_vcp` during implementation — this detail is MEDIUM confidence from docs alone and should be hardware/example-verified before committing to the design.

### Anti-Pattern 2: Letting C++ VCP types leak into `radio_control_backend.h` or `main.cpp`

**What people might do:** Store a `CdcAcmDevice*` as a global in `main.cpp`, or add a `CdcAcmDevice*` parameter to a `radio_control_ops_t` function.
**Why it's wrong:** Breaks the project's existing convention that the radio-control vtable and its consumers are USB-stack-agnostic (QMX/QDX code never touches `usb_host` types directly — everything goes through `cat_cdc_ready()`/`cat_cdc_send()` free functions in `stream_uac.cpp`). Leaking C++ class types outward also risks ODR/include issues if any consumer TU is compiled with different flags.
**Do this instead:** Keep every `usb_host_vcp` type fully local to `stream_uac.cpp` (static file-scope variables, same as `s_cdc_handle`), expose only `cat_vcp_ready()` / `cat_vcp_send()` as the crossing point — exact mirror of the existing QMX pattern.

### Anti-Pattern 3: Standing up a second USB-host task pair for FTX-1

**What people might do:** Create `usb_lib_task_ftx1()` / `uac_lib_task_ftx1()` as new tasks parallel to the existing ones, reasoning "it's a different radio, give it its own lifecycle."
**Why it's wrong:** The ESP32-S3 has exactly one USB-OTG peripheral; only one `usb_host_install()` can be active. The existing architecture already handles "one radio connected at a time" via profile switching in a single task pair (`uac_start_with_profile()` / `uac_stop()`), which is called once at boot based on configured radio type. A second task pair would double USB host state and cannot coexist with the peripheral singleton.
**Do this instead:** Add `UAC_PROFILE_FTX1` as a third value to the existing enum and branch inside the existing `usb_lib_task()`/`uac_lib_task()`, exactly as `UAC_PROFILE_GENERIC_USB` already does alongside `UAC_PROFILE_QMX`.

## Build Order (dependency-driven)

1. **Vtable plumbing (no hardware needed).** Add `radio_control_ftx1_get_ops()` declaration to `radio_control_backend.h`, stub implementation in `radio_control_ftx1.cpp` (commands can be written and unit-checked against the Yaesu CAT reference without a live radio), and wire `"ftx1"` into station config / `g_radio` selection in `main.cpp`. This is buildable and code-reviewable before any USB work starts, and is a strict prerequisite for everything else being reachable via the existing radio-selection path.
2. **`usb_host_vcp` + CP210x dependency integration (hardware needed to confirm VID/PID and driver-install sequencing).** Add `idf_component.yml` dependency, extend `uac_stream_profile_t` with `UAC_PROFILE_FTX1`, implement the CDC-ACM+VCP dual-install branch in `usb_lib_task()`, implement `cat_vcp_ready()`/`cat_vcp_send()` adapters. This depends on nothing else in this list but blocks step 3.
3. **CAT command wiring (needs step 1 + step 2).** Fill in `radio_control_ftx1.cpp`'s real command bodies (`FA`, `MD0C`, `TX0/TX1`, `PC`, `TM`) calling through `cat_vcp_send()`. Testable against real hardware once step 2's VCP channel opens reliably.
4. **Bidirectional UAC audio negotiation (needs step 2's profile scaffolding, independent of step 3).** Extend the mic candidate-list and speaker pre-open guards to include `UAC_PROFILE_FTX1`; validate real descriptor (sample rate/bit depth/channel count) against hardware; re-tune `fifo_settings_custom` if needed for the CP210x bulk endpoints coexisting with bidirectional ISO audio.
5. **End-to-end integration and parity testing (needs 1–4 complete).** Full FT8/FT4 RX decode + autoseq + TX through FTX-1; this is where FIFO/timing issues from combining CP210x bulk CAT with bidirectional 24-bit/48kHz audio (a first for this codebase — QMX pairs CDC-ACM interrupt+bulk CAT with the same audio, not CP210x) will surface, per the PROJECT.md-flagged USB bandwidth risk.

Steps 3 and 4 can proceed in parallel once step 2 lands, since CAT and audio are independent USB interfaces on the same device. Step 2 is the highest-risk, highest-uncertainty step (new non-CDC-ACM dependency, first C++ USB API in the project) and should be validated against the official `cdc_acm_vcp` ESP-IDF example early, ideally before writing FTX-1-specific CAT logic.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| Espressif `usb_host_vcp` component | `idf_component.yml` managed dependency, added via `idf.py add-dependency "espressif/usb_host_cp210x_vcp^2.2.0"` | Confirmed present in ESP Component Registry (v2.0.0, v2.2.0). Extends `cdc_acm_host`, does not replace it (MEDIUM confidence — docs-level, not source-verified) |
| Yaesu FTX-1 CAT-1 (Enhanced COM) | Kenwood-ASCII, `;`-terminated, 38400 8N1–2 by default | Same command family as QMX/QDX — reuse blocking-send-with-timeout pattern, no response parsing needed for in-scope commands |
| Yaesu FTX-1 USB Audio | `usb_host_uac`, existing driver, format TBD | Must go through candidate-negotiation loop (GENERIC_USB-style), not QMX's hardcoded strict format |

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| `main.cpp` ↔ `radio_control_ftx1.cpp` | `radio_control_ops_t` vtable calls (same as QMX/QDX/KH1) | No new mechanism; add one more `get_ops()` getter and one more `g_radio` case |
| `radio_control_ftx1.cpp` ↔ `stream_uac.cpp` | `cat_vcp_ready()` / `cat_vcp_send()` free-function adapter (new, mirrors `cat_cdc_ready()/cat_cdc_send()`) | Keeps C++ VCP/CdcAcmDevice types fully inside `stream_uac.cpp` |
| `stream_uac.cpp` ↔ ESP-IDF USB host stack | `usb_host_vcp`/`usb_host_cp210x_vcp` (new, C++) alongside existing `cdc_acm_host`/`uac_host` (C) | First C++ USB API surface in this project; contain it, don't let it spread |
| `main.cpp` ↔ station config / UI | Existing radio-type selection enum + persistence | Add `"ftx1"` value; no new config subsystem |

## Sources

- [espressif/usb_host_cp210x_vcp — ESP Component Registry](https://components.espressif.com/components/espressif/usb_host_cp210x_vcp) — confirms component exists, is Espressif-maintained, versioned (v2.0.0, v2.2.0), installed via `idf.py add-dependency`. MEDIUM confidence (registry metadata page, not full source).
- [esp-idf/examples/peripherals/usb/host/cdc/cdc_acm_vcp — GitHub](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/cdc/cdc_acm_vcp) — official example demonstrating VCP+CP210x usage pattern; not fully fetched (404 on direct raw file read during this research pass) — **flagged for direct review before implementation**.
- `usb_host_vcp` header (`vcp.hpp`) fetched during this research — confirms `VCP::register_driver<T>()` and `VCP::open(vid, pid, dev_config, iface_idx)` returning `CdcAcmDevice*`. MEDIUM confidence (header-level, send/receive/line-coding methods not directly observed — inferred to live on `CdcAcmDevice`, which the docs describe as CDC-ACM-compatible).
- Project source: `C:\GitHub\Mini-FT8\main\radio_control_qmx.cpp`, `C:\GitHub\Mini-FT8\main\stream_uac.cpp`, `C:\GitHub\Mini-FT8\main\radio_control_backend.h` — HIGH confidence, direct read of existing patterns being extended.
- `C:\GitHub\Mini-FT8\.planning\PROJECT.md` — HIGH confidence, project-level requirements and constraints already captured by the user (CAT command set, CP210x chip identification, single-port design decision).

---
*Architecture research for: Yaesu FTX-1 radio backend integration (Mini-FT8)*
*Researched: 2026-07-04*
