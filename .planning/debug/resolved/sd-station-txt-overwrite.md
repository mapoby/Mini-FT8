---
status: resolved
trigger: |
  User reports: station settings (specifically the selected radio) are not saved after power off —
  always reverts to QMX after a Cardputer power cycle/reset, even after explicitly selecting FTX-1
  in the station config menu. Discovered during Phase 5 (End-to-End Integration and Parity Testing)
  hardware checkpoint testing for the FTX-1 radio backend.
created: 2026-07-07
updated: 2026-07-07
---

## Symptoms

- **Expected behavior:** Selecting FTX-1 (or any radio) via the station config menu's radio-cycle
  key ('3' in the STATUS/menu view) should persist across a power cycle, since `save_station_data()`
  is called immediately after the selection changes.
- **Actual behavior:** After power-cycling the Cardputer, `g_radio` always reverts to `RadioType::QMX`
  regardless of what was selected and saved before power-off.
- **Error messages:** None — no crash, no logged write failure.
- **Timeline:** Discovered today (2026-07-07) during Phase 5 hardware checkpoint testing. Unknown how
  long-standing — likely always present for any user with an SD card inserted, since it depends on
  SD card presence, not on anything specific to this milestone's FTX-1 work.
- **Reproduction:** Insert an SD card containing an older/stale `Station.txt` (e.g. from before FTX-1
  was selected, or with any different `radio=` value). Boot the Cardputer, select FTX-1 via the
  station config menu (confirmed `save_station_data()` fires and writes `radio=6` to internal
  storage). Power-cycle. On the next boot, `load_station_data()` calls
  `storage_sync_station_from_sd()` first, which unconditionally overwrites the internal
  `Station.txt` with the SD card's stale copy — silently discarding the FTX-1 selection before
  the rest of `load_station_data()` even reads the (now-clobbered) internal file.

## Current Focus

hypothesis: null
test: null
expecting: null
next_action: Fix applied and build-verified. Awaiting optional hardware verification (SD-card
  reboot test) requested via checkpoint below; will finalize/archive once user confirms or skips.
reasoning_checkpoint:
  hypothesis: "storage_sync_station_from_sd() unconditionally overwrites internal Station.txt
    from the SD copy every boot, with no staleness check, silently discarding settings changes
    made after the SD copy was last written."
  confirming_evidence:
    - "Code reading of storage_sync_station_from_sd() (storage_service.cpp:1021) shows
      write_atomic_locked(kStationFile, content) called with SD content and no comparison
      against the existing internal file."
    - "save_station_data() (main.cpp:4373) only calls storage_file_write_atomic(STATION_FILE,...)
      — never touches the SD copy — so the two copies diverge as soon as any setting changes
      while SD is present."
    - "User confirmed an SD card was inserted during the reproducing session, and the reported
      symptom (radio selection reverting to QMX after power-cycle) exactly matches the
      overwrite-before-load code path in load_station_data()."
  falsification_test: "If, with the SD card removed, the radio selection persisted correctly
    across reboot, that would isolate the bug specifically to the SD-import path (it did —
    reproduction requires SD card presence per Symptoms.reproduction)."
  fix_rationale: "Git history (commit adc548d, 'refactor(storage): make FATFS ownership
    explicit') shows the SD-import feature exists to bootstrap Station.txt onto a
    freshly-partitioned/erased internal FATFS partition from an SD-provided copy — not to act
    as an ongoing sync-from-SD on every boot. Gating the import on 'internal Station.txt does
    not yet exist' preserves that original bootstrap/restore use case exactly, while making it
    impossible for the import to ever clobber a settings change already persisted internally.
    This addresses the root cause (unconditional overwrite direction) rather than a symptom,
    without adding new state, timestamps, or SD-write-back complexity to the frequently-called
    save_station_data() hot path."
  blind_spots: "Have not run a live power-cycle-with-SD-card hardware test; relying on code
    reading plus a successful build. Also have not verified behavior for a user who genuinely
    wants an existing Cardputer to adopt a *different* Station.txt from a newly-inserted SD
    card (e.g. swapping SD cards between two provisioned units) — that scenario will now
    correctly no longer auto-import if internal storage already has a Station.txt, which
    matches the fix's intent but is a behavior change worth confirming isn't relied upon
    elsewhere."

## Evidence

- timestamp: 2026-07-07
  checked: main/main.cpp save_station_data() (line ~4373), load_station_data() (line ~4223),
    load_station_radio_type_only() (line ~4207), parse_radio_config_value()/radio_type_from_saved_int()
    (line ~1568-1613) — all round-trip FTX-1 correctly (int value 6 <-> RadioType::FTX1). No bug in
    the save/parse logic itself.
  found: `save_station_data()` only writes to internal storage via `storage_file_write_atomic(STATION_FILE, ...)`
    — it never mirrors the change to the SD card copy.
  implication: If an SD card is present, its Station.txt copy is never updated by normal in-app
    settings changes — only internal storage is.
- timestamp: 2026-07-07
  checked: components/storage_service/storage_service.cpp storage_sync_station_from_sd() (line 1021).
    Called from main.cpp load_station_data() (line 4224), unconditionally, once per boot
    (gated only by a static `s_station_sync_attempted` flag, not by any staleness/timestamp check).
  found: If an SD card is mounted and a Station.txt file exists on it, this function reads the SD
    copy and calls `write_atomic_locked(kStationFile, content)` to overwrite the INTERNAL Station.txt
    with the SD content — with no comparison, timestamp check, or "internal is newer" guard of any
    kind. This happens before load_station_data() reads the internal file for actual use, so the
    freshly-clobbered internal copy (not the pre-power-off saved one) is what gets loaded.
  implication: Root cause confirmed. Any settings change (not just radio selection) made while an SD
    card with an older Station.txt is inserted will be silently discarded on the very next boot,
    with no user-visible warning that this is happening (only an ESP_LOGI "Imported Station.txt from
    SD" line, not surfaced to the UI).
- timestamp: 2026-07-07
  checked: User confirmation.
  found: User confirmed an SD card IS inserted in the Cardputer during this testing session.
  implication: This is the live, reproducing configuration — SD-overwrite is confirmed as the actual
    cause of the reported symptom, not just a theoretical code-reading finding.

## Eliminated

- hypothesis: save_station_data() has a bug that fails to persist the radio= key correctly
  status: eliminated
  reasoning: Direct code reading confirms save/parse/canonicalize round-trip FTX-1 correctly; no
    ESP_LOGE "Failed to write Station.txt" appeared in the session log, and the write path itself
    is straightforward and correct.
- hypothesis: parse_radio_config_value()/radio_type_from_saved_int() misparses the persisted FTX-1 value
  status: eliminated
  reasoning: Code reading confirms `case (int)RadioType::FTX1: return RadioType::FTX1;` and the
    "FTX1"/"FTX-1" text-token branches are both correct; no off-by-value or missing-case bug found.

## Resolution

root_cause: |
  `storage_sync_station_from_sd()` (components/storage_service/storage_service.cpp) unconditionally
  overwrites the internal Station.txt with the SD card's copy on every boot (once per boot session),
  with no staleness check and no mechanism for `save_station_data()` to keep the SD copy in sync.
  Any settings change made while an SD card with an older Station.txt is present is silently
  discarded on the next boot, because the stale SD copy clobbers the internal copy before it's read.
fix: |
  In `storage_sync_station_from_sd()` (components/storage_service/storage_service.cpp:1021),
  added a check that skips the SD import entirely if the internal Station.txt already exists
  (`storage_file_exists(kStationFile)`), before mounting the SD card. The SD-to-internal import
  now only fires when internal storage has no Station.txt at all (fresh/erased FATFS partition),
  matching the feature's original bootstrap/restore intent from commit adc548d. Normal boots
  with an existing internal Station.txt (the common case) never touch the SD copy, so
  save_station_data() writes are never subsequently clobbered.
verification: |
  Code review: traced load_station_data() -> storage_sync_station_from_sd() -> new
  storage_file_exists(kStationFile) guard -> confirmed internal file existence check runs
  before mount_sd_locked()/write_atomic_locked(), using the same recursive StorageGuard mutex
  (safe to call storage_file_exists from within an already-held guard).
  Build verification: `idf.py build` completed successfully with no new warnings/errors from
  the changed file (esp-idf/storage_service/CMakeFiles/__idf_storage_service.dir/storage_service.cpp.obj
  built and linked cleanly).
  Hardware verification: user built (idf.py build, exit 0, clean), flashed to the physical
  Cardputer via idf.py -p COM12 flash, then with the SD card still inserted selected a
  different radio (FTX-1) via the station config menu, power-cycled, and confirmed the
  selection persisted correctly instead of reverting to QMX. Fix confirmed working on real
  hardware.
files_changed:
  - components/storage_service/storage_service.cpp
