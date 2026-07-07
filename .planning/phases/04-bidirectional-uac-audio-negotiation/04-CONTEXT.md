# Phase 4: Bidirectional UAC Audio Negotiation - Context

**Gathered:** 2026-07-06
**Status:** Ready for planning

<domain>
## Phase Boundary

Firmware streams RX audio from the FTX-1's USB mic input into the existing FT8/FT4 decode pipeline, and TX (FSK tone) audio to its USB speaker output, using runtime format negotiation validated against the real device (AUDIO-01, AUDIO-02, AUDIO-03). No CAT/frequency/PTT work (Phase 3, complete) and no full-QSO validation (Phase 5) — this phase is audio transport only.

</domain>

<decisions>
## Implementation Decisions

User approved recommended defaults for all four gray areas identified during codebase scouting (auto-approved, no per-question discussion).

### Mic RX (UAC IN) format negotiation
- **D-01:** Replace the current hardcoded single-candidate mic negotiation for `UAC_PROFILE_FTX1` (`stream_uac.cpp:594-596`, currently just repeats QMX's exact 24-bit/stereo/48kHz format) with a real candidate-scan loop, same mechanism already used by `UAC_PROFILE_GENERIC_USB`.
- **D-02:** Candidate try-order prioritizes what QMX-parity would look like first, then degrades: **(2ch/24-bit) → (2ch/16-bit) → (1ch/24-bit) → (1ch/16-bit)**, all at 48kHz. Rationale: FTX-1 is architecturally closer to QMX (bidirectional radio-native audio) than to a generic USB adapter, so stereo/24-bit is the most probable match; degrade toward GENERIC_USB's mono/16-bit fallback only if the richer formats aren't advertised.
- **D-03:** Whatever format wins negotiation is recorded in `s_format` exactly as today (existing `uac_pcm_to_ft8_samples()` resampler already handles variable bit-depth/channels — no changes needed downstream of negotiation).

### TX speaker (UAC OUT) strategy
- **D-04:** Do not build a multi-format DDS renderer. `dds_render_24bit_stereo()` stays fixed at 24-bit/stereo output. The speaker candidate list for `UAC_PROFILE_FTX1` contains exactly **one** candidate — 2ch/24-bit/48kHz — matching what the DDS can actually render, mirroring QMX's pre-open-at-`TX_CONNECTED`-time/suspend-resume machinery (`stream_uac.cpp:687-768`) but gated on `UAC_PROFILE_FTX1` in addition to `UAC_PROFILE_QMX`.
- **D-05:** If the FTX-1's speaker doesn't accept 24-bit/stereo/48kHz, that is a hardware-validation finding for the Phase 4 hardware-checkpoint plan to surface and react to (e.g., add a second render path) — not something to pre-build speculatively now. This mirrors the project's existing "validate against real hardware, don't pre-build for unconfirmed cases" pattern (see PROJECT.md HARDEN-01 precedent).

### FIFO re-tuning
- **D-06:** Extend `usb_lib_task()`'s custom FIFO partitioning (currently QMX-only, `stream_uac.cpp:413-426`) to also apply for `UAC_PROFILE_FTX1`, using the QMX split (`rx=91/nptx=18/ptx=91` lines) as the analytical starting point, since FTX-1 needs the same bidirectional 24-bit/48k/stereo audio shape as QMX.
- **D-07:** Because FTX-1 additionally carries CP210x bulk CAT traffic (QMX's CAT is CDC-ACM interrupt+bulk sharing the same `nptx` pool QMX already reserves 18 lines for), the split should be validated/adjusted empirically during the hardware-checkpoint plan rather than assumed correct from the analytical starting point alone — same code-plan-then-hardware-checkpoint-plan pattern used successfully in Phases 2 and 3.

### Partial-negotiation fallback
- **D-08:** Follow the project's existing graceful-degradation philosophy (`ARCHITECTURE.md` Error Handling: "Audio underrun: Mute speaker, continue decoding"). If mic negotiation succeeds but speaker negotiation fails: continue in RX-only mode (waterfall/decode work, TX unavailable), surfaced via the existing `s_status_string` mechanism — do not block `audio_source_start()` entirely.
- **D-09:** If mic negotiation fails outright (no candidate accepted), this is more severe — decode/waterfall cannot function at all — but the existing failure path already handles this correctly (`"No 48k UAC mic format"` status string, `uac_host_device_close()`, no crash). No new fallback machinery needed; just extend the existing candidate-scan failure path to `UAC_PROFILE_FTX1`.

### Claude's Discretion
- Exact candidate array sizing/indexing mechanics inside `uac_lib_task()`'s `add_candidate` lambda usage for the FTX-1 branch.
- Whether the FIFO split constants get a named `UAC_PROFILE_FTX1`-specific comment block or share the QMX log line — implementation detail, not a user-facing decision.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements and roadmap
- `.planning/REQUIREMENTS.md` — AUDIO-01, AUDIO-02, AUDIO-03 (exact requirement text and traceability)
- `.planning/ROADMAP.md` — Phase 4 goal and hardware-verifiable success criteria (4 criteria: continuous waterfall, clean TX tone, real negotiation success, no FIFO overrun across cycles)
- `.planning/PROJECT.md` — Constraints section (USB Audio descriptor unconfirmed until validated on physical hardware; USB bus bandwidth re-tuning risk)

### Prior phase research (directly relevant patterns)
- `.planning/research/ARCHITECTURE.md` §"Pattern 3: Bidirectional UAC negotiation reuses the QMX 'pre-open speaker at enum time' trick, generalized" — the exact reuse pattern this phase implements
- `.planning/phases/02-cp210x-usb-bring-up-cat-connection/02-RESEARCH.md` — Pitfall 1 (FIFO exhaustion, explicitly deferred to Phase 4/AUDIO-03), Assumption A4 (Kconfig-default FIFO sizing was sufficient without audio active — no longer true once audio is active)

### Code — direct read required before planning
- `main/stream_uac.cpp` — `uac_lib_task()` (mic candidate-scan loop lines ~576-629, speaker pre-open lines ~687-768), `usb_lib_task()` (FIFO partitioning lines ~413-426), `profile_name()`, `uac_on_block_processed()`
- `main/stream_uac.h` — `UAC_PROFILE_FTX1` enum (already added, Phase 2), `UAC_SAMPLE_RATE`/`UAC_BIT_RESOLUTION`/`UAC_CHANNELS` constants
- `main/dds_q15.cpp`/`.h` — `dds_render_24bit_stereo()` (fixed-format TX renderer; not being generalized this phase per D-04)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `uac_lib_task()`'s existing `add_candidate` lambda + candidate-array-and-loop pattern (`GENERIC_USB` branch) — directly reusable for `FTX1`, just needs its own candidate list and try-order (D-02)
- QMX's speaker pre-open-at-enum/suspend-resume machinery (`s_spk_handle`, `s_spk_writer_task`, `uac_host_device_resume/suspend`) — reusable as-is for FTX-1, only the profile guard needs broadening
- `uac_pcm_to_ft8_samples()` resampler — already format-agnostic (handles 16/24-bit, mono/stereo), no changes needed

### Established Patterns
- Profile-gated branching inside one shared `stream_uac.cpp` task pair (never fork a second USB-host task pair per radio) — confirmed anti-pattern in `.planning/research/ARCHITECTURE.md`
- Two-plan-per-hardware-phase pattern from Phases 2/3: one "code implementation" plan + one "physical hardware checkpoint" plan that validates against the real FTX-1 and adjusts constants empirically

### Integration Points
- `uac_lib_task()`'s `UAC_HOST_DRIVER_EVENT_RX_CONNECTED` handler (mic) and `UAC_HOST_DRIVER_EVENT_TX_CONNECTED` handler (speaker) — both need their `UAC_PROFILE_FTX1` branches added
- `usb_lib_task()`'s FIFO partitioning `if (s_profile == UAC_PROFILE_QMX)` condition — needs broadening to include `UAC_PROFILE_FTX1`

</code_context>

<specifics>
## Specific Ideas

No specific UI/behavior requests beyond the four decisions above — this is a hardware-transport phase with no user-facing surface beyond the existing waterfall/status-string display.

</specifics>

<deferred>
## Deferred Ideas

- Multi-format DDS TX renderer (supporting negotiated speaker formats other than 24-bit/stereo) — deferred until hardware validation actually shows the FTX-1 needs it (D-05); not scoped into this phase.
- TX power control (POWER-01/POWER-02) and DATA-U re-assertion hardening (HARDEN-01) — already tracked as v2 requirements in REQUIREMENTS.md, unrelated to audio.

### Reviewed Todos (not folded)
None — no pending todos matched this phase (STATE.md shows "Pending Todos: None yet").

</deferred>

---

*Phase: 04-Bidirectional UAC Audio Negotiation*
*Context gathered: 2026-07-06*
