---
status: complete
---

Replace the QSO log's two separate views (time/band/call and call/R-SNR/S-SNR,
toggled via `,`/`/`) with a single combined line: `DD/MM HH:MM CALL R-SNR
S-SNR`.

## Changes

`main/main.cpp`:
- `QsoLogEntry` gains a `date_on` field (DD/MM).
- `qso_load_entries()` parses ADIF's `QSO_DATE` field (YYYYMMDD) into
  `date_on`; falls back to `--/--` if missing/malformed (avoided `??/??`
  which the compiler misreads as a `??/` trigraph under `-Werror=trigraphs`).
- `qso_rebuild_entry_lines()` now always builds one combined line
  (`date_on + " " + time_on + " " + call(padded 10) + rcvd(%+03d) + " " +
  sent(S%+03d)`) instead of branching on `g_q_page_view`. The `,`/`/` keys
  and `QPageView` enum are left in place (harmless no-ops now) to minimize
  the diff.

`components/ui/ui.cpp` + `components/ui/include/ui.h`:
- `ui_draw_debug()` gained an optional `text_size` parameter (default 2,
  preserving existing callers). The QSO log entries view now passes size 1 —
  the combined line (date+time+call+both SNR) doesn't fit the ~20-char
  budget at size 2 on the 240px display.
