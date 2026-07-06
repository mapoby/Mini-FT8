---
phase: 03-cat-command-implementation
plan: 02
subsystem: radio-control
tags: [ftx1, cat, cp210x, usb, hardware-verification, checkpoint]

# Dependency graph
requires:
  - phase: 03-cat-command-implementation
    provides: "FTX-1 CAT command encoding (FA/MD/TX) and CAT-1 line coding (Plan 01)"
provides:
  - "Hardware-confirmed pass on all four ROADMAP Phase 3 success criteria (SYNC-01/02/03, PTT-01/02)"
  - "Resolution of 03-RESEARCH.md Open Question 1 / Assumption A1: CAT-1 alone keys the FTX-1 via TX1;/TX0; — no CAT-2 needed"
affects: [Phase 4 (audio pipeline work can proceed on a hardware-confirmed CAT transport)]

# Tech tracking
tech-stack:
  added: []
  patterns: []

key-files:
  created: []
  modified: []

key-decisions:
  - "CAT-1 (this project's only open CAT port) DOES key/unkey the FTX-1 via TX1;/TX0; — the CAT manual's port-function table associating 'TX Controls' with CAT-2 does not preclude CAT-1 from doing so on this radio. No CAT-2 port was opened; no architectural re-scoping required."
  - "Task 2's first attempt failed for reasons unrelated to CAT command correctness (USB hub enumeration + a call-site ordering bug gating cp210x_try_open() behind mic-audio negotiation success). Both were fixed as a gap-closure Task 3 on plan 03-01, not as part of this plan (03-02 has files_modified: [] by design — this plan is checkpoint-only)."

requirements-completed: [SYNC-01, SYNC-02, SYNC-03, PTT-01, PTT-02]

# Metrics
duration: N/A (human-verification checkpoint spanning multiple sessions; no task-timed execution)
completed: 2026-07-06
---

# Phase 3 Plan 2: Physical FTX-1 Hardware Bring-up Summary

All four ROADMAP Phase 3 success criteria confirmed on the physical FTX-1: frequency sync, DATA-U mode, PTT key via `TX1;`, and clean unkey/restore via `TX0;`/`FA` — resolving the project's highest-risk open question (CAT-1 alone keys the radio; CAT-2 is not needed).

## Accomplishments

- **Task 1 (build):** `idf.py build` in a sourced ESP-IDF v5.5.1 shell — clean, 0 errors, `cdc_acm_host_line_coding_set` linked correctly, merged binary produced. Confirmed "build-ok".
- **Task 2 (physical hardware, attempt 1 — FAILED):** Flashed to Cardputer (COM12), FTX-1 connected via the Cardputer's main USB-C port, live serial log via a UART-TTL adapter on GPIO4/GPIO5. `cat_cdc_ready()` never became true; no CAT command could be tested ("CAT not ready; tune skipped").
- **Gap closure (executed as plan 03-01's Task 3, not part of this plan):** Two root causes found and fixed:
  1. `sdkconfig` had `CONFIG_USB_HOST_HUBS_SUPPORTED` disabled — the FTX-1 enumerates through an internal USB hub joining its CP2105 CAT chip and a separate USB-audio chip; without hub support the ESP32-S3 USB host stack could not see past the hub to reach the CP2105 at all. Fixed: enabled `CONFIG_USB_HOST_HUBS_SUPPORTED=y` and `CONFIG_USB_HOST_HUB_MULTI_LEVEL=y`.
  2. `main/stream_uac.cpp`'s `cp210x_try_open()` call for the FTX-1 profile was only reachable inside the UAC mic-audio-stream-negotiation success branch. The real FTX-1 mic only advertises 16-bit while the FTX-1 profile only tries a 24-bit candidate, so negotiation always failed and the `continue;` on the `!started` path skipped the CAT-open call every boot. Fixed: moved `cp210x_try_open()` to fire unconditionally once the UAC device opens, independent of mic-negotiation outcome.
  - Both fixes committed under plan 03-01: `db2186b` (fix), `e08dc8f` (docs — 03-01-SUMMARY.md Task 3 addendum). See `.planning/phases/03-cat-command-implementation/03-01-SUMMARY.md` for full fix detail and verification. Firmware was rebuilt and reflashed after this fix.
- **Task 2 (physical hardware, attempt 2 — PASSED):** With the rebuilt/reflashed firmware, live serial log confirmed:
  - `CP210x opened (FTX-1 CAT-1 iface 0); RTS/DTR deassert: ESP_OK`
  - `CP210x CAT-1 line coding set (38400 8N1): ESP_OK`
  - `FTX-1 sync ok freq=3573000 mode=DATA-U` (initial sync on connect)
  - `FTX-1 sync ok freq=7074000 mode=DATA-U` (after user changed band/frequency in the UI to 40m, 7.074 MHz) — confirms 9-digit FA + MD0C; sent correctly
  - `FTX-1 TX stop` following a TX/tune cycle — confirms TX0; sent and end_tx path ran
  - User directly confirmed on the physical FTX-1's own display/front panel (not just the log):
    1. **FREQUENCY (SYNC-01) — PASS:** FTX-1's VFO-A display followed to 7.074000 MHz, matching Mini-FT8.
    2. **MODE (SYNC-02) — PASS:** FTX-1 displayed DATA-U mode after sync.
    3. **PTT KEY (PTT-01) — PASS.** This was the HIGH-RISK Open Question 1 from 03-RESEARCH.md (whether `TX1;` over CAT-1 — this project's only open port — actually keys the radio, given the CAT manual's port-function table associates "TX Controls" primarily with CAT-2). CONFIRMED RESOLVED: the FTX-1's own PTT/TX indicator engaged when `TX1;` was sent over CAT-1. CAT-1 alone controls PTT; no CAT-2 or RTS/DTR fallback was needed. This resolves 03-RESEARCH.md's Assumption A1 as TRUE — no escalation, no re-scoping.
    4. **UNKEY + RESTORE (PTT-02/SYNC-03) — PASS:** FTX-1 unkeyed cleanly on `TX0;`, no issues reported.
  - User's exact confirmation: "all yes" to all three follow-up questions (frequency changed to 7.074 MHz on the radio's own display, DATA-U shown, PTT indicator engaged and turned off cleanly).

## Observed Hardware Parameters

- **CAT-1 line rate:** 38400 8N1 (factory default, as configured) — no mismatch reported; the log's `ESP_OK` on line-coding-set combined with the radio actually reacting confirms 03-RESEARCH.md Assumption A2 held.
- **FTX-1 firmware version:** not explicitly read/reported this session. CAT commands worked correctly, which per 03-RESEARCH.md's validity note requires "Ver. 1.08 or later," so the installed firmware is implicitly at or above that baseline, but no specific version string was captured.

## Task Commits

This plan is checkpoint-only (`files_modified: []` in frontmatter) — no code was changed as part of 03-02 itself. The two fixes required to pass Task 2 on retry were committed under plan 03-01:

1. `db2186b` — fix(03-01): USB hub support + decouple CP210x CAT-open from UAC mic negotiation
2. `e08dc8f` — docs(03-01): Task 3 gap-closure addendum

**Plan metadata:** (this commit) — docs(03-02): record FTX-1 CAT hardware bring-up results

## Files Created/Modified

None (checkpoint-only plan; see plan 03-01 for the gap-closure file changes).

## Decisions Made

- CAT-1 alone keys/unkeys the FTX-1 — no CAT-2 needed, no re-scoping of the locked single-port (CAT-1 only) ROADMAP/REQUIREMENTS decision.
- Task 2's initial failure was a transport/enumeration bug (USB hub support + call-site gating), not a CAT-protocol or PTT-scoping problem — fixed via a gap-closure task on 03-01 rather than reopening 03-02's scope.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] USB hub support disabled, blocking CP210x enumeration entirely**
- **Found during:** Task 2, attempt 1
- **Issue:** `CONFIG_USB_HOST_HUBS_SUPPORTED` was disabled; the FTX-1 enumerates through an internal USB hub, so the ESP32-S3 USB host stack could not reach the CP2105 CAT chip at all — `cat_cdc_ready()` never became true.
- **Fix:** Enabled `CONFIG_USB_HOST_HUBS_SUPPORTED=y` and `CONFIG_USB_HOST_HUB_MULTI_LEVEL=y` in `sdkconfig`.
- **Files modified:** `sdkconfig` (committed under plan 03-01, not this plan)
- **Committed in:** `db2186b` (plan 03-01, Task 3)

**2. [Rule 1 - Bug] `cp210x_try_open()` unreachable when FTX-1 mic UAC negotiation fails**
- **Found during:** Task 2, attempt 1
- **Issue:** `main/stream_uac.cpp` only called `cp210x_try_open()` for the FTX-1 profile inside the UAC mic-stream-negotiation success branch. The real FTX-1 mic only advertises 16-bit while the FTX-1 profile only tries a 24-bit candidate, so negotiation always failed and the CAT-open call was skipped on every boot.
- **Fix:** Moved `cp210x_try_open()` to run unconditionally once the UAC device opens, independent of mic-negotiation outcome.
- **Files modified:** `main/stream_uac.cpp` (committed under plan 03-01, not this plan)
- **Committed in:** `db2186b` (plan 03-01, Task 3)

---

**Total deviations:** 2 auto-fixed (1 blocking, 1 bug), both committed under plan 03-01's Task 3 gap closure rather than this plan.
**Impact on plan:** Both fixes were necessary preconditions for Task 2 to even attempt CAT-protocol verification; neither touched CAT command encoding or PTT-scoping logic from plan 03-01's Tasks 1-2. No scope creep within 03-02 itself (this plan remains checkpoint-only, zero files modified).

## Issues Encountered

Task 2's first attempt failed before any CAT command could be tested, due to the two transport-layer issues documented above (USB hub support, CAT-open call-site gating). Both were root-caused via live serial debugging (UART-TTL adapter on GPIO4/GPIO5, since the Cardputer's single USB-C port was occupied by the FTX-1 connection) and resolved via a gap-closure Task 3 added to plan 03-01. Firmware was rebuilt and reflashed, after which Task 2's retry passed all four criteria cleanly.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- All four ROADMAP Phase 3 success criteria (SYNC-01, SYNC-02, SYNC-03, PTT-01, PTT-02) are hardware-confirmed.
- The project's highest-risk open question (CAT-1 PTT scoping) is resolved with no architectural changes needed — Phase 4 work can build on a hardware-confirmed CAT-1 transport and FTX-1 command set.
- No blockers. Minor gap: FTX-1 firmware version string was not explicitly captured this session; not blocking since CAT commands are already confirmed working (implies firmware is at least at the CAT-capable baseline per 03-RESEARCH.md's validity note).

## Self-Check: PASSED

- CONFIRMED: `idf.py build` succeeded clean per user report ("build-ok"), Task 1.
- CONFIRMED: Live serial log lines reported by user match expected CAT sequence (`CP210x opened`, line coding set, `FTX-1 sync ok freq=7074000 mode=DATA-U`, `FTX-1 TX stop`).
- CONFIRMED: User directly observed all four criteria on the physical FTX-1 front panel/display ("all yes" to all three follow-up confirmation questions).
- CONFIRMED: Gap-closure commits `db2186b` and `e08dc8f` exist in git history (plan 03-01).

---
*Phase: 03-cat-command-implementation*
*Completed: 2026-07-06*
