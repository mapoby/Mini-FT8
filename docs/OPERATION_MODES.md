# Mini-FT8 Operation Modes (Concise)

This is a quick operator reference for mode keys:
`S R T B M N O Q D C F`

## Quick Mode Map

| Key | Mode | Purpose |
|---|---|---|
| `R` | RX | View decoded messages and tap one to start/continue QSO flow. |
| `T` | TX Queue | View and manage autoseq TX queue. |
| `B` | BAND | Edit per-band frequencies. |
| `S` | STATUS | Beacon, connect/sync, band step, tune, date/time. |
| `M` | MENU page 1 | Core station/operator settings. |
| `N` | MENU page 2 | Radio/input/comment settings. |
| `O` | MENU page 3 | Logging/active bands/RTC/copy-to-SD/retry settings. |
| `Q` | QSO | Browse QSO/log files and view entries. |
| `F` | Fetch Overlay | Browse files and dump selected file to BLE client. |
| `D` | Delete Files | Browse and delete files in SPIFFS. |
| `C` | Control | USB serial command mode. |

## Global Keys

- `R/T/B/S/Q/D/C`: toggle target mode; pressing same mode key again returns to `RX`.
- `M/N/O`: jump to MENU pages 1/2/3; pressing the same page key again on that page returns to `RX`.
- `F`: enter Fetch overlay from any mode.
- `F` again (while in Fetch): exit Fetch to `RX`.
- `` ` `` (backtick): global TX cancel in `RX`, `TX`, and `STATUS` when not editing.

## Universal Navigation (UI + BLE)

- `;` = previous page, `.` = next page (RX/TX/BAND/MENU/QSO/Fetch/Delete).
- BLE uses `U`/`V` for the same previous/next paging.
- Left/Right mapping: device `,` / `/`, BLE `Z` / `X`.
- `1..6` always target the currently visible row/slot for the active mode.
- In text-edit fields, edit keys temporarily override normal navigation.
- Left/Right is currently used in:
  - `Q` entry view: switch column set (Default/Alternate)
  - `S` Date/Time edit: move edit cursor
  - `S` tune adjust sub-edit: `-10` / `+10`
  - `N` page cursor-offset edit: `-10` / `+10`

## BLE Terminal Frame (Start from Waterfall)

Each BLE screen push is one framed block. Starting from the text-based waterfall:

1. Text waterfall header: `[                           ]` (27 bins inside brackets)
2. Separator above waterfall: `=============================` (29 `=` chars)
3. Separator below waterfall: `-----------------------------` (29 `-` chars)
4. 6 visible UI text lines (same lines currently shown on device page)
5. Line 7 meta/edit line:
   - normal: `[MODE uv]` (`u`/`v` show page-up/page-down availability; `-` means not available)
   - Fetch mode: `[Fetch uv]`
   - text edit mode: `[Edit <item>]`

Transport order on BLE is still: `=` line, waterfall line, `-` line, then lines 1..7.

Notes:
- Waterfall bins use `space`, `.`, `:`, `|` and are refreshed once per slot at second 12.
- BLE also emits standalone tick/event packets between frames:
  - countdown tokens: `|`, `4`, `8`, `12`, `:`, `.`, `o`
  - decode completion token: `[D:n]`

## Per-Mode Controls

### `R` (RX)

- `1..6` select decoded line to feed autoseq.

### `T` (TX Queue)

- `1` rotate queue to next same-parity entry.
- `2..6` drop queue item on current page.
- `E` encode/log pending TX text snapshot.

### `B` (BAND)

- `1..6` choose a band slot to edit.
- Edit mode: digits to type, Backspace delete, Enter save.

### `S` (STATUS)

- `1` cycle Beacon mode (applies after idle delay / on exit).
- `2` connect/sync action now (audio start + CAT sync path).
- `3` step to next active band (applies after idle delay / on exit).
- `4` tune toggle.
- `5` edit Date.
- `6` edit Time.
- Date/Time edit: Left/Right move cursor, digits edit, Enter save, `` ` `` cancel.

### `M / N / O` (MENU pages 1/2/3)

- Common edit behavior: Enter save, Backspace delete, `` ` `` cancel.

Page `M` (`menu_page=0`):
- `1` CQ Type cycle.
- `2` Send FreeText once.
- `3` Edit FreeText.
- `4` Edit Call.
- `5` Edit Grid.
- `6` Deep sleep.

Page `N` (`menu_page=1`):
- `1` Offset source cycle.
- `2` Edit fixed cursor offset.
- `3` Radio select (`QMX`/`KH1`).
- `4` Edit ignore list.
- `5` Edit comment.
- `6` BLE on/off.

Page `O` (`menu_page=2`):
- `1` RxTx log on/off.
- `2` SkipTX1 on/off.
- `3` Edit active bands list.
- `4` Edit RTC compensation.
- `5` Copy files to SD.
- `6` Edit max retry.

### `Q` (QSO)

File list view:
- `1..6` open selected file entries.

Entry view:
- Left/Right switch columns (Default/Alternate).
- `` ` `` back to file list.

### `F` (Fetch Overlay)

- `1..6` send selected file over BLE.
- BLE send only occurs when BLE is enabled and connected; otherwise selection is no-op.
- `` ` `` cancel Fetch and return to previous mode.
- `F` again exits Fetch to `RX`.

### `D` (Delete Files)

- `1..6` delete selected file immediately.

### `C` (Control)

- USB serial command mode for host commands.
- `C` toggles back to `RX`.
- Other mode keys can switch out of Control when no binary upload is in progress.
