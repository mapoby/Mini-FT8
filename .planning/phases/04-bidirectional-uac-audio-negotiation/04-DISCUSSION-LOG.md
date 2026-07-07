# Phase 4: Bidirectional UAC Audio Negotiation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-06
**Phase:** 04-bidirectional-uac-audio-negotiation
**Areas discussed:** Mic RX negotiation candidates, TX speaker strategy, FIFO re-tuning approach, Partial-negotiation fallback

User opted to approve Claude's recommended option for all four areas in one pass rather than a per-area interactive discussion ("im ok to approve recommended options, please proceed with auto approval").

---

## Mic RX (UAC IN) negotiation candidates

| Option | Description | Selected |
|--------|-------------|----------|
| Extend candidate-scan loop, stereo/24-bit first | Reuse GENERIC_USB's scan mechanism; try (2ch/24b) → (2ch/16b) → (1ch/24b) → (1ch/16b) at 48kHz, since FTX-1 is architecturally closer to QMX than a generic adapter | ✓ (recommended) |
| Extend candidate-scan loop, mono/16-bit first (GENERIC_USB's existing order) | Reuse GENERIC_USB's order unchanged | |
| Keep single hardcoded QMX-shaped candidate | No negotiation; likely to fail per Phase 2 research findings | |

**User's choice:** Recommended (stereo/24-bit-first candidate scan).
**Notes:** None beyond auto-approval.

---

## TX speaker (UAC OUT) strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Single candidate (24-bit/stereo/48kHz), matching DDS renderer capability | No multi-format DDS work; if hardware rejects it, that's a hardware-checkpoint finding | ✓ (recommended) |
| Multi-format DDS renderer supporting negotiated speaker formats | Builds speculative format-flexibility before hardware confirms it's needed | |

**User's choice:** Recommended (single candidate, no speculative DDS generalization).
**Notes:** Deferred multi-format DDS renderer to future work if hardware validation shows it's actually needed (see Deferred Ideas).

---

## FIFO re-tuning approach

| Option | Description | Selected |
|--------|-------------|----------|
| Extend QMX's split (rx=91/nptx=18/ptx=91) to FTX-1 as starting point, validate empirically at hardware checkpoint | Analytical starting point + hardware-driven adjustment, matching Phase 2/3's two-plan pattern | ✓ (recommended) |
| Derive a brand-new analytical split accounting for CP210x bulk endpoints before any hardware testing | Requires endpoint size assumptions that can't be confirmed without hardware | |
| Leave default Kconfig sizing and only intervene if overruns appear | Riskier — Phase 2 research already flagged this as insufficient once audio is active | |

**User's choice:** Recommended (QMX split as starting point, empirical hardware validation).
**Notes:** None beyond auto-approval.

---

## Partial-negotiation fallback

| Option | Description | Selected |
|--------|-------------|----------|
| Speaker-fail → continue RX-only; mic-fail → existing failure path (no new fallback machinery) | Matches existing project error-handling philosophy (graceful degradation, log + continue) | ✓ (recommended) |
| Any negotiation failure → hard-fail audio_source_start() entirely | Simpler but inconsistent with existing degrade-and-continue patterns elsewhere in the codebase | |

**User's choice:** Recommended (asymmetric fallback: RX-only degrade on speaker failure, existing hard-failure path retained for mic failure).
**Notes:** None beyond auto-approval.

---

## Claude's Discretion

- Exact candidate array sizing/indexing inside `uac_lib_task()`'s `add_candidate` lambda usage for the FTX-1 branch.
- Whether FIFO split constants get FTX-1-specific comments or share the QMX log line.

## Deferred Ideas

- Multi-format DDS TX renderer — only if hardware validation shows the FTX-1 speaker rejects 24-bit/stereo/48kHz.
- TX power control (POWER-01/POWER-02) and DATA-U re-assertion hardening (HARDEN-01) — already tracked as v2 requirements, unrelated to this phase.
