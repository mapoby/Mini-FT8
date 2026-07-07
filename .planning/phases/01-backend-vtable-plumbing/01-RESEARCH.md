# Phase 1: Backend Vtable Plumbing - Research

**Researched:** 2026-07-04
**Domain:** ESP-IDF/C++ firmware — vtable-based radio backend abstraction, enum plumbing, station config persistence
**Confidence:** HIGH

## Summary

This phase is a pure code-structure task with no external dependencies: add a fourth radio backend (`FTX-1`) to an existing, small, and very regular vtable pattern. The codebase already has three backends (`radio_control_qmx.cpp`, `radio_control_qdx.cpp`, `radio_control_kh1_cat.cpp`) that all implement the same 8-function `radio_control_ops_t` struct and register via a `static const radio_control_ops_t k_ops = {...}; const radio_control_ops_t* radio_control_<name>_get_ops(void)` pattern. Adding FTX-1 means: (1) a new `.cpp`/`.h` pair mirroring this pattern with every hook stubbed (`ready()` returns `false`, others return `ESP_ERR_INVALID_STATE`/no-op), (2) a new `RADIO_CONTROL_FTX1` entry in the `radio_control_backend_t` dispatch enum in `radio_control_backend.h` + a `case` in `radio_control.cpp`'s two switch statements, (3) a new `RadioType::FTX1` entry in `station_types.h`'s persisted enum, and (4) updates to roughly a dozen small switch/if-chains in `main.cpp` that enumerate the 4 (soon 5) radio types by name (`radio_name`), by canonical form (`canonical_radio_type`), by saved-int/string parsing (`radio_type_from_saved_int`, `parse_radio_config_value`), by profile binding (`get_radio_profile_binding`), and by the menu's manual cycle-through switch statement (the de facto "station configuration radio list" — there is no separate list widget).

All of this is verified directly from the current source tree (`main/radio_control_backend.h`, `main/radio_control.cpp`, `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`, `main/radio_control_kh1_cat.cpp`, `main/station_types.h`, `main/main.cpp`, `main/CMakeLists.txt`). No web research was needed — this is entirely a codebase-pattern-matching task.

**Primary recommendation:** Create `radio_control_ftx1.cpp`/`.h` mirroring `radio_control_qdx.cpp` exactly (same 8-function shape, same `k_ops` static struct, same `_get_ops()` accessor), with every function body stubbed to a safe no-op/false/ESP_ERR_INVALID_STATE, wire it into the two dispatch switch statements in `radio_control.cpp`, add `RADIO_CONTROL_FTX1` to `radio_control_backend.h`, add `RadioType::FTX1 = 6` to `station_types.h`, and thread it through every `main.cpp` switch/if-chain that currently enumerates QMX/QDX/KH1_USBC/KH1_MIC. Register the new file in `main/CMakeLists.txt`'s `SRCS` list.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Radio backend selection (persisted choice) | Application/Firmware (main.cpp globals + Station.txt) | — | `g_radio` (RadioType) is main.cpp's owned global, persisted via storage_service; this is firmware-local config, no network/API tier exists in this embedded app |
| Vtable dispatch (which backend's ops run) | Radio Control layer (`radio_control.cpp`) | — | Central dispatcher owns the single `switch(s_backend)`; individual backend `.cpp` files never know about each other |
| Backend implementation (CAT commands, stubs) | Radio Control Backend (`radio_control_ftx1.cpp`) | — | Each backend is self-contained; for Phase 1 this is 100% stub, no CAT I/O |
| UI display of selected radio name | UI/Menu (`main.cpp` menu draw functions) | — | `radio_name()` / `draw_menu_view()` are firmware-side text rendering, no separate `components/ui/ui.cpp` involvement found for radio selection |
| Config persistence (Station.txt) | Storage (`storage_service`) | Application (`main.cpp` save/load functions) | `save_station_data()`/load routines in main.cpp read/write the `radio=<int>` line via `storage_service`'s stream API |

## Standard Stack

### Core
No new libraries or dependencies are required for this phase. This is internal code structure only — same C++17/ESP-IDF v5.5.1 toolchain already in use.

### Supporting
N/A — no new components needed. `radio_control_ftx1.cpp` will use the exact same includes as its siblings (`radio_control_backend.h`, `<cstdio>`, `<cstring>`, `esp_log.h`); it should NOT include `stream_uac.h` or attempt any CAT I/O in Phase 1, since `usb_host_cp210x_vcp` (the CP210x transport) is a Phase 2 dependency per REQUIREMENTS.md CAT-01. `[VERIFIED: codebase — main/CMakeLists.txt REQUIRES list, main/radio_control_qmx.cpp]`

### Alternatives Considered
None — the vtable/switch pattern is already fully established by 3 existing backends; there is no reason to introduce a different registration mechanism (e.g. a table/array-based lookup) for a 4th backend that must match the existing style exactly, per CLAUDE.md's mandate to mirror the QMX/KH1 pattern.

**Installation:** N/A — no package installation needed.

## Architecture Patterns

### System Architecture Diagram

```
Station Config Menu (main.cpp draw_menu_view / keyboard handler)
        │  user presses '3' → cycles g_radio through RadioType enum
        ▼
g_radio (RadioType, main.cpp global) ──save_station_data()──> Station.txt ("radio=<int>")
        │
        ▼
apply_radio_profile_binding()
  ├─ canonical_radio_type(g_radio)         [normalizes unknown/legacy values]
  ├─ get_radio_profile_binding(radio)      [RadioType -> {audio_backend, radio_control_backend}]
  ├─ audio_source_set_backend(...)          [selects AUDIO_SOURCE_* — audio pipeline, out of scope Phase 1/4]
  └─ radio_control_set_backend(...)         [selects RADIO_CONTROL_* dispatch target]
                │
                ▼
      radio_control.cpp: current_ops()
        switch (s_backend) {
          RADIO_CONTROL_QMX      -> radio_control_qmx_get_ops()
          RADIO_CONTROL_QDX      -> radio_control_qdx_get_ops()
          RADIO_CONTROL_KH1_CAT  -> radio_control_kh1_get_ops()
          RADIO_CONTROL_FTX1     -> radio_control_ftx1_get_ops()   [NEW, Phase 1]
        }
                │
                ▼
      radio_control_ftx1.cpp: k_ops (radio_control_ops_t)
        .ready = ftx1_ready              -> returns false (stub, no hardware)
        .on_audio_start = ...            -> ESP_ERR_INVALID_STATE / no-op
        .sync_frequency_mode = ...       -> ESP_ERR_INVALID_STATE / no-op
        .begin_tx / set_tone_hz / end_tx / set_tune / set_time -> stubs
```

### Recommended Project Structure
```
main/
├── radio_control_backend.h      # add radio_control_ftx1_get_ops() declaration
├── radio_control.h              # add RADIO_CONTROL_FTX1 to radio_control_backend_t enum
├── radio_control.cpp            # add RADIO_CONTROL_FTX1 case to current_ops() and radio_control_backend_name()
├── radio_control_qmx.cpp        # existing pattern to mirror
├── radio_control_qdx.cpp        # existing pattern to mirror (closest match: no UART, uses cat_cdc_ready() style)
├── radio_control_kh1_cat.cpp    # existing pattern (not the best mirror — UART-based, KH1-specific tone math)
├── radio_control_ftx1.cpp       # NEW — stub implementation
├── radio_control_ftx1.h         # NEW — only needed if FTX1 needs extra public API like KH1's set_enabled; likely NOT needed since ops accessor already declared in radio_control_backend.h
├── station_types.h              # add RadioType::FTX1 = 6
├── main.cpp                     # extend radio_name(), canonical_radio_type(), radio_type_from_saved_int(),
│                                 #   parse_radio_config_value(), get_radio_profile_binding(), and the menu
│                                 #   cycle switch (search for "case RadioType::KH1_MIC:" around line 5484)
└── CMakeLists.txt               # add "radio_control_ftx1.cpp" to SRCS
```

### Pattern 1: Backend ops struct + accessor (mirror exactly)
**What:** Each backend defines a file-static `const radio_control_ops_t k_ops` aggregate and exposes it via one accessor function declared in `radio_control_backend.h`.
**When to use:** Always, for every backend — this IS the registration mechanism (no dynamic registration, no table).
**Example:**
```cpp
// Source: main/radio_control_qdx.cpp (existing pattern, verified in this session)
static const radio_control_ops_t k_ops = {
    .name = "qdx",
    .ready = qdx_ready,
    .on_audio_start = qdx_on_audio_start,
    .sync_frequency_mode = qdx_sync_frequency_mode,
    .begin_tx = qdx_begin_tx,
    .set_tone_hz = qdx_set_tone_hz,
    .end_tx = qdx_end_tx,
    .set_tune = qdx_set_tune,
    .set_time = qdx_set_time,
};

const radio_control_ops_t* radio_control_qdx_get_ops(void) {
    return &k_ops;
}
```
For FTX-1, every function body should be a minimal stub. Recommended stub bodies (matches the "ready() returns false" success criterion and the existing pattern of `ESP_ERR_INVALID_STATE` for not-yet-connected/not-ready backends):
```cpp
// FTX-1 stub — Phase 1 only, no hardware I/O
static const char* TAG = "RADIO_FTX1";

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
`[VERIFIED: main/radio_control_qdx.cpp, main/radio_control_qmx.cpp, main/radio_control_backend.h read this session]`

### Pattern 2: Central dispatcher switch (radio_control.cpp)
**What:** `radio_control.cpp` owns a single `static radio_control_backend_t s_backend` and two switch statements (`current_ops()`, `radio_control_backend_name()`) that must both add a `case RADIO_CONTROL_FTX1:`.
**When to use:** Every new backend requires editing both switches in this one file — nowhere else has this specific dispatch logic duplicated.
**Example:**
```cpp
// Source: main/radio_control.cpp (existing, to be extended)
static const radio_control_ops_t* current_ops(void) {
    switch (s_backend) {
    case RADIO_CONTROL_KH1_CAT:
        return radio_control_kh1_get_ops();
    case RADIO_CONTROL_QDX:
        return radio_control_qdx_get_ops();
    case RADIO_CONTROL_FTX1:                       // NEW
        return radio_control_ftx1_get_ops();       // NEW
    case RADIO_CONTROL_QMX:
    default:
        return radio_control_qmx_get_ops();
    }
}
```
`[VERIFIED: main/radio_control.cpp read this session]`

### Pattern 3: RadioType (persisted station config enum) vs radio_control_backend_t (runtime dispatch enum) — two separate enums
**What:** `station_types.h`'s `RadioType` (persisted to Station.txt as `radio=<int>`, drives UI/menu) is a *different* enum from `radio_control_backend.h`'s `radio_control_backend_t` (drives CAT dispatch only). `main.cpp`'s `get_radio_profile_binding()` is the single translation function mapping one to the other (plus an `audio_source_backend_t`).
**When to use:** Both enums need a new value for FTX-1; they are intentionally decoupled (e.g. `KH1_USBC` and `KH1_MIC` are two different `RadioType`s that both map to the single `RADIO_CONTROL_KH1_CAT` backend).
**Example:**
```cpp
// station_types.h — existing enum, append FTX1
enum class RadioType {
    NONE = 0,
    TRUSDX = 1,
    QMX = 2,
    KH1_USBC = 3,
    KH1 = KH1_USBC,
    KH1_MIC = 4,
    QDX = 5,
    FTX1 = 6,          // NEW
};
```
`[VERIFIED: main/station_types.h read this session]`

### Anti-Patterns to Avoid
- **Adding FTX-1 handling to only some of the switch/if-chains in main.cpp:** There are at least 6 distinct places in `main.cpp` that enumerate radio types (`canonical_radio_type`, `radio_type_from_saved_int`, `parse_radio_config_value`, `get_radio_profile_binding`, `radio_name`, and the menu cycle switch around line 5474-5488). Missing any one silently downgrades FTX-1 to QMX via the `default:` fallback in most of them — this fails success criterion 3 ("does not... affect QMX/QDX/KH1 selection paths") only if done wrong, but more importantly means FTX-1 "selection" silently becomes QMX selection, which is a functional failure even though it "doesn't crash."
- **Giving FTX-1's `.h` file KH1-style extra public API** (like `radio_control_kh1_set_enabled`/`is_enabled`) unless actually needed: KH1 needs this because it shares UART1 with GPS and needs enable/disable lifecycle. FTX-1 in Phase 1 has no UART/hardware resource contention (it will use CP210x VCP host driver, not a shared UART), so a bare `radio_control_ftx1_get_ops()` declaration in `radio_control_backend.h` is likely sufficient — a separate `radio_control_ftx1.h` may not be needed at all in Phase 1 (see Open Questions).
- **Making `ready()` return true or partially implementing CAT I/O "for testing":** the phase's explicit success criterion is `ready()` returns `false` — do not attempt real hardware calls yet; `stream_uac.h`/`cat_cdc_ready()`-style calls belong to Phase 2+ once `usb_host_cp210x_vcp` exists.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Radio backend registration | A new registration mechanism (factory, self-registering static init, table of function pointers indexed dynamically) | The existing static `k_ops` struct + `switch` dispatch pattern | Three backends already use this exact pattern; introducing a different mechanism for a 4th backend breaks the "mirror QMX/KH1" requirement in CLAUDE.md and REQUIREMENTS.md PLUMB-02 |
| Enum-to-enum translation (RadioType -> radio_control_backend_t) | A new mapping table/lookup | Extend the existing `get_radio_profile_binding()` switch in main.cpp | Single source of truth already exists; don't duplicate |

**Key insight:** This phase has zero "hand-roll vs library" tension — it is pure mechanical extension of an existing hand-rolled (and intentionally so, for this embedded codebase) pattern. The risk is exclusively about *completeness* (missing one of the ~6-8 enumeration sites), not about picking the wrong tool.

## Common Pitfalls

### Pitfall 1: `canonical_radio_type()` silently downgrades unknown RadioTypes to QMX
**What goes wrong:** `canonical_radio_type()` in main.cpp only recognizes `QDX`, `KH1_USBC`, `KH1_MIC` as "not QMX"; everything else (including the new `FTX1`) falls through to `return RadioType::QMX;`. If this function isn't updated, selecting FTX-1 will silently behave as QMX everywhere `canonical_radio_type()` is used (which is nearly everywhere: `is_kh1_radio`, `get_radio_profile_binding`, `radio_name`, `apply_radio_profile_binding`, status line, etc.)
**Why it happens:** The function was written as an explicit allow-list rather than an exhaustive `switch` with a compiler-enforced default, so adding a new enum value doesn't produce a compile warning/error here.
**How to avoid:** Add `RadioType::FTX1` to the `canonical_radio_type()` allow-list as the very first edit; verify by grepping all functions that call `canonical_radio_type` and confirming FTX-1 flows through correctly (not silently becoming QMX).
**Warning signs:** After adding FTX-1 to the menu cycle, the "Radio:" line in the config menu shows "QMX" instead of "FTX-1" (or an FTX-1 entry is unreachable/skipped in the cycle).

### Pitfall 2: `switch` statements without `default:` (or with `default:` that maps to QMX) don't produce compiler warnings for missing new enum values
**What goes wrong:** `radio_type_from_saved_int()`, `get_radio_profile_binding()`, `radio_name()`, and the menu cycle switch (main.cpp ~5474) all have explicit `default:` cases (mapping to QMX or falling through). Because `-Wswitch` warnings require an *absence* of `default:` to fire for missing enum cases, these switches will compile silently even if FTX-1 is never added to them.
**Why it happens:** `default:` was added intentionally for forward/backward-compat with unknown persisted ints (Station.txt corruption resilience), but it also suppresses the exhaustiveness check that would otherwise catch a missed case.
**How to avoid:** Do not rely on compiler warnings to find all switch sites — use the explicit list of files/functions/line numbers below (from a full grep of the codebase) as a checklist, and manually verify each.
**Warning signs:** Build succeeds with zero warnings even if FTX-1 support is incomplete — this is a "grep audit," not a "let the compiler tell you" task.

### Pitfall 3: Two independent enums (`RadioType` and `radio_control_backend_t`) plus a third (`CoreRadioType` in `core_api.h`) can drift out of sync
**What goes wrong:** `core_api.h` defines a THIRD enum, `CoreRadioType { QMX, KH1, QDX }`, with `map_in`/`map_out` translation functions in `core_api.cpp` (lines 152-160). This enum currently has no KH1_MIC-vs-KH1_USBC distinction and no FTX-1. However, `core_cmd_set_radio()` (the only consumer of `CoreRadioType`) has ZERO callers anywhere in the codebase `[VERIFIED: grep across full repo found only the declaration and definition, no call sites]` — it appears to be a dead/future API surface, not currently wired to the menu-based radio selection.
**Why it happens:** `core_api.h`/`.cpp` was built as a UI-agnostic "functional core" layer for a future control surface, separate from the current keyboard-menu-driven `main.cpp` flow that directly manipulates `g_radio`.
**How to avoid:** For Phase 1, it is safe to leave `CoreRadioType`/`map_in`/`map_out` unchanged since success criteria only require the config-menu path (`g_radio`, `RadioType`) to work — but flag this in the plan as a known gap so a future phase (or code reviewer) doesn't assume `core_cmd_set_radio` already supports FTX-1.
**Warning signs:** None currently reachable at runtime since there are no callers; this is a completeness/consistency concern for future-proofing only, not a functional bug for Phase 1's success criteria.

### Pitfall 4: Forgetting to register the new `.cpp` file in `main/CMakeLists.txt`
**What goes wrong:** `main/CMakeLists.txt` has an explicit `SRCS` list (not a glob) that currently lists `radio_control.cpp`, `radio_control_qmx.cpp`, `radio_control_qdx.cpp`, `radio_control_kh1_cat.cpp`. A new `radio_control_ftx1.cpp` will not compile/link unless added to this list — this would produce an obvious linker error, not silent failure.
**How to avoid:** Add `"radio_control_ftx1.cpp"` to the `SRCS` list in `main/CMakeLists.txt` (line 10, after `radio_control_kh1_cat.cpp`) as one of the first edits.
**Warning signs:** Linker error `undefined reference to radio_control_ftx1_get_ops` if this step is skipped.

## Code Examples

### Full dispatch enum extension
```cpp
// Source: main/radio_control_backend.h (existing) — add declaration
const radio_control_ops_t* radio_control_ftx1_get_ops(void);

// Source: main/radio_control.h (existing) — add enum value
typedef enum {
    RADIO_CONTROL_QMX = 0,
    RADIO_CONTROL_KH1_CAT = 1,
    RADIO_CONTROL_QDX = 2,
    RADIO_CONTROL_FTX1 = 3,
} radio_control_backend_t;
```

### Menu cycle switch extension (main.cpp ~line 5474)
```cpp
// Source: main/main.cpp (existing cycle handler for menu key '3') — extend
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
    g_radio = RadioType::FTX1;      // NEW — insert before wraparound
    break;
  case RadioType::FTX1:
  default:
    g_radio = RadioType::QMX;
    break;
}
```

### Station.txt round-trip (int-based persistence, already generic)
```cpp
// Source: main/main.cpp — radio_type_from_saved_int() and parse_radio_config_value()
// already read/write RadioType as a plain int; only the switch/if-chain needs
// an FTX1 branch, the persistence mechanism itself (storage_service stream
// read/write) requires no changes.
```

## State of the Art

Not applicable — no external ecosystem/library versioning is relevant to this phase; it is entirely internal code-pattern extension.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | A separate `radio_control_ftx1.h` file is not required in Phase 1 (declaration lives in `radio_control_backend.h` like QMX/QDX, not a KH1-style dedicated header) | Anti-Patterns / Recommended Project Structure | Low — if the planner decides FTX-1 needs its own header (e.g. for a future `radio_control_ftx1_set_enabled` mirroring KH1), it's a small addition, not a rework |
| A2 | `CoreRadioType`/`core_api.h` does not need FTX-1 added in Phase 1 because it currently has no callers | Pitfall 3 | Low — if a future phase or hidden caller does use `core_cmd_set_radio`, FTX-1 would be unselectable through that path only; the primary menu-driven UI path is unaffected |
| A3 | The FTX-1 numeric enum value should be `RadioType::FTX1 = 6` (next available after `QDX = 5`) and `RADIO_CONTROL_FTX1 = 3` (next available after `RADIO_CONTROL_QDX = 2`) | Code Examples, Pattern 3 | Low — these are arbitrary but must be stable once persisted to Station.txt; if the planner picks different numeric values that's fine as long as they don't collide with existing ones (0-5 for RadioType, 0-2 for radio_control_backend_t are taken) |

**If this table is empty:** N/A — see entries above; all are low-risk implementation-detail assumptions, not requirements-level ambiguities.

## Open Questions (RESOLVED)

1. **RESOLVED — Should `radio_control_ftx1.cpp` mirror QDX's minimal style or KH1's UART-lifecycle style?**
   - What we know: QDX has no special enable/disable lifecycle (just `cat_cdc_ready()` checks via the shared CDC-ACM path); KH1 has a UART1 enable/disable lifecycle because it shares hardware with GPS.
   - What's unclear: FTX-1 will eventually use a CP210x VCP (Phase 2), which is a distinct USB host driver, not a UART. It's unclear if it will need its own enable/disable lifecycle function analogous to `radio_control_kh1_set_enabled` once Phase 2 lands.
   - Resolution: For Phase 1 (stub-only, no hardware), mirror the simpler QDX style — no enable/disable lifecycle function needed yet, since there's no shared hardware resource to arbitrate in Phase 1. Adopted in 01-01-PLAN.md. Revisit in Phase 2 when USB bring-up work begins.

2. **RESOLVED — Exact numeric enum values for `RadioType::FTX1` and `RADIO_CONTROL_FTX1`.**
   - What we know: `RadioType` explicitly documents "Keep explicit values stable for station.txt radio= saves" — so whatever value is chosen must not collide with existing persisted values (0=NONE, 1=TRUSDX, 2=QMX, 3=KH1_USBC, 4=KH1_MIC, 5=QDX).
   - What's unclear: Nothing technically — next available value is 6. Flagging only so the planner locks this value in the plan/task description explicitly (since it becomes a persisted on-disk format detail).
   - Resolution: Use `RadioType::FTX1 = 6` and `RADIO_CONTROL_FTX1 = 3`. Locked in 01-01-PLAN.md `<interfaces>` block.

## Environment Availability

Skipped — this phase has no external tool/service/runtime dependencies. It is a pure code-structure change compiled with the project's existing ESP-IDF v5.5.1 toolchain (already required and presumably available in the dev environment; no new SDK/component/service is introduced until Phase 2's `usb_host_cp210x_vcp`).

## Security Domain

Security enforcement is enabled in `.planning/config.json` (`security_enforcement: true`, ASVS level 1), but this phase has no attack surface: it adds a stubbed, unreachable-by-hardware code path (`ready()` always `false`) with no network exposure, no user-supplied untrusted input beyond the existing Station.txt int-parsing (already covered by existing bounds-checked `parse_radio_config_value`/`radio_type_from_saved_int`), and no cryptography, auth, or session concerns. No new ASVS categories apply beyond what already governs Station.txt parsing (V5 Input Validation — already satisfied by the existing `strtol`/allow-list pattern, unchanged by this phase).

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V5 Input Validation | yes (pre-existing, unchanged) | Existing `parse_radio_config_value`/`radio_type_from_saved_int` allow-list parsing of Station.txt `radio=` line — extend the allow-list, don't weaken it |
| V2/V3/V4/V6 | no | No auth, session, access-control, or cryptography surface in this phase |

## Sources

### Primary (HIGH confidence)
- `main/radio_control_backend.h` — vtable struct definition (read this session)
- `main/radio_control.h` — dispatch enum definition (read this session)
- `main/radio_control.cpp` — dispatch switch statements (read this session)
- `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`, `main/radio_control_kh1_cat.cpp` — existing backend implementations (read this session)
- `main/station_types.h` — persisted `RadioType` enum (read this session)
- `main/main.cpp` (lines ~570-1780, ~3470-3560, ~4180-4390, ~5450-5510) — all radio-type enumeration/persistence/menu sites (read and grepped this session)
- `main/core_api.h`, `main/core_api.cpp` — `CoreRadioType` mapping layer, confirmed no callers of `core_cmd_set_radio` (grepped this session)
- `main/CMakeLists.txt` — SRCS registration list (read this session)
- `.planning/REQUIREMENTS.md`, `.planning/STATE.md`, `.planning/ROADMAP.md` — phase scope and traceability (read this session)

### Secondary (MEDIUM confidence)
None — no web/external sources were needed for this phase.

### Tertiary (LOW confidence)
None.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new stack, pure existing-pattern extension, fully verified against source
- Architecture: HIGH — dispatch pattern and all enumeration sites directly read/grepped from the codebase this session
- Pitfalls: HIGH — all four pitfalls are drawn from concrete, cited code (not speculative), including the dead `CoreRadioType` API surface which was verified via full-repo grep

**Research date:** 2026-07-04
**Valid until:** Stable — this research is tied to the current state of `main/*.cpp`/`.h`, which will change once Phase 1 itself is implemented; re-verify only if the codebase changes materially before planning begins (e.g. an unrelated PR touching `radio_control.cpp`).
