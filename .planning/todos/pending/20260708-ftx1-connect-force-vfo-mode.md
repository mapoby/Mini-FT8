---
created: 2026-07-08T16:35:00.000Z
title: FTX-1 Connect button should force VFO mode before frequency sync
area: general
files:
  - main/radio_control_ftx1.cpp:27-47 (ftx1_sync_frequency_mode)
  - main/main.cpp:5151-5153 (STATUS key '2' -> begin_usb_host_mode())
---

## Problem

On the STATUS screen, pressing `2` (Connect) calls `begin_usb_host_mode()`,
which eventually syncs frequency via `ftx1_sync_frequency_mode()` — this
sends `FA%09d;` (set VFO-A frequency) and `MD0C;` (set DATA-U mode) over CAT.

User reports: if the FTX-1 is in Memory-channel mode (not VFO mode) when
Connect is pressed, nothing happens — the CAT `FA` command appears to be
ignored, presumably because the radio is displaying/tuned to a recalled
memory channel rather than a free-tunable VFO, and Yaesu CAT `FA` typically
only takes effect in VFO mode.

## Solution (TBD — needs correct CAT command identified + hardware verification)

Send a CAT command to force the radio into VFO mode (VFO-A) before (or as
part of) the frequency sync that already happens in
`ftx1_sync_frequency_mode()` (`main/radio_control_ftx1.cpp:27`), so pressing
Connect always works regardless of the radio's prior mode state.

Needs research, not a blind guess: this project's convention (see
`.planning/PROJECT.md` Key Decisions and the CAT-01/02/03 hardware-verified
requirements) is to confirm exact CAT command syntax against the FTX-1's own
CAT reference/manual before hardcoding, then hardware-verify like every
other FTX-1 CAT command in this milestone (`FA`, `MD0C`, `TX1`/`TX0` were all
confirmed this way). Likely candidate is a Yaesu-style VFO/Memory select
command (naming varies by rig — `VM`, `MR`/`FR`, etc. in different Yaesu CAT
dialects) but the FTX-1's exact command needs to be looked up or probed on
real hardware, not assumed from other Yaesu models.

Where to add it: probably at the top of `ftx1_sync_frequency_mode()` (fires
on every Connect-driven sync, S->3 band change, and initial connect — see
`sync_radio_to_current_band()` callers in `main/main.cpp`), so it's
unconditionally correct rather than only fixing the Connect-button path
specifically.
