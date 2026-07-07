# Phase 4: Bidirectional UAC Audio Negotiation - Research

**Researched:** 2026-07-06
**Domain:** ESP-IDF `usb_host_uac` (espressif/usb_host_uac v1.3.3) format negotiation, DWC_OTG FIFO/channel budgeting on ESP32-S3, `stream_uac.cpp` candidate-scan/speaker-pre-open extension for a third USB profile
**Confidence:** HIGH for code-level integration points and ESP-IDF/HAL mechanics (read directly from vendored source and installed toolchain); LOW for the FTX-1's actual USB Audio descriptor (no public documentation found — confirmed unconfirmed, matches PROJECT.md's existing flag)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Mic RX (UAC IN) format negotiation**
- D-01: Replace the current hardcoded single-candidate mic negotiation for `UAC_PROFILE_FTX1` (`stream_uac.cpp:594-596`, currently just repeats QMX's exact 24-bit/stereo/48kHz format) with a real candidate-scan loop, same mechanism already used by `UAC_PROFILE_GENERIC_USB`.
- D-02: Candidate try-order prioritizes what QMX-parity would look like first, then degrades: (2ch/24-bit) -> (2ch/16-bit) -> (1ch/24-bit) -> (1ch/16-bit), all at 48kHz.
- D-03: Whatever format wins negotiation is recorded in `s_format` exactly as today (existing `uac_pcm_to_ft8_samples()` resampler already handles variable bit-depth/channels -- no changes needed downstream of negotiation).

**TX speaker (UAC OUT) strategy**
- D-04: Do not build a multi-format DDS renderer. `dds_render_24bit_stereo()` stays fixed at 24-bit/stereo output. The speaker candidate list for `UAC_PROFILE_FTX1` contains exactly one candidate -- 2ch/24-bit/48kHz -- mirroring QMX's pre-open-at-`TX_CONNECTED`-time/suspend-resume machinery but gated on `UAC_PROFILE_FTX1` in addition to `UAC_PROFILE_QMX`.
- D-05: If the FTX-1's speaker doesn't accept 24-bit/stereo/48kHz, that is a hardware-validation finding for the Phase 4 hardware-checkpoint plan to surface and react to (e.g., add a second render path) -- not something to pre-build speculatively now.

**FIFO re-tuning**
- D-06: Extend `usb_lib_task()`'s custom FIFO partitioning (currently QMX-only, `stream_uac.cpp:413-426`) to also apply for `UAC_PROFILE_FTX1`, using the QMX split (`rx=91/nptx=18/ptx=91` lines) as the analytical starting point.
- D-07: Because FTX-1 additionally carries CP210x bulk CAT traffic, the split should be validated/adjusted empirically during the hardware-checkpoint plan rather than assumed correct from the analytical starting point alone.

**Partial-negotiation fallback**
- D-08: Follow the project's existing graceful-degradation philosophy. If mic negotiation succeeds but speaker negotiation fails: continue in RX-only mode (waterfall/decode work, TX unavailable), surfaced via the existing `s_status_string` mechanism -- do not block `audio_source_start()` entirely.
- D-09: If mic negotiation fails outright (no candidate accepted), the existing failure path already handles this correctly (`"No 48k UAC mic format"` status string, `uac_host_device_close()`, no crash). No new fallback machinery needed; just extend the existing candidate-scan failure path to `UAC_PROFILE_FTX1`.

### Claude's Discretion
- Exact candidate array sizing/indexing mechanics inside `uac_lib_task()`'s `add_candidate` lambda usage for the FTX-1 branch.
- Whether the FIFO split constants get a named `UAC_PROFILE_FTX1`-specific comment block or share the QMX log line -- implementation detail, not a user-facing decision.

### Deferred Ideas (OUT OF SCOPE)
- Multi-format DDS TX renderer (supporting negotiated speaker formats other than 24-bit/stereo) -- deferred until hardware validation actually shows the FTX-1 needs it (D-05); not scoped into this phase.
- TX power control (POWER-01/POWER-02) and DATA-U re-assertion hardening (HARDEN-01) -- already tracked as v2 requirements in REQUIREMENTS.md, unrelated to audio.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| AUDIO-01 | Firmware streams RX audio from the FTX-1's USB audio input (mic) into the existing FT8/FT4 decode pipeline, using runtime format negotiation (not a hardcoded profile) since the FTX-1's USB Audio descriptor is unconfirmed | Pattern 1 (mic candidate-scan branch extension, exact code), Pitfall 2 (sample-rate mismatch risk), Open Question 1 (descriptor-dump verification step) |
| AUDIO-02 | Firmware streams TX (FSK tone) audio to the FTX-1's USB audio output (speaker) during transmit, reusing the QMX DDS/UAC-OUT pre-open pattern | Pattern 2 (speaker guard widening, exact code), Pitfall 3 (single-attempt pre-open semantics under D-08 degradation) |
| AUDIO-03 | USB host FIFO partitioning is re-tuned (not copy-pasted from QMX) to support a third simultaneous USB client (CP210x bulk CAT + bidirectional UAC audio) | Pattern 3 (FIFO guard widening + hard 200-line budget arithmetic verified against ESP-IDF HAL source), Pitfall 1 (boot-time HAL_ASSERT risk), Open Question 2 (CP210x bulk MPS headroom) |
</phase_requirements>

## Summary

Phase 4 is a mechanical extension of two already-proven patterns in `stream_uac.cpp`, not new architecture. The mic candidate-scan loop (currently gated to `UAC_PROFILE_GENERIC_USB`) and the speaker pre-open/suspend-resume machinery (currently gated to `UAC_PROFILE_QMX`) both already exist and are format-agnostic downstream (`uac_pcm_to_ft8_samples()`, `dds_render_24bit_stereo()` write a fixed format but negotiation determines what that format is only for RX). What Phase 4 adds is: (1) a `UAC_PROFILE_FTX1` branch in the `add_candidate` sequence with the try-order the user locked in CONTEXT.md D-02, (2) broadening the speaker `TX_CONNECTED` guard from `s_profile != UAC_PROFILE_QMX` to also accept `UAC_PROFILE_FTX1` (single fixed candidate per D-04), and (3) broadening `usb_lib_task()`'s custom FIFO partition condition from `UAC_PROFILE_QMX` to also cover `UAC_PROFILE_FTX1`.

The one genuinely new risk this phase introduces is FIFO/channel budget arithmetic for a third simultaneous USB client. Reading the ESP-IDF HAL source directly (`components/hal/usb_dwc_hal.c`, `components/usb/hcd_dwc.c`) confirms the numbers already baked into the existing code comment: ESP32-S3's DWC_OTG core has exactly **200 FIFO lines total** (confirmed via `usb_dwc_ll_ghwcfg_get_fifo_depth()`, matching QMX's `91+18+91=200` split exactly), and `usb_dwc_hal_set_fifo_config()` calls `HAL_ASSERT(usb_dwc_hal_fifo_config_is_valid(...))` — **if the custom FIFO config is ever over-budget, this is a hard assert/abort at `usb_host_install()` time, not a graceful `esp_err_t` failure.** This means the hardware-checkpoint plan must verify the FIFO split arithmetically sums to ≤200 lines *before* flashing, not rely on a runtime error path — an over-budget config will crash the boot, not just fail to open a device. Channel budget (distinct from FIFO lines) is comfortably non-limiting: ESP32-S3's DWC_OTG has significantly more host channels than the 3 endpoints this phase needs (CP210x CAT bulk, UAC mic IN, UAC speaker OUT), so channel exhaustion is not a realistic risk here — FIFO line budget is the actual constraint.

No public Yaesu documentation (operation manual, CAT reference manual, or third-party sources) describes the FTX-1's USB Audio descriptor (sample rate options, bit depth, channel layout, sync mode). This was independently re-confirmed by web search this session and matches PROJECT.md's own flag — treat every claim about the FTX-1's actual advertised format as unconfirmed until the hardware-checkpoint plan runs `uac_host_printf_device_param()`/`uac_host_get_device_alt_param()` against the real unit.

**Primary recommendation:** Implement the code-side extension exactly as scoped by CONTEXT.md's locked decisions (D-01 through D-09) — reuse existing `add_candidate` lambda, existing speaker pre-open block, existing FIFO custom-config block, only widening their `s_profile` guards — and treat the FIFO line-count arithmetic as a hard pre-flight budget check (documented below) rather than something to validate only empirically on hardware, since an over-budget config asserts at boot rather than failing softly.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Mic (UAC IN) format negotiation | Firmware — USB host orchestration (`stream_uac.cpp: uac_lib_task()`) | — | Single ESP32-S3 USB-OTG peripheral; one task pair owns all profile negotiation, unchanged pattern |
| Speaker (UAC OUT) pre-open/resume/suspend | Firmware — USB host orchestration (`stream_uac.cpp: uac_lib_task()` TX_CONNECTED handler, `spk_writer_task()`) | — | Existing QMX machinery generalized to a second profile; no new subsystem |
| USB host FIFO/channel partitioning | Firmware — USB host orchestration (`stream_uac.cpp: usb_lib_task()`) | ESP-IDF HAL (`components/hal/usb_dwc_hal.c`) | Firmware picks the split; HAL enforces (and hard-asserts on) the physical FIFO-line budget |
| PCM-to-FT8-sample resampling | Firmware — audio pipeline (`main/resample.cpp`, `uac_pcm_to_ft8_samples()`) | — | Already format-agnostic (16/24-bit, mono/stereo); zero changes needed downstream of negotiation (D-03) |
| CPFSK tone rendering for TX | Firmware — DSP (`main/dds_q15.cpp`) | — | Fixed 24-bit/stereo renderer; not generalized this phase (D-04/D-05) |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `espressif/usb_host_uac` | `^1.3.3` (pinned; `idf_component.yml` in this repo currently constrains `^1.2.0`, resolved lock is 1.3.3) | UAC class driver: candidate negotiation (`uac_host_device_start`), streaming I/O (`uac_host_device_read/write`), suspend/resume | Already the project's sole UAC dependency; this phase adds zero new dependencies `[VERIFIED: read managed_components/espressif__usb_host_uac/include/usb/uac_host.h directly this session]` |
| ESP-IDF | v5.5.1 (unchanged) | USB Host Library (`usb_host_install`, `usb_host_config_t.fifo_settings_custom`), DWC_OTG HAL | Project constraint; no IDF changes needed. FIFO-budget mechanics confirmed by direct read of `C:\Espressif\esp-idf-v5.5.1\components\usb\include\usb\usb_host.h` and `components\hal\usb_dwc_hal.c` `[VERIFIED: direct source read this session]` |
| `espressif/usb_host_cp210x_vcp` | `^2.2.0` (already added, Phase 2) | CP210x CAT-1 transport; contributes a bulk IN/OUT endpoint pair that shares the FIFO budget with UAC | No change needed this phase — already integrated; its bulk traffic is what the FIFO re-tune must accommodate `[VERIFIED: main/stream_uac.cpp direct read]` |

**Installation:** No new packages. No `idf_component.yml`/`CMakeLists.txt` changes required for this phase — everything needed (`uac_host_stream_config_t`, `add_candidate` pattern, `fifo_settings_custom`) is already present in the codebase from Phases 1-2.

### Version verification
Not applicable — no new dependency versions introduced this phase. `usb_host_uac` and `usb_host_cp210x_vcp` versions are unchanged from Phase 2/prior state.

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Candidate-scan loop (existing GENERIC_USB pattern) | `uac_host_device_open_with_vid_pid()` + a single hardcoded format | Rejected by CONTEXT.md D-01 — the FTX-1's descriptor is unconfirmed, so a strict single-candidate approach (QMX's current placeholder) is known to fail per 02-RESEARCH.md's own finding that FTX-1 mic-only advertises 16-bit while the current placeholder tries 24-bit |
| Custom `fifo_settings_custom` per-profile split | Default Kconfig FIFO bias (`CONFIG_USB_HOST_HW_BUFFER_BIAS_*`) | Rejected — Kconfig defaults are single global bias, not tunable per-profile at runtime, and were already shown insufficient for QMX's bidirectional 24-bit/48k/stereo + CDC combination (existing code comment); adding a third client only increases pressure on the same fixed 200-line budget |

## Architecture Patterns

### System Architecture Diagram

```
FTX-1 (single USB port, composite device — 3 logical clients competing for one FIFO/channel budget)
   │
   ├─ CP210x CAT-1 (Enhanced COM, vendor-class 0xFF)         bulk IN + bulk OUT
   ├─ USB Audio mic (UAC IN)                                  isochronous IN
   └─ USB Audio speaker (UAC OUT)                             isochronous OUT
   ▼
ESP32-S3 USB-OTG (single peripheral, single usb_host_install(), 200 FIFO lines total)
   ▼
stream_uac.cpp: usb_lib_task()
   ├─ host_config.fifo_settings_custom  ★ CHANGED: guard widened to
   │     (rx_fifo_lines, nptx_fifo_lines,   `s_profile == UAC_PROFILE_QMX ||
   │      ptx_fifo_lines)                    s_profile == UAC_PROFILE_FTX1`
   │     starting point: rx=91/nptx=18/ptx=91 (QMX values, sum=200)
   │     -- MUST re-validate sum <= 200 if any value is adjusted for FTX-1's
   │        actual CP210x bulk transfer size (larger than CDC-ACM's 64B OUT)
   ▼
uac_lib_task() RX_CONNECTED handler (mic)
   ├─ if profile == GENERIC_USB → existing 4-candidate mono/stereo x 16/24-bit scan
   └─ if profile == FTX1        ★ NEW: add_candidate(2,24) → (2,16) → (1,24) → (1,16)
          all @ 48kHz (D-02 try-order), first successful uac_host_device_start() wins
   ▼
uac_lib_task() TX_CONNECTED handler (speaker)
   └─ guard widened: `if (s_profile != UAC_PROFILE_QMX && s_profile != UAC_PROFILE_FTX1) continue;`
          single candidate: 2ch/24-bit/48kHz (D-04) — matches dds_render_24bit_stereo() exactly
          FLAG_STREAM_SUSPEND_AFTER_START, pre-opened at enum time (existing QMX machinery, unchanged)
   ▼
stream_uac_task() / spk_writer_task()  (UNCHANGED — already format-driven via s_format at RX,
                                          fixed-format at TX; no changes needed downstream)
   ▼
ft8_audio_pipeline_run() / ft8_lib decode / dds_q15 render  (UNCHANGED)
```

### Recommended Project Structure (files touched, no new files)
```
main/
├── stream_uac.cpp
│   ├── uac_lib_task() RX_CONNECTED handler:
│   │     add_candidate() branch for UAC_PROFILE_FTX1 (D-01/D-02)
│   ├── uac_lib_task() TX_CONNECTED handler:
│   │     widen `if (s_profile != UAC_PROFILE_QMX)` guard to include FTX1 (D-04)
│   └── usb_lib_task():
│         widen `if (s_profile == UAC_PROFILE_QMX)` FIFO-partition guard to include FTX1 (D-06/D-07)
└── stream_uac.h            # no changes expected — UAC_PROFILE_FTX1 already exists (Phase 2)
```

### Pattern 1: Mic candidate-scan extension (D-01/D-02)

**What:** Add a fourth branch to the existing `if (s_profile == UAC_PROFILE_GENERIC_USB) { ... } else { ... }` structure in `uac_lib_task()` (`stream_uac.cpp:589-596`), so `UAC_PROFILE_FTX1` gets its own candidate list instead of falling into the QMX single-candidate `else` branch.
**When to use:** This exact code path, this exact phase — no broader applicability.
**Example:**
```cpp
// Source: extends main/stream_uac.cpp:580-596 (uac_lib_task RX_CONNECTED handler)
uac_host_stream_config_t candidates[4];
int candidate_count = 0;
auto add_candidate = [&](uint8_t ch, uint8_t bits) {
    candidates[candidate_count].channels = ch;
    candidates[candidate_count].bit_resolution = bits;
    candidates[candidate_count].sample_freq = UAC_SAMPLE_RATE;
    candidates[candidate_count].flags = 0;
    candidate_count++;
};
if (s_profile == UAC_PROFILE_GENERIC_USB) {
    add_candidate(1, 24);
    add_candidate(1, 16);
    add_candidate(2, 24);
    add_candidate(2, 16);
} else if (s_profile == UAC_PROFILE_FTX1) {
    // D-02 try-order: QMX-parity-shaped formats first, degrade toward
    // GENERIC_USB's mono/16-bit fallback only if richer formats aren't advertised.
    add_candidate(2, 24);
    add_candidate(2, 16);
    add_candidate(1, 24);
    add_candidate(1, 16);
} else {
    add_candidate(UAC_CHANNELS, UAC_BIT_RESOLUTION);   // QMX: strict single candidate
}
```
The existing `for (int i = 0; i < candidate_count; ++i)` loop immediately below (`stream_uac.cpp:599-615`) requires no changes — it already iterates whatever `candidates[]`/`candidate_count` were populated and records the winning format into `s_format` (D-03: no downstream changes needed, `uac_pcm_to_ft8_samples()` already reads `s_format.bit_resolution`/`s_format.channels` at runtime).

The failure-path branch at `stream_uac.cpp:617-629` currently special-cases only `UAC_PROFILE_GENERIC_USB` for its "No 48k UAC mic format" message vs. a generic "Format not supported" for everything else (which includes QMX today). Since `UAC_PROFILE_FTX1` will now also multi-candidate-scan, consider whether its failure message should match GENERIC_USB's (arguably more accurate — "no candidate accepted" is the real failure mode for a scanning profile) — Claude's Discretion per CONTEXT.md, not a locked decision.

### Pattern 2: Speaker (UAC OUT) guard widening (D-04/D-05)

**What:** The `TX_CONNECTED` handler currently starts with `if (s_profile != UAC_PROFILE_QMX) { ...continue... }` (`stream_uac.cpp:687-694`), fully skipping all speaker pre-open logic for any non-QMX profile. Widen this single condition.
**When to use:** This exact guard, this phase only.
**Example:**
```cpp
// Source: main/stream_uac.cpp:687-694, MODIFIED
} else if (evt.driver.event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
    if (s_profile != UAC_PROFILE_QMX && s_profile != UAC_PROFILE_FTX1) {
        ESP_LOGI(TAG, "Speaker connected ignored profile=%s addr=%u iface=%u",
                 profile_name(s_profile),
                 (unsigned)evt.driver.addr,
                 (unsigned)evt.driver.iface_num);
        continue;
    }
    // ... rest of pre-open/claim logic UNCHANGED (stream_uac.cpp:696-768) ...
```
The pre-open block itself (`s_spk_addr`/`s_spk_iface` capture, `uac_host_device_open()`, `uac_host_device_start()` with the fixed `scfg = {2, 24, 48000, FLAG_STREAM_SUSPEND_AFTER_START}`, pump buffer + writer task allocation) needs **zero changes** — it is already profile-agnostic once the guard admits FTX-1. This directly implements D-04 (single fixed candidate matching `dds_render_24bit_stereo()`) and D-05 (no multi-format renderer — if `uac_host_device_start()` fails for this fixed config on real FTX-1 hardware, that is a hardware-checkpoint finding, not something to pre-build a fallback for).

### Pattern 3: FIFO partition guard widening + budget arithmetic (D-06/D-07)

**What:** `usb_lib_task()`'s `if (s_profile == UAC_PROFILE_QMX) { ... custom fifo ... } else { ... default ... }` (`stream_uac.cpp:413-426`) needs its condition widened, using QMX's `rx=91/nptx=18/ptx=91` split as the starting point per D-06.
**Example:**
```cpp
// Source: main/stream_uac.cpp:413-426, MODIFIED
if (s_profile == UAC_PROFILE_QMX || s_profile == UAC_PROFILE_FTX1) {
    host_config.fifo_settings_custom.rx_fifo_lines   = 91;   // 364 B — starting point, D-06
    host_config.fifo_settings_custom.nptx_fifo_lines = 18;   // 72 B  — starting point, D-06
    host_config.fifo_settings_custom.ptx_fifo_lines  = 91;   // 364 B — starting point, D-06
    ESP_LOGI(TAG, "USB FIFO profile=%s policy=qmx-custom rx=91 nptx=18 ptx=91",
             profile_name(s_profile));
} else {
    ESP_LOGI(TAG, "USB FIFO profile=%s policy=default", profile_name(s_profile));
}
```
**Hard budget constraint (verified this session against ESP-IDF HAL source, not inferred):**
- ESP32-S3's DWC_OTG core FIFO depth is read at runtime via `usb_dwc_ll_ghwcfg_get_fifo_depth()` (`components/hal/usb_dwc_hal.c:147`) — for ESP32-S3 (full-speed only PHY) this is the value the existing code comment already documents as **200 lines total** (4 bytes/line), and it is exactly consumed by QMX's `91+18+91=200`.
- `usb_dwc_hal_set_fifo_config()` calls `HAL_ASSERT(usb_dwc_hal_fifo_config_is_valid(hal, config))` (`components/hal/usb_dwc_hal.c:208`), where validity is simply `rx_fifo_lines + nptx_fifo_lines + ptx_fifo_lines <= fifo_size` (`usb_dwc_hal.c:188-195`). **This means QMX's split already consumes 100% of the available budget with zero headroom.** Any FIFO re-tune for FTX-1 that *increases* any of the three values (e.g., to accommodate CP210x's larger bulk transfers vs. CDC-ACM's 64-byte OUT-only path) must *decrease* one of the others to stay ≤200, or the assert fires at `usb_host_install()` time — **a boot-time crash, not a soft `esp_err_t` failure the app could catch and degrade from.**
- Practical implication for the hardware-checkpoint plan: before adjusting any of the three values from the 91/18/91 starting point, compute the new sum by hand and confirm ≤200. There is no runtime API to query "remaining FIFO budget" — the check is purely additive arithmetic against the fixed 200-line ceiling.
- Channel budget (separate from FIFO lines — `hal->constant_config.chan_num_total`, checked at `usb_dwc_hal.c:280`) is comfortably non-limiting for 3 endpoints (CP210x bulk, UAC IN, UAC OUT); FIFO line count, not channel count, is the actual constraint for AUDIO-03.

### Anti-Patterns to Avoid
- **Assuming FIFO exhaustion fails gracefully:** it does not — `HAL_ASSERT` in `usb_dwc_hal_set_fifo_config()` will abort the firmware if `rx+nptx+ptx > 200` for ESP32-S3. Any FIFO re-tune must be checked arithmetically before flashing, not discovered via a log warning at runtime.
- **Standing up a second candidate-scan loop or task pair for FTX-1:** already ruled out project-wide (`ARCHITECTURE.md` Anti-Pattern 3) — reuse the single `uac_lib_task()`/`usb_lib_task()` pair, branch on `s_profile` only.
- **Building a multi-format DDS renderer speculatively:** explicitly deferred by D-04/D-05 — do not add renderer variants for formats other than 24-bit/stereo until hardware validation actually shows the FTX-1's speaker rejects that fixed candidate.
- **Trusting training-data claims about FTX-1's USB Audio descriptor:** no authoritative source exists (confirmed by this session's web search); do not hardcode an assumed sample rate/bit depth/channel count as if verified — the candidate-scan loop exists precisely because this is unknown.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| UAC format negotiation / candidate probing | A custom USB Audio descriptor parser that reads `bmaControls`/format-type descriptors directly | `uac_host_device_start()`'s built-in per-candidate negotiation (already used by GENERIC_USB) | The UAC class driver already walks the interface's alternate-setting descriptors and reports success/failure per candidate; a custom parser would duplicate driver internals for no benefit |
| Speaker double-buffering / URB scheduling for isochronous OUT | A custom isochronous transfer scheduler | Existing `uac_host_device_write()` + internal ringbuffer (already tuned for QMX, 4 KB ringbuffer/288 B packets) | Already solved and hardware-validated for QMX; FTX-1 reuses identical machinery once the profile guard admits it |
| FIFO/channel budget calculation | A runtime FIFO-budget probing routine | Static arithmetic against the known 200-line ESP32-S3 ceiling (confirmed via HAL source this session) | The ceiling is a fixed hardware constant for a given ESP32-S3 PHY configuration (not runtime-negotiable); a hand-computed table is simpler and safer than adding budget-probing code that could itself trigger the same `HAL_ASSERT` it's trying to avoid |

**Key insight:** Every mechanism this phase needs (candidate scanning, speaker pre-open/suspend/resume, resampling) already exists in this codebase, proven against QMX/GENERIC_USB. The only genuinely new engineering judgment required is FIFO-line arithmetic under a fixed, non-negotiable 200-line ceiling — get that sum wrong and the firmware won't boot with audio active, regardless of which radio is attached.

## Common Pitfalls

### Pitfall 1: FIFO over-budget causes boot-time `HAL_ASSERT`, not a runtime error
**What goes wrong:** A well-intentioned "give FTX-1 more headroom for CP210x's larger bulk transfers" edit that bumps `nptx_fifo_lines` up without correspondingly reducing `rx_fifo_lines` or `ptx_fifo_lines` pushes the sum above 200.
**Why it happens:** The existing 91/18/91 split already sums to exactly 200 with zero slack; it's easy to treat "increase one value" as an isolated, safe-seeming change without re-checking the total.
**How to avoid:** Treat 200 as a hard, non-negotiable ceiling (verified via `usb_dwc_ll_ghwcfg_get_fifo_depth()` at `components/hal/usb_dwc_hal.c:147`, exercised via `usb_dwc_hal_fifo_config_is_valid()` at line 188). Any adjustment to one FIFO field must be paired with an equal-or-greater reduction elsewhere in the same commit.
**Warning signs:** Firmware fails to boot at all (not "fails to open device") immediately after a FIFO-tuning change — this is the signature of the `HAL_ASSERT` firing during `usb_host_install()`.

### Pitfall 2: FTX-1's actual descriptor may not match any of the four candidates
**What goes wrong:** If the FTX-1 advertises a sample rate other than 48 kHz (e.g., 44.1 kHz only, or a range that doesn't include exactly 48000), all four D-02 candidates fail regardless of channel/bit-depth ordering, since every candidate in the try-order is fixed at `UAC_SAMPLE_RATE` (48000).
**Why it happens:** D-02's try-order only varies channel count and bit depth, not sample rate — this mirrors the existing GENERIC_USB pattern, which makes the same simplifying assumption.
**How to avoid:** During the hardware-checkpoint plan, run `uac_host_get_device_alt_param()` for each alternate setting and log the `sample_freq_type`/`sample_freq[]` values *before* concluding "no candidate worked" is a code bug — it may correctly reflect a sample-rate mismatch the current candidate list can't express. If discrete rates other than 48 kHz are found, that's a scope question for a follow-up decision (resampler already exists for bit-depth/channel variance, but not sample-rate variance at the UAC layer).
**Warning signs:** All four candidates in the FTX-1 branch fail with `ESP_ERR_NOT_FOUND`/`ESP_ERR_NOT_SUPPORTED`, and `uac_host_printf_device_param()`'s dumped alt-settings show no exact `48000` entry.

### Pitfall 3: Speaker pre-open silently never re-attempted after a `TX_CONNECTED` failure
**What goes wrong:** The existing speaker pre-open logic only runs once, at the first `TX_CONNECTED` event, guarded by `if (!s_spk_handle)`. If `uac_host_device_open()` or `uac_host_device_start()` fails at that moment (D-05's exact "if 24-bit/stereo doesn't match, surface it" scenario), `s_spk_handle` stays `NULL` for the rest of the session — there is no retry path, matching D-08's intended graceful RX-only degradation, but this means the failure is effectively permanent per USB attach cycle, not transient.
**Why it happens:** This is intentional existing QMX design (pre-open once while heap is least fragmented) — it is correct behavior per D-08, but easy to mistake for a bug during hardware bring-up if not anticipated.
**How to avoid:** Confirm the hardware-checkpoint plan's verification steps explicitly check for this expected failure mode (status string reflects RX-only degradation per D-08) rather than treating "speaker never connects" as an unexplained regression.
**Warning signs:** `s_status_string` never reflects TX-readiness for an entire session after one `uac_host_device_start()` failure at enum time — expected, not a bug, per D-08/D-09.

## Code Examples

### Full FTX-1 mic candidate branch (combines Pattern 1's context)
```cpp
// Source: main/stream_uac.cpp, uac_lib_task() RX_CONNECTED handler (~line 589)
if (s_profile == UAC_PROFILE_GENERIC_USB) {
    add_candidate(1, 24);
    add_candidate(1, 16);
    add_candidate(2, 24);
    add_candidate(2, 16);
} else if (s_profile == UAC_PROFILE_FTX1) {
    add_candidate(2, 24);
    add_candidate(2, 16);
    add_candidate(1, 24);
    add_candidate(1, 16);
} else {
    add_candidate(UAC_CHANNELS, UAC_BIT_RESOLUTION);
}
```

### Descriptor dump call for hardware-checkpoint verification (already exists, reuse as-is)
```cpp
// Source: main/stream_uac.cpp:563 (uac_host_printf_device_param already called
// unconditionally right after uac_host_device_open() succeeds) — no code change
// needed; the hardware-checkpoint plan should capture this log output as the
// FTX-1's actual descriptor evidence.
uac_host_printf_device_param(handle);
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ARCHITECTURE.md`'s original framing of FTX-1 audio via a hypothetical generalized "bidirectional negotiation" not yet scoped to exact code lines | This phase's CONTEXT.md decisions (D-01–D-09) pin exact try-order, exact single TX candidate, and exact FIFO starting point, all directly against current `stream_uac.cpp` line numbers | Discuss-phase session, 2026-07-06 | Removes ambiguity `ARCHITECTURE.md` Pattern 3 originally left open ("extend the candidate-list branch... extend the guard") — this research and CONTEXT.md now specify precisely which values and which guards |

**Deprecated/outdated:** None — no library versions changed this phase.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | FTX-1's mic/speaker both use standard synchronous UAC sync mode (not adaptive/async), matching QMX's assumption baked into the existing pump/read code (no feedback-endpoint handling anywhere in `stream_uac.cpp`) | Summary, Pattern 2/3 | If FTX-1 uses adaptive/async sync, audible pitch drift or underrun/overrun over a multi-symbol TX slot (already flagged as Pitfall 7 in `.planning/research/PITFALLS.md`); would require new feedback-endpoint logic not currently present anywhere in this codebase — HIGH effort if wrong |
| A2 | FTX-1's UAC descriptor includes an exact 48000 Hz sample rate option (all four D-02 candidates fix sample rate at 48000, varying only channels/bits) | Pitfall 2 | If 48 kHz isn't advertised, all four candidates fail regardless of ordering; would require adding sample-rate variance to the candidate list, a design change beyond this phase's current scope |
| A3 | QMX's `91/18/91` FIFO split is a valid *starting point* for FTX-1 (i.e., CP210x's bulk endpoints don't require meaningfully more `nptx_fifo_lines` than CDC-ACM's 64-byte OUT-only path already budgeted for) | Pattern 3, D-06/D-07 | If CP210x's actual bulk MPS is larger, `nptx_fifo_lines=18` (72 B) may be insufficient, requiring a reallocation that must stay within the hard 200-line ceiling — CONTEXT.md already flags this as something to validate empirically in the hardware-checkpoint plan, not assume correct |
| A4 | No authoritative public source describes the FTX-1's USB Audio descriptor (verified negative via WebSearch this session, in addition to PROJECT.md's pre-existing flag) | Summary | Low risk to this research's conclusions (the negative finding is the point — it's why candidate-scanning is mandated), but any executor tempted to hardcode a specific format based on assumed knowledge should be redirected to the candidate-scan approach |

**If this table is empty:** N/A — see entries above; all are pre-existing unknowns already flagged in CONTEXT.md/PROJECT.md, not new speculative claims introduced by this research.

## Open Questions

1. **Does the FTX-1 advertise a discrete 48 kHz sample rate on both mic and speaker interfaces?**
   - What we know: QMX and the project's fixed constants (`UAC_SAMPLE_RATE=48000`) assume yes; no FTX-1-specific confirmation exists.
   - What's unclear: Whether the FTX-1's descriptor uses discrete rates (`sample_freq_type != 0`) including exactly 48000, a continuous range that includes it, or a different fixed rate entirely.
   - Recommendation: Hardware-checkpoint plan's first verification step should be capturing `uac_host_printf_device_param()`/alt-param dumps for both mic and speaker interfaces before evaluating candidate-scan success/failure — this diagnostic step is cheap and resolves Pitfall 2 immediately either way.

2. **Does CP210x's actual bulk transfer size on the FTX-1 exceed the FIFO headroom `nptx_fifo_lines=18` (72 B) currently budgets?**
   - What we know: CDC-ACM's 64-byte OUT-only CAT path (QMX) fits in 18 lines; CP210x adds bulk IN as well as OUT, and Silicon Labs CP210x parts commonly support up to 64-byte bulk endpoints on full-speed, but the exact wMaxPacketSize the FTX-1's CP2105 enumerates with was not independently re-verified against a descriptor dump this session (Phase 2's hardware checkpoint captured PID/interface index, not full endpoint descriptors).
   - What's unclear: Whether 18 lines remains sufficient once CP210x's bulk IN (query-response reads, used starting Phase 3) is also active concurrently with bidirectional audio.
   - Recommendation: Hardware-checkpoint plan should log actual FIFO-related error codes (`ESP_ERR_NO_MEM`/transfer failures) under combined load (audio both directions + CAT query traffic) per D-07, and be prepared to reallocate lines between `rx`/`nptx`/`ptx` while re-verifying the ≤200 sum — this is exactly the empirical validation D-07 already calls for.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| ESP-IDF v5.5.1 toolchain (`idf.py`) | Build verification | Installed at `C:\Espressif\esp-idf-v5.5.1` but not on PATH in Bash sessions (same caveat noted in 02-RESEARCH.md, still unresolved as of this session) | v5.5.1 | Manual code/header audit (as done this session); orchestrator or a manually-sourced session must confirm a clean `-Werror` build |
| `espressif/usb_host_uac` component | AUDIO-01/02 | Present in `managed_components/espressif__usb_host_uac/` | 1.3.3 (headers read directly this session) | None needed — already vendored |
| Physical FTX-1 hardware | All three success criteria (AUDIO-01/02/03) | User-confirmed available per prior phases' pattern | — | None — this phase's success criteria are explicitly hardware-verifiable (waterfall continuity, clean TX tone, real negotiation, no FIFO overrun) and cannot be validated by code review alone |
| Monitoring receiver / SDR for TX tone verification | Success Criterion 2 | Assumed available (same as any FT8 TX bring-up) | — | None — required for hardware-checkpoint plan |

**Missing dependencies with no fallback:** None blocking code-implementation plan; physical hardware and a monitoring receiver are required for the hardware-checkpoint plan specifically (same pattern as Phases 2/3).

**Missing dependencies with fallback:** `idf.py` PATH issue — falls back to manual review + orchestrator-run build, as in prior phases.

## Security Domain

> `security_enforcement: true`, ASVS Level 1, block on high (`.planning/config.json`). This phase is firmware-only USB audio transport (no network surface); most ASVS categories do not apply, consistent with prior phases' documented posture.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | No auth surface — local USB peripheral |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Partial | UAC descriptor parsing (`uac_host_get_device_alt_param`, candidate negotiation) is bounds-checked by the vendored `espressif/usb_host_uac` component, not custom code introduced this phase; no new descriptor-walking code is added |
| V6 Cryptography | No | N/A — no crypto surface in audio transport |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed/spoofed USB Audio descriptor causing the negotiation loop to misbehave | Tampering | Rely on `espressif/usb_host_uac`'s own bounds-checked descriptor parsing (`uac_host_get_device_alt_param`, `uac_host_device_start`); this phase adds only candidate *values* (channel/bit-depth lists), not new descriptor-parsing code |
| FIFO/channel resource exhaustion from a third simultaneous client causing a hard `HAL_ASSERT` (denial of availability, not a security vuln per se but a local-DoS-shaped failure mode) | Denial of Service | Documented above as Pitfall 1 — arithmetic pre-check against the fixed 200-line ceiling before any FIFO value change ships |

## Sources

### Primary (HIGH confidence)
- `C:\GitHub\Mini-FT8\main\stream_uac.cpp` — direct read, full file, this session
- `C:\GitHub\Mini-FT8\main\stream_uac.h` — direct read, this session
- `C:\GitHub\Mini-FT8\main\dds_q15.h` — direct read, this session (confirms `dds_render_24bit_stereo()` signature, fixed 24-bit stereo)
- `C:\GitHub\Mini-FT8\managed_components\espressif__usb_host_uac\include\usb\uac_host.h` — direct read, this session (confirms `uac_host_stream_config_t`, `uac_host_device_start/suspend/resume`, `FLAG_STREAM_SUSPEND_AFTER_START`, `uac_host_get_device_alt_param`)
- `C:\Espressif\esp-idf-v5.5.1\components\usb\include\usb\usb_host.h` — direct read, this session (confirms `usb_host_config_t.fifo_settings_custom` field semantics and doc comments)
- `C:\Espressif\esp-idf-v5.5.1\components\hal\usb_dwc_hal.c` — direct read, this session (confirms `usb_dwc_hal_fifo_config_is_valid()`, `HAL_ASSERT` on invalid config, runtime FIFO-depth query via `usb_dwc_ll_ghwcfg_get_fifo_depth()`)
- `C:\Espressif\esp-idf-v5.5.1\components\usb\hcd_dwc.c` — direct read, this session (confirms default Kconfig-bias FIFO calculation and 200-line total consistent with existing code comment)
- `.planning/phases/02-cp210x-usb-bring-up-cat-connection/02-RESEARCH.md` — direct read, this session (Pitfall 1/Assumption A4 context, CP2105 PID/interface confirmation)
- `.planning/research/ARCHITECTURE.md`, `PITFALLS.md` — direct read, this session (Pattern 3, Pitfall 7, Performance Traps)
- `.planning/phases/04-bidirectional-uac-audio-negotiation/04-CONTEXT.md` — direct read, this session (locked decisions D-01–D-09)

### Secondary (MEDIUM confidence)
- None — all findings this session were either direct source reads or a documented negative web-search result (see Tertiary).

### Tertiary (LOW confidence)
- WebSearch for "Yaesu FTX-1 USB audio descriptor sample rate bit depth" — returned no authoritative technical specification; confirms (via absence) that the FTX-1's UAC descriptor remains genuinely undocumented publicly, matching PROJECT.md's pre-existing flag. Treat as a confirmed gap, not a finding to act on.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependencies; existing component versions and APIs read directly from vendored headers
- Architecture: HIGH for code integration points (exact current line numbers cited from direct reads this session); LOW for the FTX-1's actual negotiated format (unconfirmable without hardware, by design — this is exactly what the hardware-checkpoint plan resolves)
- Pitfalls: HIGH for the FIFO hard-assert mechanism (traced through actual ESP-IDF HAL source, not inferred from docs) — MEDIUM for the exact FIFO re-tune values needed (starting point is analytically derived per D-06, final values require empirical hardware validation per D-07)

**Research date:** 2026-07-06
**Valid until:** Should remain valid through Phase 4 execution — the ESP32-S3 200-line FIFO ceiling and `usb_host_uac`/`usb_host_cp210x_vcp` component versions are stable, non-moving facts; re-check only if ESP-IDF is upgraded past v5.5.1 or the UAC component version changes.

---
*Phase 4 research: Bidirectional UAC Audio Negotiation*
*Researched: 2026-07-06*
