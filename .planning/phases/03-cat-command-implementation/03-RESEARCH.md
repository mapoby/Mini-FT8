# Phase 3: CAT Command Implementation - Research

**Researched:** 2026-07-06
**Domain:** Yaesu FTX-1 CAT (Computer Aided Transceiver) ASCII protocol over the already-open CP210x CAT-1 virtual COM port; `radio_control_ftx1.cpp` vtable implementation; existing `radio_control_*` backend conventions
**Confidence:** HIGH for command syntax (verified against the official Yaesu FTX-1 CAT Operation Reference Manual, fetched and read directly this session) and for all codebase integration points (read directly from the repo). MEDIUM/LOW for two hardware-gated behaviors flagged explicitly below (CAT-1 vs CAT-2 PTT scoping, line-coding/baud-rate requirement) — both require physical-hardware confirmation in a checkpoint plan, same pattern as Phase 2's 02-03.

## Summary

Phase 3's job is to give `radio_control_ftx1.cpp` a real CAT command implementation on top of the CP210x CAT-1 port Phase 2 already opens, deasserts RTS/DTR on, and reports ready via `cat_cdc_ready()`. The command layer itself is almost entirely mechanical — reuse the exact same `cat_cdc_send()`/`cat_cdc_ready()` functions QMX and QDX already call (no new transport code needed) — but the **Yaesu CAT protocol's on-the-wire syntax genuinely differs from the Kenwood-style protocol QMX/QDX/KH1 use**, in three ways that will silently produce invalid or ignored commands if copy-pasted from the existing backends:

1. **`FA` frequency field is 9 digits, not 11.** QMX/QDX use `snprintf(fa, sizeof(fa), "FA%011d;", freq_hz)` (11-digit field, Kenwood convention). The FTX-1's official CAT manual specifies `FA` + exactly 9 digits + `;` (`FA000030000;` … `FA470000000;`), matching REQUIREMENTS.md's SYNC-01 wording exactly. Using `%011d` against the FTX-1 produces a malformed 14-character command the radio will reject or misparse. `[CITED: FTX-1 CAT Operation Reference Manual, "FA FREQUENCY VFO MAIN-SIDE" table]`
2. **`MD` requires an explicit VFO-side parameter QMX/QDX don't have.** QMX/QDX send bare `"MD6;"` (mode digit only). The FTX-1's `MD` command is `MD` + P1 (`0`=MAIN-side, `1`=SUB-side) + P2 (mode code) + `;` — so DATA-U is `"MD0C;"` (P1=`0`, P2=`C`), exactly matching REQUIREMENTS.md SYNC-02. Sending `"MDC;"` (P1 omitted) is a malformed command per the "not enough parameters" rule the manual explicitly warns about. `[CITED: same manual, "MD OPERATING MODE" table, mode code `C`=DATA-U]`
3. **`TX` is a single 2-letter command with a P1 flag, not two separate commands.** `TX1;`/`TX0;` are literally the `TX` "TX SET" command with parameter `1` (RADIO TX OFF, **CAT TX ON**) or `0` (RADIO TX OFF, CAT TX OFF). This matches REQUIREMENTS.md/ROADMAP wording exactly, and is structurally the same "2-char command + digit param + `;`" shape as QMX/QDX's `TX;`/`RX;`, just parameterized differently. `[CITED: same manual, "TX TX SET" table]` Note the third value, `P1=2` ("RADIO TX ON, CAT TX OFF"), is documented as **Answer-only** — it is never a value software should *Set*; it is what the radio reports back if TX was engaged by some other means (e.g. front-panel PTT). Do not attempt `qmx_send_cmd`-style code that treats `2` as a settable "force TX" value.

**The single highest-risk open question this research could not resolve from documentation alone:** the manual states "Enhanced COM Port (CAT-1): CAT Communications (Frequency and Communication Mode Settings)" and "Standard COM Port (CAT-2): TX Controls (PTT control, CW Keying, Digital Mode Operation) **or** CAT Communications" — wording that describes CAT-1's function as frequency/mode *only*, with TX control associated with CAT-2. REQUIREMENTS.md/ROADMAP already lock this project into a single-port (CAT-1 only) design (CAT-2 is explicitly Out of Scope). It is not established from the manual whether the `TX` **software command** is actually rejected/ignored on CAT-1, or whether the port-function table is describing *typical/legacy hardware-RTS/DTR-PTT wiring convention* only (the `RPTT SELECT` menu items literally say "OFF/RTS/DTR", i.e. that's what "TX control" via a port historically meant) while the ASCII `TX;` command itself works identically on whichever port has it open, since both ports run through the same MCU command parser. **This must be verified with the physical FTX-1 in a hardware checkpoint before or immediately after this phase's first PTT task** — if `TX1;` over CAT-1 does not key the radio, the single-port architectural decision itself needs revisiting (a decision made at roadmap time, above this phase's authority to silently override).

**Primary recommendation:** Create `radio_control_ftx1.cpp`'s CAT command functions following the exact `qdx_*`/`qmx_*` shape (`ftx1_send_cmd()` wrapping `cat_cdc_send()`, matching `qmx_send_cmd`/`qdx_send_cmd`), but with FTX-1's own command formatting (9-digit `FA`, `MD0<mode>;` with explicit side digit, `TX1;`/`TX0;`). Model `begin_tx`/`set_tone_hz`/`end_tx` after **QDX's** pattern (audio-tone-based TX, no per-symbol CAT tone commands, no VFO shift for TX) rather than QMX's or KH1's (both of which CAT-shift the VFO per TX and/or send a per-symbol `TA`/`FO` tone command) — the FTX-1, like the QDX, will carry its TX audio over USB Audio (Phase 4), not through CAT-encoded tone frequencies. Defensively re-send `FA`+`MD0C` after `TX0;` anyway (SYNC-03), even though the VFO likely never needs to move for a non-split single-VFO design, because Yaesu's own related-model errata (FT-991A/FTDX-10 DATA-U→USB mode-reversion quirk, tracked as v2 HARDEN-01) makes a defensive re-assert cheap insurance until real hardware proves it unnecessary.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SYNC-01 | Firmware sets VFO-A frequency via CAT (`FA<9-digit Hz>;`, range 000030000–470000000 Hz) | Exact command format verified against the official CAT manual: `FA` + 9-digit zero-padded Hz + `;`. Confirmed this is a **9-digit** field (`%09d`), not the 11-digit format QMX/QDX use — a critical formatting difference, not a copy-paste target. |
| SYNC-02 | Firmware sets DATA-U operating mode via CAT (`MD0C;`) once at sync time, not resent per TX | Exact command verified: `MD` + P1(`0`=MAIN-side) + P2(`C`=DATA-U) + `;`. The existing `g_cdc_initial_sync_pending`/`sync_radio_to_current_band()` call-site pattern (main.cpp) already provides the "once at sync, not per-TX" call site — `radio_control_sync_frequency_mode()` is called from `sync_radio_to_current_band()` (band change/initial connect/STATUS exit), never from the TX path (`tx_start()`/`tx_tick()` call `radio_control_begin_tx()`/`radio_control_end_tx()`, never `radio_control_sync_frequency_mode()`). FTX-1's `sync_frequency_mode()` should send `MD0C;` once here; `begin_tx()`/`end_tx()` must NOT resend it. |
| SYNC-03 | Firmware restores RX dial frequency/mode after each TX, mirroring the KH1 backend's post-TX restore pattern | `kh1_end_tx()` (`radio_control_kh1_cat.cpp:161-185`) is the exact analog: force-resend `FA<rx_freq>` (and, for FTX-1, defensively `MD0C;` too) after TX completes/cancels, restoring pre-TX VFO/mode display. Unlike KH1 (which shifts its VFO per-TX and therefore functionally needs this), FTX-1 likely never moves its VFO for TX (single non-split VFO, audio-tone-based TX like QDX) — but SYNC-03's explicit restore requirement, combined with the known Yaesu DATA-U→USB display-reversion quirk on sibling models (v2 HARDEN-01), makes this a defensive re-assert, not a functional necessity. |
| PTT-01 | Firmware keys PTT via CAT (`TX1;`) and unkeys via CAT (`TX0;`) — no RTS/DTR hardware toggling | `TX` command confirmed: single 2-letter command, P1=`1`→"RADIO TX OFF, CAT TX ON" (Set), P1=`0`→"RADIO TX OFF, CAT TX OFF" (Set), P1=`2` is Answer-only (never Set). Phase 2 already deasserts RTS/DTR on open and never re-touches those lines — PTT-01's "no RTS/DTR toggling" constraint is already satisfied by Phase 2's work; Phase 3 only needs to never call `cdc_acm_host_set_control_line_state()` from the CAT layer. **HIGH-RISK OPEN QUESTION:** whether `TX1;`/`TX0;` sent over CAT-1 (this project's only open port, per locked Out-of-Scope decision) actually keys the radio, given the manual's port-function table associates "TX Controls" with CAT-2. Flagged for hardware checkpoint — see Open Questions. |
| PTT-02 | Firmware sequences mode-set before PTT-key, and drains audio before unkeying, mirroring the QMX backend's drain-before-unkey pattern | **Correction to REQUIREMENTS.md's attribution:** the actual "drain audio before unkey" pattern in this codebase lives in **QDX's** `qdx_end_tx()` (`uac_tx_end()` called, then `qdx_send_cmd("RX;")`), not QMX's (`qmx_end_tx()` sends `"RX;"` immediately with no audio-drain call — QMX's TX audio is per-symbol synchronous CAT `TA` commands, not a buffered USB-audio stream, so there is nothing to "drain"). Since FTX-1 will carry TX audio over USB Audio like QDX (Phase 4), FTX-1's `end_tx()` should call `uac_tx_end()` (safe to call even before Phase 4 wires real audio — it no-ops harmlessly when `s_spk_writer_run` is false) **before** sending `TX0;`, mirroring QDX's actual code, not QMX's. "Mode-set before PTT-key" is satisfied structurally by `sync_frequency_mode()` (sends `MD0C;`) always being called before `begin_tx()`/`TX1;` in the existing `sync_radio_to_current_band()` → later `tx_start()` flow — no same-call-site mode+PTT combination is needed. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| CAT ASCII command encoding/formatting (`FA`, `MD`, `TX`) | Firmware — radio backend (`radio_control_ftx1.cpp`) | — | Existing convention: all `*_sync_frequency_mode`/`*_begin_tx`/`*_end_tx` command-string construction lives in the per-radio backend file, never in `stream_uac.cpp` |
| CAT byte transport (send over open CP210x handle) | Firmware — USB orchestration (`stream_uac.cpp`: `cat_cdc_send()`/`cat_cdc_ready()`) | — | Already exists, unchanged from Phase 2; reused verbatim, identical to QMX/QDX |
| CAT-1 line coding (baud rate/data bits/parity) — **new work this phase** | Firmware — USB orchestration (`stream_uac.cpp`: `cp210x_try_open()`) | Firmware — radio backend (defensive re-assert, optional) | Belongs at the transport layer where the handle is opened, consistent with where RTS/DTR deassert (CAT-03) already lives; must happen before the FTX-1 backend's first CAT write, or all FTX-1 CAT traffic in this phase risks being framed at the wrong baud rate |
| TX/PTT state sequencing (mode before key, drain before unkey) | Firmware — radio backend (`radio_control_ftx1.cpp`) + main loop TX state machine (`main.cpp: tx_start()`/`tx_tick()`) | — | Existing convention: `main.cpp` owns *when* `begin_tx`/`end_tx` fire (slot-boundary/tone-schedule driven); the backend owns *what* CAT bytes go out at each call |
| Post-TX frequency/mode restore (SYNC-03) | Firmware — radio backend (`radio_control_ftx1.cpp`: `ftx1_end_tx()`) | — | Mirrors `kh1_end_tx()`'s existing restore-after-TX pattern; no main.cpp changes needed, this is entirely inside the backend's `end_tx()` |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `espressif/usb_host_cdc_acm` | `^2.2.0` (already pinned, Phase 2) | `cdc_acm_host_data_tx_blocking()` (via `cat_cdc_send()`), `cdc_acm_host_line_coding_set()` | No new dependency; this phase only calls existing public API the codebase already links against `[VERIFIED: managed_components/espressif__usb_host_cdc_acm/include/usb/cdc_acm_host.h, read this session]` |
| `espressif/usb_host_cp210x_vcp` | `^2.2.0` (already pinned, Phase 2) | Provides the CP210x-patched `line_coding_set` vtable hook (AN571 `SET_BAUDRATE`/`SET_LINE_CTL` vendor requests) | Confirmed by reading `usb_host_cp210x_vcp.c` directly this session: `cp210x_vcp_open()` installs `cp210x_line_coding_set`/`cp210x_line_coding_get` into the handle's `intf_func` vtable, and separately issues `CP210X_CMD_IFC_ENABLE` automatically — but does **not** call `line_coding_set` itself. `[VERIFIED: managed_components/espressif__usb_host_cp210x_vcp/usb_host_cp210x_vcp.c, read in full this session]` |

No new component dependencies for this phase — everything needed already ships from Phase 2's `idf_component.yml`/`CMakeLists.txt` changes.

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Reusing `cat_cdc_send()`/`cat_cdc_ready()` unchanged | A new FTX-1-specific send/ready adapter pair | Rejected — Phase 2's research/summary already established the handle type (`cdc_acm_dev_hdl_t`) and these two functions are handle-type-agnostic; QMX and FTX-1 share the exact same transport functions today. No reason to diverge. |
| Explicit `cdc_acm_host_line_coding_set()` call in `cp210x_try_open()` | Assume the CP210x's power-on-reset default baud rate already matches the FTX-1's CAT-1 factory default (38400 8N1) | Risky assumption — CP210x devices often power up at a driver- or previous-session-dependent rate, and this codebase's CDC-ACM open path (both QMX's and the new CP210x path) never calls `line_coding_set` anywhere today. Recommended: set explicitly, don't assume. |

## Architecture Patterns

### System Architecture Diagram

```
main.cpp state machine                          radio_control_ftx1.cpp             stream_uac.cpp (Phase 2, unchanged)
─────────────────────                          ────────────────────────             ───────────────────────────────────
sync_radio_to_current_band()                                                        s_cdc_handle (CP210x CAT-1, already open)
  (band change / initial connect /               ftx1_sync_frequency_mode(freq_hz)
   STATUS exit — NEVER per-TX)          ─────►     ftx1_send_cmd("FA%09d;", freq)  ──►  cat_cdc_send()
                                                    ftx1_send_cmd("MD0C;")          ──►  cat_cdc_send()
                                                    (mode sent ONCE here; never
                                                     resent from begin_tx/end_tx)

tx_start()                                        ftx1_begin_tx(freq_hz, tx_base_hz)
  (per-TX, slot-boundary driven)        ─────►       ftx1_send_cmd("TX1;")          ──►  cat_cdc_send()
                                                      (no FA/MD resend — mode+freq
                                                       already synced; no per-symbol
                                                       TA/FO tone command — FTX-1
                                                       audio-tone TX rides UAC OUT,
                                                       Phase 4, like QDX)

tx_tick() all tones sent / cancelled              ftx1_end_tx()
                                        ─────►       uac_tx_end()  (safe no-op pre-Phase-4;
                                                       real audio-drain once Phase 4 lands)
                                                     ftx1_send_cmd("TX0;")           ──►  cat_cdc_send()
                                                     ftx1_send_cmd("FA%09d;", rx_freq) ──► cat_cdc_send()  (SYNC-03 restore)
                                                     ftx1_send_cmd("MD0C;")          ──►  cat_cdc_send()  (defensive re-assert)

[NEW THIS PHASE, transport layer]                                                   cp210x_try_open() — ADD:
                                                                                       cdc_acm_host_line_coding_set(handle, &lc)
                                                                                       lc = {38400, 8, none, 1 stop}
                                                                                       (must happen before ANY ftx1_send_cmd
                                                                                        succeeds meaningfully)
```

### Recommended Project Structure (files touched, no new files)
```
main/
├── radio_control_ftx1.cpp   # Replace all ESP_ERR_INVALID_STATE stubs (except ready())
│                            #   with real CAT command implementations
├── stream_uac.cpp           # cp210x_try_open(): add cdc_acm_host_line_coding_set() call
│                            #   immediately after RTS/DTR deassert, before g_cdc_initial_sync_pending=true
└── (no main.cpp changes expected — existing call sites for
     sync_frequency_mode/begin_tx/set_tone_hz/end_tx/set_tune already
     dispatch generically through radio_control.cpp's vtable)
```

### Pattern 1: CAT command formatting — FTX-1-specific, do not copy QMX/QDX literally

**What:** `ftx1_send_cmd()` wraps `cat_cdc_send()` exactly like `qmx_send_cmd()`/`qdx_send_cmd()`, but every command string this phase constructs must follow the FTX-1's own field widths, verified against the official manual:

```cpp
// Source: radio_control_qmx.cpp:17-20 (structural analog, transport call unchanged)
static esp_err_t ftx1_send_cmd(const char* cmd, uint32_t timeout_ms) {
    if (!cat_cdc_ready()) return ESP_ERR_INVALID_STATE;
    return cat_cdc_send(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), timeout_ms);
}

static esp_err_t ftx1_sync_frequency_mode(int freq_hz) {
    // FA: 9-digit field, NOT 11 like QMX/QDX's "FA%011d;".
    // Range 000030000-470000000 Hz per CAT manual (FA table).
    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", freq_hz);
    esp_err_t err = ftx1_send_cmd(fa, 200);
    if (err != ESP_OK) return err;

    // MD: P1=0 (MAIN-side) is REQUIRED, unlike QMX's bare "MD6;".
    // P2=C is DATA-U per the MD mode-code table.
    err = ftx1_send_cmd("MD0C;", 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "FTX-1 sync ok freq=%d mode=DATA-U", freq_hz);
    }
    return err;
}
```
`[CITED: FTX-1 CAT Operation Reference Manual pages "FA FREQUENCY VFO MAIN-SIDE" and "MD OPERATING MODE"]`

**When to use:** `ftx1_sync_frequency_mode()` is called only from `sync_radio_to_current_band()` (band change / initial CDC-connect / STATUS-menu exit) per the existing generic `radio_control_sync_frequency_mode()` dispatch — never from the TX path. This is what makes "`MD0C;` sent once at sync time, not resent on every TX" (SYNC-02's success criterion) true without any new guard logic — the call site itself never fires from `tx_start()`/`tx_tick()`.

### Pattern 2: PTT — single global TX flag, no VFO shift, no per-symbol tone command

**What:** Unlike QMX (`qmx_begin_tx()` resends `"MD6;"` then `"TX;"`) or KH1 (`kh1_begin_tx()` computes a shifted `tx_freq10` and resends `FA`), the FTX-1's `TX` command carries no frequency/mode side-effects — it is purely `TX1;`/`TX0;`. Because FTX-1's TX audio will ride USB Audio (Phase 4, like QDX) rather than per-symbol CAT tone commands (like QMX's `TA`/KH1's `FO`), `begin_tx()` should not attempt to resend `FA`/`MD` (already synced) and `set_tone_hz()` should be a no-op, mirroring QDX exactly:

```cpp
// Source: radio_control_qdx.cpp:38-63 (structural analog — audio-tone-TX radios,
// not per-symbol-CAT-tone radios like QMX/KH1)
static esp_err_t ftx1_begin_tx(int freq_hz, int tx_base_hz) {
    (void)freq_hz;      // FTX-1 is single-VFO, non-split; no per-TX FA shift needed
    (void)tx_base_hz;   // tone offset is realized in USB audio (Phase 4), not CAT

    esp_err_t err = ftx1_send_cmd("TX1;", 200);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "FTX-1 TX start");
    }
    return err;
}

static esp_err_t ftx1_set_tone_hz(float tone_hz) {
    (void)tone_hz;
    return ESP_OK;  // no-op: tone is realized via UAC OUT/DDS in Phase 4, not CAT
}

static esp_err_t ftx1_end_tx(void) {
    // PTT-02: drain audio BEFORE unkeying. uac_tx_end() no-ops safely today
    // (s_spk_writer_run is false pre-Phase-4) and becomes the real drain once
    // Phase 4 wires FTX-1 TX audio through the same UAC OUT path QDX uses.
    uac_tx_end();

    esp_err_t err = ftx1_send_cmd("TX0;", 200);
    if (err != ESP_OK) return err;

    // SYNC-03: defensive restore, mirroring kh1_end_tx()'s post-TX restore.
    // See Pattern 3 below for the full restore sequence.
    return ftx1_restore_rx_state();
}
```
**When to use:** This is the correct analog *specifically because* FTX-1, like QDX, moves TX audio over USB rather than through CAT — do not pattern-match against QMX or KH1 here even though those are also "radio backends," because their TX audio delivery mechanism (per-symbol synchronous CAT tone commands) is architecturally different from what FTX-1 will use.

### Pattern 3: Post-TX restore (SYNC-03), mirroring `kh1_end_tx()`

**What:** `kh1_end_tx()` (`radio_control_kh1_cat.cpp:161-185`) force-resends `FA<rx_freq>` after TX to restore the pre-TX VFO display, using a `_forced` variant (bypassing any "skip if unchanged" cache) since the whole point is guaranteeing the CAT command actually goes out regardless of cached state. FTX-1 should do the same for both frequency and mode, since the exact motivation (Yaesu's DATA-U→USB display-reversion quirk on sibling FT-991A/FTDX-10 models, tracked as v2 HARDEN-01) is unconfirmed-but-plausible for FTX-1 too:

```cpp
// Source: radio_control_kh1_cat.cpp:161-185 (structural analog for post-TX restore)
static esp_err_t ftx1_restore_rx_state(void) {
    // s_rx_freq_hz must be cached by ftx1_sync_frequency_mode() (see Pattern 1)
    // the same way KH1 caches s_rx_freq10.
    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", s_rx_freq_hz);
    esp_err_t fa_err = ftx1_send_cmd(fa, 200);

    // Defensive re-assert (not strictly required for a non-split single VFO,
    // but cheap insurance against the DATA-U reversion quirk noted in
    // REQUIREMENTS.md's v2 HARDEN-01 — remove if 03-0N hardware bring-up
    // shows FTX-1 never exhibits it).
    esp_err_t md_err = ftx1_send_cmd("MD0C;", 200);

    if (fa_err != ESP_OK) return fa_err;
    return md_err;
}
```
**When to use:** Called from `ftx1_end_tx()` (Pattern 2) after `TX0;` succeeds. Also consider from `ftx1_set_tune(false, ...)` (tune-off path) for the same reason KH1's `kh1_set_tune()` delegates its "disable" branch to `kh1_end_tx()`.

### Pattern 4: CAT-1 line coding — new transport-layer work, not backend work

**What:** Phase 2's `cp210x_try_open()` opens the port and deasserts RTS/DTR, but never configures baud rate/data bits/parity. `cdc_acm_host_open()`'s underlying implementation does not set any default line coding either (confirmed by reading `cdc_acm_host.c` directly — no `line_coding_set` call in the open path). The FTX-1's CAT-1 factory-default communication parameters are **38400 bps, 8 data bits, no parity, 1 stop bit** (`[CITED: CAT manual, "Communication Parameters"]`). Without an explicit `cdc_acm_host_line_coding_set()` call, the CP210x bridge may be running at whatever rate a previous host left it at (or a hardware power-on default), and every `FA`/`MD`/`TX` command this phase sends risks being framed incorrectly at the wire level — a failure mode that would look like "CAT never seems to work" rather than a clear error, since `cat_cdc_send()`'s `cdc_acm_host_data_tx_blocking()` call only reports USB-transfer-level success/failure, not whether the radio understood the bytes.

```c
// Source: managed_components/espressif__usb_host_cp210x_vcp/usb_host_cp210x_vcp.c
// (line_coding_set vtable hook, confirmed present and functional; just never
// called anywhere in this codebase yet)
// Add to stream_uac.cpp's cp210x_try_open(), immediately after the
// CAT-03 RTS/DTR deassert call, before g_cdc_initial_sync_pending = true:
cdc_acm_line_coding_t line_coding = {
    .dwDTERate = 38400,
    .bCharFormat = 0,   // 1 stop bit
    .bParityType = 0,   // none
    .bDataBits = 8,
};
esp_err_t lc_err = cdc_acm_host_line_coding_set(handle, &line_coding);
ESP_LOGI(TAG, "CP210x CAT-1 line coding set (38400 8N1): %s", esp_err_to_name(lc_err));
```
**When to use:** This belongs in `stream_uac.cpp` (transport tier), not `radio_control_ftx1.cpp`, per the Architectural Responsibility Map — it configures the physical CAT-1 UART bridge, not a CAT protocol semantic. Even though this phase's charter is "CAT Command Implementation," this one line of transport-layer setup is a hard prerequisite for any FTX-1 CAT command in this phase to reliably work, and Phase 2 did not anticipate it (it only handled RTS/DTR line *state*, not line *coding*). Flag this explicitly in the plan so it isn't silently skipped as "not this phase's file."

### Anti-Patterns to Avoid
- **Copying QMX's `"FA%011d;"` format verbatim:** produces a 14-character command against a radio that expects 12 (`FA` + 9 digits + `;`). Will not open-loop-fail (no read-back is wired in this phase) — it will simply be a malformed command the radio silently rejects, which is a much harder bug to diagnose than a build error. Always use `%09d` for FTX-1.
- **Copying QMX's bare `"MD6;"` without the P1 side digit:** the FTX-1 `MD` command requires the MAIN/SUB-side selector; omitting it is the exact "not enough parameters" mistake the CAT manual explicitly warns about.
- **Treating `TX` P1=2 as a settable "force TX on" value:** it's Answer-only (radio→computer), reflecting TX engaged by another means. Only `0` and `1` are valid Set values.
- **Building a per-symbol CAT tone command for FTX-1 (QMX/KH1-style `TA`/`FO`):** FTX-1, like QDX, will carry TX audio over USB Audio (Phase 4). Building CAT-based tone injection now is wasted work that would need to be removed once Phase 4 lands the real UAC OUT path.
- **Skipping the CAT-1 line-coding configuration on the theory that "Phase 2 already opened the port successfully":** port *open* (enumeration/handle creation) succeeding says nothing about whether the UART bridge's baud rate matches what the radio's CAT-1 parser expects; these are two different layers of the CP210x bridge.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| CP210x baud-rate/line-control vendor requests (AN571 `SET_BAUDRATE`/`SET_LINE_CTL`) | A raw `usb_host_transfer_submit()` custom vendor-request call | `cdc_acm_host_line_coding_set(handle, &line_coding)` | The Phase-2-vendored `usb_host_cp210x_vcp` component already patches this call to issue the correct AN571 vendor requests transparently — same reasoning Phase 2's research already established for RTS/DTR control |
| CAT command byte transport (framing, timeout, blocking send) | A new FTX-1-specific send function bypassing `cat_cdc_send()` | `cat_cdc_send()`/`cat_cdc_ready()` (`stream_uac.cpp`, unchanged since Phase 2) | Handle-type-agnostic already; QMX and FTX-1 share it verbatim today, no reason to diverge |

**Key insight:** As with Phase 2, there is no new API surface to design. The only genuinely new piece of code this phase needs beyond string formatting is the one-line `cdc_acm_host_line_coding_set()` call — everything else is "construct the right ASCII string, call the already-existing `cat_cdc_send()`."

## Common Pitfalls

### Pitfall 1: Copy-pasting Kenwood-style command widths onto a Yaesu radio
**What goes wrong:** `FA%011d;` (QMX/QDX's 11-digit format) or bare `MD6;` (no side digit) sent to the FTX-1 are syntactically invalid per its own CAT manual, and the manual explicitly documents this exact class of mistake ("Not enough digits", "Not enough parameters specified").
**Why it happens:** All three existing radio backends (QMX, QDX, KH1) share a Kenwood-derived CAT dialect closely enough that surface-level command names (`FA`, `MD`, `TX`/`RX`) look identical, inviting direct copy-paste. The Yaesu FTX-1's field widths and parameter structure are different underneath the same-looking command letters.
**How to avoid:** Always format FTX-1 commands from the verified field widths in this document's Pattern 1, not from the sibling backend files.
**Warning signs:** Radio never changes displayed frequency/mode despite `cat_cdc_send()` reporting `ESP_OK` (a malformed-but-still-transmitted command looks identical to a successful one at the USB-transfer level, since this phase does not wire response/read-back parsing).

### Pitfall 2: Missing CAT-1 line-coding configuration
**What goes wrong:** All CAT traffic sent over CAT-1 is framed at the wrong baud rate, so the radio's UART receiver either drops the bytes or receives garbage — again indistinguishable from "command was malformed" at this phase's send-side-only visibility.
**Why it happens:** Phase 2 handled RTS/DTR line *state* explicitly (CAT-03) but line *coding* (baud/data/parity) was out of that phase's scope and nobody wired it — confirmed by direct source read this session.
**How to avoid:** Add the `cdc_acm_host_line_coding_set()` call in `cp210x_try_open()` per Pattern 4, and treat it as a phase-blocking prerequisite even though the file it lives in (`stream_uac.cpp`) isn't `radio_control_ftx1.cpp`.
**Warning signs:** Identical failure signature to Pitfall 1 (no visible reaction to any CAT command) — hardware bring-up should test line-coding correctness (Pitfall 2) and command syntax correctness (Pitfall 1) as two separate hypotheses if the radio doesn't react at all.

### Pitfall 3: Assuming CAT-1 supports PTT because "TX is just another 2-letter command"
**What goes wrong:** If the FTX-1 firmware genuinely restricts the `TX` command's *effect* (or its parser's acceptance) to CAT-2 despite CAT-1 accepting the bytes without a parse error, `TX1;` sent over CAT-1 could either be silently ignored or (worse) return an Answer echoing back `TX0;` (still-off) while giving no error — easy to misread as "PTT-01 done" when it silently isn't.
**Why it happens:** The manual's port-function table describes CAT-1 as frequency/mode-only and CAT-2 as TX-control-capable; it is not definitively clear from documentation alone whether that's a firmware-enforced restriction or a description of typical historical RTS/DTR-based hardware PTT wiring.
**How to avoid:** Treat as a HIGH-RISK open question requiring a physical-hardware checkpoint (see Open Questions) before considering PTT-01/02 done; do not accept "the CAT-1 write succeeded" as proof the radio actually keyed.
**Warning signs:** No visible PTT indicator on the FTX-1 display/front panel despite `ftx1_send_cmd("TX1;", ...)` returning `ESP_OK`.

### Pitfall 4: Mis-attributing the audio-drain pattern to QMX instead of QDX
**What goes wrong:** A planner or executor reading REQUIREMENTS.md's PTT-02 wording ("mirroring the QMX backend's drain-before-unkey pattern") and then reading `qmx_end_tx()` will find no drain call at all (`qmx_end_tx()` just sends `"RX;"` immediately) — because QMX's TX audio model (per-symbol synchronous CAT `TA` command) has nothing to drain. The actual drain-before-unkey code lives in `qdx_end_tx()` (`uac_tx_end()` then `"RX;"`).
**Why it happens:** REQUIREMENTS.md's wording predates this session's direct code read; it's a documentation attribution error, not a code inconsistency.
**How to avoid:** Implement FTX-1's `end_tx()` against `qdx_end_tx()`'s actual code (Pattern 2 above), not against a hypothetical QMX drain call that doesn't exist in the current codebase.
**Warning signs:** None visible from behavior alone (both QMX's and QDX's `end_tx()` "work" from the CAT layer's perspective) — this is a design-alignment risk that only surfaces once Phase 4 audio integration reveals whether FTX-1's TX audio actually drained cleanly.

## Code Examples

### Full `radio_control_ftx1.cpp` CAT skeleton (combines Patterns 1-3)
```cpp
// Source: this session's synthesis of radio_control_qmx.cpp / radio_control_qdx.cpp /
// radio_control_kh1_cat.cpp structural conventions + verified FTX-1 CAT manual syntax
#include "radio_control_backend.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "stream_uac.h"

static const char* TAG = "RADIO_FTX1";
static int s_rx_freq_hz = 0;   // cached by sync_frequency_mode(), restored by end_tx()
static bool s_tx_active = false;

static bool ftx1_ready(void) {
    return cat_cdc_ready();
}

static esp_err_t ftx1_send_cmd(const char* cmd, uint32_t timeout_ms) {
    if (!cat_cdc_ready()) return ESP_ERR_INVALID_STATE;
    return cat_cdc_send(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), timeout_ms);
}

static esp_err_t ftx1_sync_frequency_mode(int freq_hz) {
    s_rx_freq_hz = freq_hz;

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

static esp_err_t ftx1_restore_rx_state(void) {
    char fa[16];
    snprintf(fa, sizeof(fa), "FA%09d;", s_rx_freq_hz);
    esp_err_t fa_err = ftx1_send_cmd(fa, 200);
    esp_err_t md_err = ftx1_send_cmd("MD0C;", 200);
    return (fa_err != ESP_OK) ? fa_err : md_err;
}

static esp_err_t ftx1_begin_tx(int freq_hz, int tx_base_hz) {
    (void)freq_hz;
    (void)tx_base_hz;
    esp_err_t err = ftx1_send_cmd("TX1;", 200);
    if (err == ESP_OK) {
        s_tx_active = true;
        ESP_LOGI(TAG, "FTX-1 TX start");
    }
    return err;
}

static esp_err_t ftx1_set_tone_hz(float tone_hz) {
    (void)tone_hz;
    return ESP_OK;  // audio-tone TX rides UAC OUT in Phase 4, like QDX
}

static esp_err_t ftx1_end_tx(void) {
    if (!s_tx_active) return ESP_OK;

    uac_tx_end();  // PTT-02: drain before unkey (no-op pre-Phase-4, real drain after)

    esp_err_t err = ftx1_send_cmd("TX0;", 200);
    s_tx_active = false;
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "FTX-1 TX stop");
    return ftx1_restore_rx_state();  // SYNC-03
}

static esp_err_t ftx1_set_tune(bool enable, int freq_hz, int tone_hz) {
    if (!enable) {
        return ftx1_end_tx();
    }
    esp_err_t err = ftx1_sync_frequency_mode(freq_hz);
    if (err != ESP_OK) return err;
    err = ftx1_send_cmd("TX1;", 200);
    if (err == ESP_OK) s_tx_active = true;
    (void)tone_hz;  // no CAT-side tone for FTX-1; audio tone deferred to Phase 4
    return err;
}

static esp_err_t ftx1_on_audio_start(void) {
    ESP_LOGI(TAG, "FTX-1 CAT backend initialized");
    return ESP_OK;
}

static esp_err_t ftx1_set_time(int hour, int minute, int second) {
    (void)hour; (void)minute; (void)second;
    return ESP_ERR_NOT_SUPPORTED;  // no DT-command wiring in this phase; discretionary
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

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| REQUIREMENTS.md's assumption that FTX-1's `TX1;`/`TX0;` and Kenwood-style `FA`/`MD` field widths mirror QMX/QDX closely enough to copy-paste | Verified against the official Yaesu CAT manual this session: field widths (`FA`=9 digits not 11) and required parameters (`MD` needs an explicit side digit QMX/QDX's bare `MDn;` doesn't have) genuinely differ | This session (2026-07-06), first time the actual manual was fetched and read for this project | Any plan/task written from a "just copy QMX's command construction" mental model will produce malformed CAT commands; Phase 3 tasks must cite the field widths in this document explicitly |

**Deprecated/outdated:** Nothing from prior phases is deprecated by this research — Phase 2's transport work (open, RTS/DTR deassert, `cat_cdc_send`/`cat_cdc_ready`) is reused unchanged and is not superseded.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `TX1;`/`TX0;` sent over CAT-1 will actually key/unkey the FTX-1, despite the manual's port-function table associating "TX Controls" primarily with CAT-2 | Summary, Pitfall 3, Open Questions | HIGH — if wrong, PTT-01/02 cannot be satisfied without either using CAT-2 (contradicts the locked single-port ROADMAP/REQUIREMENTS decision) or falling back to RTS/DTR toggling (contradicts PTT-01's explicit "no RTS/DTR toggling" requirement). This is the single most consequential unresolved question from this research and should gate a hardware checkpoint before/alongside the first PTT task. |
| A2 | FTX-1's CAT-1 factory-default line coding (38400 8N1) is what the port will actually present after `cp210x_vcp_open()`, and explicitly setting it via `cdc_acm_host_line_coding_set()` is both necessary and sufficient | Pattern 4, Pitfall 2 | MEDIUM — if the radio's CAT-1 rate was changed from factory default via its own menu (`OPERATION SETTING → GENERAL → CAT-1 RATE`) by a previous owner/session, firmware's hardcoded `38400` would mismatch. Low likelihood for a device fresh out of the box, but worth a hardware-bring-up log line confirming the actual configured rate matches. |
| A3 | FTX-1's TX audio delivery model will be USB-Audio-based (like QDX), not per-symbol CAT-tone-based (like QMX/KH1), making `set_tone_hz()` correctly a no-op in this phase | Pattern 2 | MEDIUM — this is an inference from the ROADMAP (Phase 4 is explicitly "Bidirectional UAC Audio Negotiation") and the ROADMAP/REQUIREMENTS' repeated framing of FTX-1 as a single-USB-port audio+CAT device, not from any FTX-1-specific CAT-audio-routing documentation read this session. If wrong (e.g., FTX-1 unexpectedly needs a CAT-encoded tone/level command mirroring QMX's `TA`), `set_tone_hz()` would need real logic added later — low risk since it only means added work in Phase 4/5, not incorrect behavior in Phase 3. |
| A4 | The v2-deferred HARDEN-01 "DATA-U→USB mode reversion" quirk (documented for FT-991A/FTDX-10) either does or does not affect the FTX-1, and a defensive `MD0C;` re-assert after every TX (Pattern 3) is cheap enough to include speculatively | Pattern 3, Summary | LOW — worst case, the defensive re-assert is provably unnecessary and is simply an extra harmless CAT write after every TX; REQUIREMENTS.md itself defers the *confirmed* fix to v2 (HARDEN-01) pending hardware evidence, so this phase's inclusion of the cheap defensive command is intentionally more conservative than that decision, not a violation of it. |

## Open Questions

1. **Does `TX1;`/`TX0;` sent over CAT-1 actually key/unkey the FTX-1, or is PTT control functionally scoped to CAT-2 only?**
   - What we know: The official CAT manual's port-function table describes CAT-1 as "CAT Communications (Frequency and Communication Mode Settings)" and CAT-2 as "TX Controls (PTT control, CW Keying, Digital Mode Operation) **or** CAT Communications." The `TX` command itself is listed in the single unified "CAT Control Command List" without any per-port restriction noted.
   - What's unclear: Whether this is a firmware-enforced command-acceptance restriction (CAT-1 would reject/ignore `TX`) or a documentation convention describing typical/legacy hardware-RTS/DTR-PTT wiring on CAT-2 (in which case the ASCII `TX;` command works identically on either port, since both are read by the same MCU command parser).
   - Recommendation: Treat as a blocking hardware-verification item for the first PTT task in this phase (a `checkpoint:human-verify` task analogous to Phase 2's 02-03, or an early smoke-test step in the first PTT-implementing plan) — send `TX1;` over the already-open CAT-1 handle and visually confirm the FTX-1's own PTT/TX indicator engages, before building out the rest of the TX state machine integration on an unverified assumption. If CAT-1 cannot key the radio, escalate to the user/roadmap owner rather than silently opening CAT-2 (an out-of-scope decision above this phase's authority).

2. **Does the FTX-1 require `MOD SOURCE` (menu item, `EX010413`) set to `USB`/`AUTO` for its own audio-routing sanity, or is this purely a Phase 4/5 concern?**
   - What we know: `MOD SOURCE` (RADIO SETTING → MODE DATA) controls whether the radio modulates from MIC, USB, Bluetooth, or AUTO. Factory default per the manual's Table 3 is not marked bold/default in the excerpt captured (worth confirming on the physical unit).
   - What's unclear: Whether this needs to be set via CAT (`EX` command) as part of Phase 3's sync sequence, or is purely a physical-radio menu setting the user configures once (out of firmware's control entirely) before Phase 4 audio work begins.
   - Recommendation: Out of Phase 3's strict scope (SYNC-01/02/03, PTT-01/02 are about frequency/mode/PTT only, not audio routing) — defer to Phase 4 research. Does not block this phase's success criteria, since PTT-01/02's visible-keying success criterion doesn't require modulated audio.

3. **Is the assumed CAT-1 line coding (38400 8N1) actually what the physical unit presents, and does explicitly setting it via `cdc_acm_host_line_coding_set()` have any observable side-effect worth logging during bring-up?**
   - What we know: 38400 8N1 is the manual's documented factory default for CAT-1/CAT-3; Phase 2 never configured line coding at all.
   - What's unclear: Whether skipping this step would have caused Phase 2's own hardware checkpoint (02-03) to already show CAT failures — it didn't, but Phase 2 never sent an actual CAT command, only opened the port and deasserted RTS/DTR, so this is untested territory, not contradicted-by-existing-evidence territory.
   - Recommendation: Include the `cdc_acm_host_line_coding_set()` call unconditionally (Pattern 4) and log its result explicitly during this phase's own hardware checkpoint, so a `line coding set` failure is visible and distinguishable from a malformed-command failure (Pitfall 1) or a CAT-1-scoping failure (Open Question 1).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Physical FTX-1 hardware | Open Question 1 (CAT-1 PTT scoping), all SYNC-*/PTT-* hardware-verifiable success criteria | User-confirmed available per Phase 2's completed hardware checkpoint (02-03) | — | None — this phase's success criteria are explicitly hardware-verifiable (per ROADMAP) and cannot be validated by code review alone, same constraint Phase 2 operated under |
| ESP-IDF v5.5.1 toolchain (`idf.py`) | Build verification | Confirmed present at `C:\Espressif\esp-idf-v5.5.1` (per Phase 2 SUMMARY notes) but not on PATH in executor Bash sessions | v5.5.1 | Manual code/grep audit by the executor, `idf.py build` run by orchestrator/human afterward — same pattern Phase 2 used throughout |
| `espressif/usb_host_cdc_acm` / `usb_host_cp210x_vcp` components | `cdc_acm_host_line_coding_set()` (Pattern 4) | Already fetched into `managed_components/` per Phase 2 | ^2.2.0 (both) | None needed — no new dependency, function already available in headers already included by `stream_uac.cpp` |

**Missing dependencies with no fallback:** None — this phase adds no new external dependencies; it only writes CAT command strings and one additional call into already-vendored, already-linked components.

## Security Domain

> `security_enforcement: true`, ASVS Level 1, block on high (per `.planning/config.json`). This phase is firmware-only USB/CAT protocol implementation with no network surface; most ASVS categories do not apply, consistent with Phase 2's assessment and `ARCHITECTURE.md`'s standing note ("None (local device). CAT radio control assumes trusted connection.").

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | No auth surface — local USB peripheral, physically trusted |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Partial | This phase only *sends* CAT commands (no response/read-back parsing wired — `data_cb` remains `NULL` per Phase 2); frequency values passed into `snprintf("FA%09d;", freq_hz)` should be range-clamped to the documented 000030000–470000000 Hz range before formatting, since an out-of-range or negative `freq_hz` would silently produce a malformed (wrong-width or sign-containing) command string rather than a caught error. Recommend a clamp/range-check inside `ftx1_sync_frequency_mode()`, mirroring the existing project convention already used in `qmx_set_tone_hz()`'s documented clamping of `ta_int`/`ta_frac`. |
| V6 Cryptography | No | N/A — CAT link is plaintext ASCII by design (Yaesu protocol), consistent with existing QMX/QDX/KH1 backends |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Out-of-range or malformed `freq_hz` producing a wrong-width `FA` command that the radio may misparse or silently ignore | Tampering (of the outbound command, not attacker-controlled — this is a correctness/DoS-to-self risk, not an external attack surface) | Clamp `freq_hz` to `[30000, 470000000]` before formatting the 9-digit `FA` field, matching the documented CAT range; log a warning if a caller ever passes a value outside it (should not happen via the existing band-table-driven call sites, but defensive) |
| A future CAT response-read path (not this phase) trusting unvalidated bytes from a `data_cb`, if a later phase wires FTX-1 response parsing (e.g. reading back `IF;`/`FA;` to confirm state) | Tampering / Denial of Service | Out of scope for Phase 3 (`data_cb` stays `NULL`, no read-back parsing added); flagged for whichever future phase adds it, same recommendation Phase 2's research already carried forward |

## Sources

### Primary (HIGH confidence)
- Yaesu FTX-1 Series CAT Operation Reference Manual (`FTX-1_CAT_OM_ENG_2508-C.pdf`, Copyright 2025 YAESU MUSEN CO., LTD.) — fetched and read in full this session via WebFetch/PDF read; all command syntax (`FA`, `MD`, `TX`, communication parameters, CAT-1/CAT-2 port function description) verified directly from this document.
- `C:\GitHub\Mini-FT8\main\radio_control_qmx.cpp`, `radio_control_qdx.cpp`, `radio_control_kh1_cat.cpp`, `radio_control_ftx1.cpp`, `radio_control_backend.h`, `radio_control.cpp` — direct read, full files, this session.
- `C:\GitHub\Mini-FT8\main\stream_uac.cpp` (TX audio functions `uac_tx_begin_cpfsk`/`uac_tx_begin_tune`/`uac_tx_end`, lines ~1058-1114; `cat_cdc_ready`/`cat_cdc_send`) — direct read, this session.
- `C:\GitHub\Mini-FT8\main\main.cpp` (`sync_radio_to_current_band()`, `consume_cdc_initial_sync()`, `tx_start()`, `tx_tick()` — call-site verification for SYNC-01/02 "once, not per-TX" and PTT-02 sequencing) — direct read, this session.
- `C:\GitHub\Mini-FT8\managed_components\espressif__usb_host_cp210x_vcp\usb_host_cp210x_vcp.c` — direct read in full this session, confirms `cp210x_vcp_open()` does NOT call `line_coding_set` automatically, and confirms the vtable patch (`cp210x_line_coding_set`/`get`, `cp210x_set_control_line_state`, `cp210x_send_break`) that makes `cdc_acm_host_line_coding_set()` work correctly against the CP210x once called.
- `C:\GitHub\Mini-FT8\managed_components\espressif__usb_host_cdc_acm\cdc_acm_host.c` (grep for `cdc_acm_host_open`) — confirms no default line-coding configuration happens on open, for either QMX's or FTX-1's path.
- `.planning/REQUIREMENTS.md`, `.planning/STATE.md`, `.planning/ROADMAP.md`, `.planning/phases/02-cp210x-usb-bring-up-cat-connection/*` (RESEARCH, PATTERNS, all 3 plan/summary pairs) — direct read this session, establishes exactly what Phase 2 completed and what this phase inherits.

### Secondary (MEDIUM confidence)
- Yaesu FT-991A/FTDX-10 DATA-U→USB mode-reversion quirk (referenced via REQUIREMENTS.md's v2 HARDEN-01) — carried forward from prior roadmap-time research, not independently re-verified against a primary source this session; treated as a plausibility-only justification for the defensive re-assert in Pattern 3, not as an established FTX-1-specific fact.

### Tertiary (LOW confidence)
- None — all claims in this document are either directly cited against the official manual/codebase, or explicitly flagged as assumptions in the Assumptions Log above.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependencies; both components' relevant functions confirmed present and functional by direct source read
- Architecture: HIGH for command-formatting/call-site integration (verified against manual + direct code read); MEDIUM for the CAT-1 PTT-scoping question (Open Question 1) — explicitly flagged, not glossed over
- Pitfalls: HIGH for command-format mismatches (directly verified against both the manual and the existing codebase) — MEDIUM for the CAT-1/CAT-2 PTT-scoping pitfall (genuinely ambiguous from documentation, requires hardware)

**Research date:** 2026-07-06
**Valid until:** Should remain valid through Phase 3 execution — CAT command syntax is defined by firmware version "Ver. 1.08 or later" per the manual's own note; if the physical unit's firmware is older, CAT operation won't work at all (a Phase 2 hardware-checkpoint-level concern, already implicitly passed since Phase 2's 02-03 hardware bring-up succeeded). Re-check only if execution is delayed enough that a newer CAT manual revision supersedes the 2508-C edition read this session.

---
*Phase 3 research: CAT Command Implementation*
*Researched: 2026-07-06*
