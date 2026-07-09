---
status: resolved
resolved: 2026-07-08
resolution_summary: |
  Root cause was FAT-level corruption on the internal 3MB storage partition, not application
  logic. Confirmed via physical inspection: entering MSC (USB Drive) mode and browsing the
  partition from a PC showed 0 bytes free out of 2.91MB, but an EMPTY directory listing --
  the classic signature of orphaned/lost clusters (FAT allocation table marks space used, but no
  directory entry points to it), most likely caused by an abrupt hard-reset (idf.py flash's
  RTS-pin reset) interrupting a filesystem write mid-operation during one of the session's many
  flash cycles. This explains both failure symptoms: storage_file_exists("Station.txt") returning
  false (root directory had no valid entry) and the write attempt failing silently at
  fopen/fwrite (no free space to allocate a new file despite an empty-looking directory).
  Fix: reformatted the partition via Windows (Format-Volume, FAT, while the device was in MSC
  mode exposing it as a USB drive) -- no firmware/code changes needed. Confirmed on hardware:
  next boot showed a clean Station.txt round-trip ("Internal Station.txt already present;
  skipping SD import", radio correctly loaded as FTX-1, no import/write failures), and the user
  confirmed a subsequent settings change saved cleanly with no error logged.
  Data loss: local QSO logs (.adi/.txt files) on that partition were already inaccessible in the
  corrupted state before the reformat (0 files visible via MSC despite 0 bytes free) -- nothing
  recoverable was lost beyond what the corruption had already destroyed.
trigger: |
  User: settings (radio selection, callsign, grid, bands, etc.) fall back to default on every
  reboot, confirmed persistent across multiple reboots, not a one-off. Radio reverts to QMX
  (the hardcoded default) instead of staying on FTX-1.
created: 2026-07-08
updated: 2026-07-08
---
# fatfs-station-txt-persistent-failure

## Symptoms

- Expected: Station.txt (radio selection, callsign, grid, band list, beacon/protocol settings,
  etc.) persists across reboots as it has all session — confirmed working correctly through many
  earlier reboots today (FTX-1 selection, RTC source, etc. all survived reflashes/reboots fine
  until this point).
- Actual: On every reboot now, `storage_file_exists("Station.txt")` returns false internally, the
  code falls through to the SD-import bootstrap path (which also fails), and the radio reverts to
  QMX (the hardcoded default when Station.txt can't be read). Confirmed persistent — user reports
  it happens on every reboot, not transient.
- Timeline: First observed after several consecutive `idf.py -p COM12 flash` cycles today (SNR
  display fix, manual slot-sync feature, QSO log combined-view feature — 3 flashes after the
  successful 5x QSO final test). Not observed as a problem during any earlier reboot today
  (multiple successful Station.txt round-trips confirmed via "Internal Station.txt already
  present; skipping SD import" log lines throughout the session, see Evidence).
- Reproduction: 100% — every reboot since first observed shows the same failure pattern.

## Evidence

- source: COM5 debug UART capture, live boot log
  timestamp: 2026-07-08T18:29:45
  content: |
    18:29:45.316 I (442) storage_service: MSC storage event=0 mount=0
    18:29:45.322 I (442) storage_service: MSC storage event=1 mount=1
    18:29:45.330 I (443) storage_service: initialized owner: firmware owns /storage on partition 'fatfs'
    18:29:45.336 I (451) sdspi_transaction: cmd=52, R1 response: command not supported
    18:29:45.340 I (455) main_task: Returned from app_main()
    18:29:45.372 I (496) sdspi_transaction: cmd=5, R1 response: command not supported
    18:29:45.568 I (693) storage_service: Failed to import Station.txt from SD
    18:29:45.576 I (694) BOARD_POWER: battery ADC init OK: unit=1 channel=9 GPIO10 ratio=2.0 cali=1
    18:29:45.798 I (919) RADIO_CTRL: Selected radio control backend=qmx
    18:29:45.805 I (919) FT8: Profile bind radio=QMX audio=qmx_uac control=qmx
    18:29:46.986 E (2112) FT8: Failed to write Station.txt
  interpretation: |
    "initialized owner: firmware owns /storage" confirms the firmware DOES hold storage ownership
    this boot (the MSC/firmware arbitration handshake succeeded) — so the failure is not an
    ownership-gate rejection. storage_sync_station_from_sd() (storage_service.cpp:1021) only
    reaches its SD-import branch when storage_file_exists(kStationFile) returns false
    (storage_service.cpp:1031-1038) — meaning the internal Station.txt genuinely isn't found this
    boot. The SD import itself then fails too (no valid backup on SD, or SD read failure — the
    "command not supported" cmd=52/cmd=5 lines just above are normal SDIO-vs-plain-SD-card probe
    responses, not fatal errors, so likely a red herring / unrelated to the actual failure).
    Radio falls back to QMX (canonical_radio_type default) since no config was loaded.
    The subsequent "Failed to write Station.txt" (main.cpp:4493-4494, inside save_station_data())
    means storage_file_write_atomic() -> write_atomic_locked() (storage_service.cpp:185-226) also
    failed. No "rename failed" ESP_LOGE line appears (that's the only failure point in
    write_atomic_locked with its own log line, at storage_service.cpp:221) — so the failure is
    happening earlier: either fopen(temp_path, "wb") returning NULL (line 201-204), or fwrite
    returning short / sync_file() failing (line 206-207), both of which fail silently in this
    function with no log output. This suggests either the internal FATFS partition itself is in a
    bad/corrupted state (mount "succeeds" per the storage_service init log but subsequent file
    operations fail), or it's full, or some other low-level FS issue.

- source: code reading, main/main.cpp:4440-4495 (save_station_data)
  interpretation: |
    Confirms the log tag: "Failed to write %s" with STATION_FILE fires only when
    storage_file_write_atomic() returns false AND storage_service_firmware_available() was true
    (the earlier silent-skip warning path, "Firmware does not own storage; station data save
    skipped", did NOT fire — we saw the ERROR variant, not that WARN). So firmware ownership of
    storage was confirmed true both at boot-time station load AND at this later write attempt.

## Hypotheses considered

- hypothesis: Internal FATFS partition corruption from an abrupt hard-reset (idf.py flash's RTS-pin
    hard reset) catching a write mid-flight at some point across today's many flash cycles,
    leaving the filesystem in a state where mount "succeeds" superficially but file
    lookup/creation fails.
  status: leading candidate, not yet confirmed — needs direct inspection of FATFS state (e.g. via
    the MSC/USB mass-storage exposure to a host PC, or added diagnostic logging around the mount
    call and a stat/statvfs of the partition) rather than inferring purely from these log lines.
- hypothesis: Partition genuinely full (QSO logs + RT transcripts + ADIF files accumulated across
    today's extensive testing, possibly filling the 3MB partition), causing fopen/fwrite to fail.
  status: plausible, cheap to check — would need either an on-device free-space log line (may not
    exist yet, worth adding) or checking file count/sizes via the MSC USB exposure from a PC.
- hypothesis: A race between the MSC (USB mass storage) ownership handshake and firmware's own
    early file access — despite the log showing "firmware owns storage" before the failed Station.txt
    operations, worth double-checking whether ownership could flip transiently during boot in a way
    not fully reflected by that one log line.
  status: unexplored, lower priority given the log's explicit ownership-confirmed line ordering.

## Current Focus

hypothesis: The wear-levelling (WL) layer mounts cleanly (wl_mount() succeeds on first try, no
  "internal storage reformatted after wear levelling mount failure" WARN in the boot log), so the
  existing repair path in mount_wear_levelling_with_repair() (storage_service.cpp:86-134) never
  triggers. But the FAT filesystem structures sitting on top of that WL layer (root directory,
  FAT table) are themselves corrupted — most likely from an abrupt idf.py hard-reset (RTS-pin
  reset) catching a FAT metadata write mid-flight during one of today's several flash cycles. This
  explains why BOTH storage_file_exists("Station.txt") (stat() against a corrupted/garbled root
  directory finds nothing) AND write_atomic_locked()'s fopen(temp_path, "wb") (can't allocate a
  new directory entry / cluster in a corrupted FAT chain) fail in the same boot, despite
  "firmware owns storage" being true throughout (confirmed: firmware_owns_storage_locked() is the
  only gate on both paths, and no ownership-flip code runs automatically at boot —entering MSC
  mode is only reachable via explicit user keypress at main.cpp:5075-5076, so the "race with MSC
  handshake" hypothesis is now downgraded — no code path calls
  storage_service_set_usb_drive_enabled() during boot).
  Corruption at the FAT layer (vs. WL layer) explains why the existing format_if_mount_failed
  auto-repair (storage_service.cpp:683) didn't kick in either: that only fires when the FAT mount
  call itself returns an error, not when it mounts "successfully" onto structurally-damaged
  directory/FAT-table data.
test: Use the firmware's own existing MSC (USB mass storage) export mode — press 'C' on the
  Cardputer to enter MSC mode (enter_msc_mode(), main.cpp:4594, triggered at main.cpp:5075-5076) —
  to expose the internal fatfs partition directly to the PC as a USB drive and inspect it with
  Windows Explorer. This is non-destructive (read-only inspection), requires no reflash, and
  directly answers corrupted-vs-full-vs-something-else far faster than adding new diagnostic log
  lines and reflashing (which would also require detaching the FTX-1 first).
expecting: If corrupted: Windows reports "You need to format the disk before you can use it" or
  shows garbled/missing files or wrong volume size. If genuinely full: drive mounts cleanly in
  Explorer but shows 0 bytes free with a large accumulation of QSO/.adi/RT*.txt files. If
  something else: drive mounts cleanly, shows expected files including Station.txt, with
  reasonable free space (would contradict the leading hypothesis entirely and require
  re-investigation).
next_action: |
  IMPORTANT: do NOT take any destructive action (reformat, erase-flash on the fatfs partition)
  without explicit user confirmation — this would also wipe local QSO logs (.adi/.txt files) the
  user has not yet confirmed they're willing to lose.
  CHECKPOINT ISSUED: waiting on user to boot the device, press 'C' to enter MSC/USB-drive mode,
  and report back what Windows Explorer shows (format prompt? file list? free space?).

## Eliminated

(none yet)
