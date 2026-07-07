---
status: resolved
trigger: |
  FT8 slot-boundary/RTC timing misalignment: Mini-FT8's on-screen FT8 slot progress bar and the
  audio decode slot-boundary calculation do not match the actual real-world FT8 timing coming from
  a physical FTX-1 radio (confirmed by ear/eye comparison against the FTX-1's own received audio).
  Cardputer has no RTC hardware (DS3231 not detected); time is set manually via on-screen date/time
  editor and stored via rtc_apply_manual_time_from_strings() in main/main.cpp. Manually correcting
  the date/time to match real UTC (confirmed accurate to ~seconds against a live UTC reference) did
  not fix the misalignment and even coincided with decode rate dropping from ~36% to 0% over a 20+
  cycle window. Waterfall shows real signal activity throughout, ruling out band conditions/dead-band
  as the cause. This bug is radio-agnostic (shared main.cpp code), not specific to the FTX-1 backend
  built in this milestone's Phases 1-5.
created: 2026-07-07
updated: 2026-07-07
---

## Symptoms

- **Expected behavior:** The firmware's FT8 slot progress bar and the audio-decode slot-boundary
  calculation should align with real-world UTC 15-second FT8 slot boundaries, matching the timing of
  actual received transmissions from other stations (as heard through the physical FTX-1 radio).
- **Actual behavior:** The on-screen slot progress bar finishes later than the actual FT8 audio
  transmission heard from the FTX-1 (directly observed by ear/eye). Before any manual clock
  correction this session, decoded messages consistently reported `t=` offsets of -1.28s to -1.60s
  (ft8_lib's per-decode fine sync offset), and decode rate was ~36% (8/22 cycles) — signals were
  being partially captured near the edge of the tolerance window. After two manual date/time
  corrections this session (first an apparent ~208-day date jump from a bad manual entry, then a
  ~1-hour GMT-vs-UTC correction), the on-screen date/time was confirmed to match real UTC within
  ~seconds — yet decode rate dropped to 0/36+ cycles, and the user directly observed the progress
  bar still finishing later than the FTX-1's actual received audio.
- **Error messages:** None — no crash, panic, or ESP_ERR. Firmware logs `FT8: Decoded 0 unique
  messages` repeatedly; `Candidates found: 50` still appears every cycle (Costas-sync detection
  still occurs; full LDPC decode of complete messages fails).
- **Timeline:** Observed today (2026-07-07) during the Phase 5 (End-to-End Integration and Parity
  Testing) hardware checkpoint for the Yaesu FTX-1 radio backend (05-01-PLAN.md). Unknown whether
  this predates today's session — the Cardputer has never had working RTC hardware (DS3231 not
  detected at boot), so manual time entry has always been the only time source; this may be a
  long-standing but previously unnoticed/unmeasured issue, now surfaced by close comparison against
  a real radio's audio during hardware-checkpoint testing.
- **Reproduction:** Boot the Cardputer, manually set date/time via the on-screen date/time editor
  (Settings menu, "S" then fields 5/6 for Date/Time), connect to a live FT8 band via a physical
  radio (FTX-1 in this case), and compare the firmware's slot progress bar / decode timing against
  the actual audio timing heard from the radio. Misalignment is audible/visible directly, and
  reflected in decoded messages' `t=` offset field and overall decode success rate.

## Current Focus

hypothesis: CONFIRMED by live hardware trial. Manual UTC time entry via the on-screen editor seeds
  rtc_epoch_base directly from the typed digit string, with no compensation for the several-to-tens-
  of-seconds of human read/type latency between reading an external UTC reference and pressing
  Enter. A precisely-timed low-latency re-entry (pre-staged digits, Enter pressed in sync with a
  live UTC reference) restored t= decode offsets to near 0.0s and full decode recovery, with no
  code change to the RTC/slot-boundary math.
reasoning_checkpoint:
  hypothesis: "Manual time-entry latency (not a clock/timezone/decode bug) causes the seeded
    rtc_epoch_base to be off by up to tens of seconds, pushing FT8 decode outside its ~2.5s sync
    tolerance."
  confirming_evidence:
    - "User's live hardware trial: imprecise entry produced consistent t= offsets of -1.28s to
      -1.60s and ~36% decode; precise, pre-staged entry produced t= offsets tightly clustered near
      0.00s (range -0.48s to +0.24s) across 3 consecutive cycles with 5-7 real decodes each cycle
      (CQ calls, replies, grids, RR73/report exchanges)."
    - "Code reading confirmed a single shared rtc_now_ms() clock source feeds the progress bar, TX
      slot-boundary trigger, and RX decode-trigger identically — ruling out a dual-clock bug as an
      alternative explanation for the improvement."
    - "No TZ/DST configuration exists anywhere in the project (grep confirmed) — ruling out a
      timezone bug as an alternative explanation."
  falsification_test: "If offsets/misalignment had persisted despite a carefully-timed low-latency
    entry, the hypothesis would have been refuted and the bug would have to be in RTC/slot-boundary
    code or audio-side sample-rate drift instead. This did not occur — offsets collapsed to near
    zero immediately upon precise entry."
  fix_rationale: "The root cause is entry latency reintroduced on every full HH:MM:SS retype (the
    only correction path previously available). Adding a lightweight +/-1s nudge lets the user
    fine-tune the already-committed clock against a live reference without retyping the full
    string, eliminating the latency-reintroduction path without touching the slot-boundary/decode
    math (which was already confirmed correct)."
  blind_spots: "Long-term drift of the ESP32 soft RTC between manual corrections is not addressed
    (out of scope — no RTC hardware is present on this unit). The nudge UI has been hardware-
    verified by the user (build clean, keys behave as designed)."
test: Live hardware trial (Cardputer + FTX-1), timed manual entry with pre-staged digits; then a
  second hardware trial building/flashing the nudge-key fix and confirming key behavior.
expecting: N/A — hypothesis confirmed; fix confirmed on hardware; see reasoning_checkpoint above.
next_action: None — session resolved and archived.
tdd_checkpoint: null

## Evidence

- timestamp: 2026-07-07
  checked: main/main.cpp update_countdown(), redraw_countdown_now(), check_slot_boundary(), and
    main/ft8_audio_pipeline.cpp's slot-tracking loop (slot_idx = rtc_now_ms() / slot_ms, checked on
    every audio block, ~every symbol period).
  found: All four consumers (progress bar draw, redraw-on-demand, TX slot-boundary trigger, and the
    RX decode-trigger slot tracker) call the same rtc_now_ms() function and the same
    `now_ms / g_protocol->slot_time_ms` formula. There is exactly one clock source in the firmware;
    no separate "audio clock" or free-running local counter is used for slot alignment — the audio
    pipeline re-queries rtc_now_ms() on every processed block, so it cannot drift independently of
    the UI's clock.
  implication: The "progress bar and decode-trigger use inconsistent clock sources" hypothesis
    (from the original task framing) is refuted by direct code reading. Whatever the true root
    cause is, it must be a shared error in the single rtc_now_ms() value itself (i.e. wrong epoch
    seeded), not a divergence between two separate clocks.
- timestamp: 2026-07-07
  checked: rtc_set_from_strings_source() (main.cpp:2250) — parses g_date/g_time strings with
    sscanf, builds `struct tm` with tm_isdst left at 0 (zero-initialized), calls mktime(), then
    seeds rtc_epoch_base from the result plus esp_timer_get_time() as the monotonic anchor.
    rtc_update_strings_from_epoch() (the reverse path, used for display) uses localtime_r() on that
    same epoch. Checked sdkconfig for any TZ override (grep for TZ/timezone/setenv/tzset across
    main/, components/board_cardputer_adv/, components/external_rtc/, components/storage_service/).
  found: No `setenv("TZ", ...)` or `tzset()` call anywhere in project code, and no TZ-related
    sdkconfig entry. ESP-IDF/newlib defaults to UTC0 (no DST) when TZ is unset. mktime() and
    localtime_r() are therefore both operating in UTC, and — critically — since both the "commit"
    direction (mktime) and the "display" direction (localtime_r) use the exact same (absent) offset,
    any hypothetical non-UTC TZ would round-trip invisibly (display would still show back what was
    typed) without affecting this conclusion either way.
  implication: A timezone/DST offset bug in mktime()/localtime_r() is refuted — there is no TZ
    configured, so no offset is being silently applied. This directly explains why the ~1hr
    "GMT-vs-UTC" correction attempt made things worse (0% decode) rather than better: there was no
    actual TZ bug for that correction to fix, so shifting the clock by ~1hr introduced a *new*,
    much larger real error on top of whatever small error was already present.
- timestamp: 2026-07-07
  checked: rtc_apply_manual_time_from_strings() / rtc_set_from_strings_source() control flow for
    exactly what moment in time gets captured as "now" when the user presses Enter on the time
    field.
  found: The epoch seeded is built entirely from the typed digit string (whatever the user read off
    an external reference clock and then typed), with esp_timer_get_time() captured only at the
    instant Enter is processed. There is no correction for the elapsed time between "user reads
    reference clock" and "user finishes typing and presses Enter" — that gap is entirely absorbed
    into the seeded epoch's error. The user's own note that one correction attempt was independently
    confirmed to be ~19 seconds off from a live UTC reference (after accounting for "human read/type
    latency") is direct, first-party evidence that this latency is on the order of 10s of seconds —
    an order of magnitude larger than FT8's ~2.5s decode sync tolerance.
  implication: A ~19s entry error is fully sufficient on its own to explain both symptoms: (1) the
    pre-correction ~1.3-1.6s t= bias (a smaller, "lucky" residual error from an earlier, faster
    manual entry) and (2) the 0% decode after the "fix" (a slower/larger-latency entry this time,
    coincidentally worse). No additional RTC/decode code bug is needed to explain the data; the
    editor's lack of latency compensation is sufficient by itself.
- timestamp: 2026-07-07
  checked: Live hardware confirmation trial — user pre-staged all time-entry digits except the
    final seconds, watched a precise UTC reference, and pressed Enter at a predictable round-second
    moment to minimize edit latency to ~1-2s, then ran multiple FT8 decode cycles.
  found: t= offsets collapsed to a tight cluster near 0.00s (range -0.48s to +0.24s) across 3
    consecutive cycles, with 5-7 real decodes per cycle including full CQ/reply/grid/RR73 exchanges
    — essentially full decode recovery, compared to the prior consistent -1.28s to -1.60s bias and
    ~36% decode rate.
  implication: Confirms the manual-entry-latency hypothesis directly; no dual-clock, timezone, or
    audio sample-rate drift bug is needed to explain the data.
- timestamp: 2026-07-07
  checked: Hardware verification of the applied fix — idf.py build (clean, exit 0, only main.cpp
    recompiled) and idf.py -p COM12 flash to the physical Cardputer (FTX-1 briefly disconnected
    first, since the shared-USB-pins constraint from the 04-02 checkpoint made COM12 unavailable
    while it was attached).
  found: Build succeeded with no errors or warnings. After flashing, the user entered STATUS view
    and confirmed pressing '7' decrements the displayed Time: field by 1s and '8' increments it by
    1s, exactly as designed, with no key collisions or unexpected behavior.
  implication: The rtc_nudge_seconds() fix and '7'/'8' key wiring work correctly on real hardware,
    completing verification of the fix.

## Eliminated

- hypothesis: Band conditions / dead band causing zero decodes
  status: eliminated
  reasoning: User confirmed the waterfall shows real, active signal traces throughout the
    zero-decode window (20+ minutes), ruling out a propagation lull as the sole explanation.
- hypothesis: Gross UTC/timezone error (e.g. local time entered instead of UTC) as the sole cause
  status: eliminated
  reasoning: After correcting to UTC, the Cardputer's displayed date/time (2026-07-07 07:53:55) was
    confirmed within ~19 seconds of a live UTC reference (2026-07-07 07:54:14) — accounting for
    human read/type latency, the wall-clock value itself appears essentially correct, yet the
    progress-bar/decode misalignment persisted and decode rate did not improve (in fact worsened in
    this session, though this could be confounded by band timing).
- hypothesis: The on-screen progress bar and the audio-decode slot-boundary trigger read
    inconsistent clock sources or update on different timing, causing them to visually/functionally
    disagree with each other.
  status: eliminated
  reasoning: Direct code reading of main.cpp (update_countdown, redraw_countdown_now,
    check_slot_boundary) and ft8_audio_pipeline.cpp (the block-processing loop's slot_idx tracking)
    shows all four call sites use the identical rtc_now_ms() function and identical
    `/ g_protocol->slot_time_ms` arithmetic, re-evaluated on every block/tick — there is exactly one
    clock source in the firmware, so no dual-clock divergence is possible.
- hypothesis: A timezone/DST handling bug in mktime()/localtime_r() (e.g. tm_isdst mishandling or an
    implicit non-UTC TZ) silently shifts the seeded epoch away from true UTC.
  status: eliminated
  reasoning: No `setenv("TZ", ...)` / `tzset()` call and no TZ-related sdkconfig entry exist
    anywhere in the project; ESP-IDF/newlib defaults to UTC0 with no TZ set, and tm_isdst is
    zero-initialized (correct for a DST-less UTC0 zone). mktime() and localtime_r() both operate on
    the same (absent) offset, so no hidden shift is being applied in either direction.
- hypothesis: Audio-side sample-rate drift between the FTX-1 and the Cardputer's decode pipeline
    causing progressive slot misalignment.
  status: eliminated
  reasoning: Not needed as an explanation — the manual-entry-latency hypothesis was confirmed
    directly by the live hardware trial (precise entry alone restored near-0.00s offsets and full
    decode recovery), so no residual discrepancy remains that would require invoking sample-rate
    drift.

## Resolution

root_cause: Manual UTC time entry via the on-screen date/time editor (rtc_set_from_strings_source /
  rtc_apply_manual_time_from_strings in main/main.cpp) seeds rtc_epoch_base directly from the typed
  digit string with no compensation for the several-to-tens-of-seconds of human latency between
  reading an external UTC reference and pressing Enter. Every consumer of time (progress bar,
  TX slot-boundary trigger, and the audio pipeline's RX decode-trigger) is confirmed to derive from
  this single shared rtc_now_ms() value via identical arithmetic — there is no dual-clock or
  timezone bug. A single manual entry with ~19s of latency (as independently measured by the user)
  is more than sufficient to explain both the original ~1.3-1.6s decode bias (an earlier, faster
  entry with smaller residual error) and the 0% decode rate after the "GMT-vs-UTC correction" (a
  slower entry this time, since there was no real TZ bug for that correction to fix — it just added
  a new, larger error on top).
fix: Confirmed by live hardware trial — root cause was manual time-entry latency, not a code defect
  in slot-boundary/decode timing. Applied a proportionate UX fix: added a new
  `rtc_nudge_seconds(int delta)` helper (main/main.cpp, near rtc_apply_manual_time_from_strings)
  that adjusts the already-committed rtc_epoch_base by +/-1 second, re-syncs the ESP RTC and DS3231
  (if present), and persists via save_station_data() — mirroring the existing manual-commit path
  exactly. Wired to two new keys in the STATUS view (idle, non-editing state): '7' = nudge -1s,
  '8' = nudge +1s. This lets the user fine-tune the clock against a live UTC reference a second at a
  time without retyping the full HH:MM:SS string, which was the mechanism that kept reintroducing
  tens-of-seconds of read/type latency on every correction attempt. No change was made to
  rtc_now_ms(), the slot-boundary arithmetic, or the decode-trigger path, since code reading
  confirmed those were already correct and shared by all consumers.
verification: Confirmed on physical hardware. idf.py build completed cleanly (exit 0, no
  errors/warnings, only main.cpp recompiled). Flashed to the Cardputer via idf.py -p COM12 flash
  (FTX-1 briefly disconnected first due to the shared-USB-pins constraint noted in the 04-02
  checkpoint). User booted the device, entered STATUS view, and confirmed pressing '7' decrements
  the Time: field by 1s and '8' increments it by 1s, exactly as designed, with no key collisions.
  Root-cause confirmation was separately validated by a live FT8 decode trial: precise, low-latency
  manual time entry (pre-staged digits, Enter timed against a UTC reference) restored t= offsets to
  near 0.00s and decode success from ~36%/0% back to near-full recovery (5-7 real decodes per cycle
  across 3 cycles).
files_changed:
  - main/main.cpp (added rtc_nudge_seconds() helper; added '7'/'8' key handling in STATUS view)
