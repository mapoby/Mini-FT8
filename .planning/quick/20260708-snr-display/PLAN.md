---
status: complete
---

Show signal reports (SNR) more prominently: default the QSO log's alternate
call/R-SNR/S-SNR view instead of requiring a keypress, and surface SNR
directly on the live RX decode list, which computed it but never displayed it.

## Changes

1. `main/main.cpp`: `g_q_page_view` now initializes to `QPageView::Alternate`
   instead of `QPageView::Default`, and the file-switch reset in the QSO log
   view (`UIMode::QSO` entries list) now resets to `Alternate` too. The
   time/band/call view is still reachable via `,` (comma).
2. `components/ui/ui.cpp`: `draw_rx_line()` now draws a compact, right-aligned
   SNR badge (`%+d`, small font size 1) on each RX list row. The value was
   already carried on `RxDecodeEntry::snr` (populated from the FT8 decoder's
   per-candidate SNR) but never rendered.
