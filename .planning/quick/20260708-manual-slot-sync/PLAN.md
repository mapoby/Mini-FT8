---
status: complete
---

Add a manual coarse clock-sync command: pressing `0` on the STATUS screen
snaps the soft RTC to the most recent FT8/FT4 slot boundary, for when the
user observes (by ear, or against a reference clock/radio) that a new slot
cycle is starting right now.

## Changes

`main/main.cpp`:
- New `rtc_snap_to_slot_boundary()` (next to the existing `rtc_nudge_seconds()`)
  computes `delta_ms` = milliseconds past the most recent slot boundary using
  `rtc_now_ms()` and `g_protocol->slot_time_ms`, then shifts `rtc_ms_start`/
  `rtc_last_update` by that delta. Always rounds DOWN to the most recent
  boundary (never rounds up/nearest) since a human keypress always lags the
  true boundary, never leads it. Operates at millisecond precision (not whole
  seconds like `rtc_nudge_seconds`) because FT4's 7.5s slot period isn't a
  whole number of seconds.
- Bound to the `0` key in the STATUS screen's top-level key handler (same
  block as the existing `7`/`8` nudge keys), calling `draw_status_view()` and
  a `debug_log_line()` afterward.
- Needed a forward declaration of `rtc_update_strings()` since the new
  function sits earlier in the file than that definition.
