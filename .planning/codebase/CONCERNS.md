# Codebase Concerns

**Analysis Date:** 2026-07-04

## Tech Debt

**Monolithic Main Application File:**
- Issue: `main/main.cpp` is 5643 lines — contains UI, state management, audio pipeline, radio control, logging, and storage service all in one file
- Files: `main/main.cpp`
- Impact: Extremely difficult to test, maintain, and debug. High cognitive load for new contributors. Risk of unintended side effects when modifying any feature.
- Fix approach: Refactor into logical modules (UI layer, state machine layer, IO layer) with clear separation of concerns. Extract UI mode handlers into separate files. Move hashtable logic to its own module.

**Excessive Global State:**
- Issue: 896 references to global variables (prefixed with `g_`) scattered throughout `main/main.cpp`. Includes UI state, configuration, autoseq state, audio pipeline state, and radio control state — all mutable module-level state.
- Files: `main/main.cpp`, `main/core_api.cpp`
- Impact: Very difficult to reason about program behavior. Thread safety concerns. Makes testing and refactoring extremely risky. Multiple modules can modify same global variables with no clear ownership.
- Fix approach: Encapsulate related globals into structs. Create clear state ownership boundaries. Use accessor functions instead of direct access. Consider state machine pattern for main application flow.

## Known Bugs

**Buffer Overflow Risk in Callsign Lookup:**
- Symptoms: If a callsign longer than 11 characters is processed, it could overflow the `callsign_hashtable[].callsign` field which is only 12 bytes
- Files: `main/main.cpp:294`
- Trigger: Receiving an FT8 message with a malformed callsign or processing invalid input
- Impact: Stack corruption, crash, or potential code execution
- Current mitigation: `strncpy()` is used in insertion (line 242, 256) but `strcpy()` is used in lookup (line 294) without bounds checking
- Fix: Replace `strcpy()` with `strncpy()` and ensure null termination on line 294: `strncpy(callsign, callsign_hashtable[scan_idx].callsign, 11); callsign[11] = '\0';`

**Hash Table Linear Probing Wraparound:**
- Symptoms: When hashtable reaches capacity during linear probing, the probe can wrap but doesn't correctly detect full table
- Files: `main/main.cpp:212-252` (hashtable_add function)
- Trigger: High load with callsign collisions, combined with active aging/eviction
- Impact: Callsigns can be silently dropped or duplicates created. Messages may not be decoded properly if callsign lookup fails.
- Workaround: Hashtable size is relatively small (128 entries) and FM8 typically doesn't see >50 unique calls in a session
- Fix: Review linear probing logic and consider sentinel slot or explicit full-table detection

**Decode.c Hash Collision Risk:**
- Symptoms: CRC value (16 bits) is reused as message hash but only 14 bits are used (line 366)
- Files: `components/ft8_lib/ft8/decode.c:366`
- Impact: Potential message collision/confusion with 2 bits of information loss
- Current mitigation: CRC is reasonable but using full 16 bits would be safer
- Fix: Change comment to clarify 14-bit usage or extend to full 16/32-bit hash

## Security Considerations

**Unsafe String Parsing:**
- Risk: Multiple `sscanf()` calls without full validation of return codes or ranges
- Files: `main/main.cpp:1899, 1900, 2004, 2062, 2244-2245, 3978, 3994, 4228, 4260-4265, 4269-4281`
- Current mitigation: Some return codes are checked, but not all edge cases validated
- Recommendations: Create safe wrapper function for `sscanf()` that validates all fields. Add range checks for dates/times after parsing. Use structured parsing instead of scanf where possible.

**No Input Validation for Station Configuration:**
- Risk: User-supplied callsign, grid, and comment fields are not validated for length or special characters before storage/use
- Files: `main/main.cpp` (configuration load/save)
- Current mitigation: Limited by UI field widths, but no runtime bounds checking
- Recommendations: Add explicit length limits and character set validation for all user inputs. Sanitize before writing to ADIF/Cabrillo files.

**File System Assumptions:**
- Risk: Code assumes FATFS mount succeeds and doesn't validate filesystem state before writes
- Files: `main/main.cpp` (storage operations throughout)
- Current mitigation: Some logging of heap state exists but no preemptive filesystem check
- Recommendations: Add filesystem health check before critical writes. Implement graceful degradation if SD card is removed during operation.

## Performance Bottlenecks

**Hashtable Linear Probing on Full Capacity:**
- Problem: When hashtable fills, `hashtable_add()` calls `hashtable_trim_size()` which scans entire table to find oldest entries
- Files: `main/main.cpp:212-220`
- Cause: O(n) scanning on every insert when near capacity
- Observation: Trimming removes 50 entries at a time, so amortized cost is acceptable, but worst-case latency spike possible
- Improvement path: Consider lazy eviction or background cleanup. Profile actual call patterns during long sessions.

**Vector Reallocation in autoseq Queue:**
- Problem: If `autoseq.cpp` uses std::vector for the queue and inserts/removes cause reallocations, real-time performance degrades
- Files: `main/autoseq.cpp:22` (fixed-size array used; actually OK)
- Current mitigation: Queue is fixed-size array (AUTOSEQ_MAX_QUEUE=30), not vector — no reallocation risk
- Note: Good design choice for embedded system

**String Concatenation in TX Message Building:**
- Problem: Multiple std::string operations in `autoseq.cpp` when building TX messages
- Files: `main/autoseq.cpp` (format_tx_text, generate_cq_text_into)
- Impact: Memory allocation on every TX slot (15s interval), potential heap fragmentation over long sessions
- Improvement path: Pre-allocate message buffer or use stack-based string building for known-size messages

## Fragile Areas

**Autoseq State Machine:**
- Files: `main/autoseq.cpp`, `main/autoseq.h`
- Why fragile: Complex state transitions between CALLING, REPLYING, REPORT, ROGER_REPORT, ROGERS, SIGNOFF. Next_tx type constraints (TX_NONE semantics) are subtle and documented in comments but not enforced by type system. CQ one-shot semantics vs persistent QSO contexts easy to get wrong.
- Safe modification: Any change to QsoContext or TxMsgType enum requires reviewing all state transition logic. Document invariants in code. Add assertions for invalid state transitions.
- Test coverage: Limited test coverage; mostly manual testing through QSO sequences. No regression tests for state machine.

**Audio Pipeline Integration:**
- Files: `main/ft8_audio_pipeline.cpp`, `main/stream_uac.cpp`, `main/stream_mic.cpp`, `main/dds_q15.cpp`
- Why fragile: Multiple input sources (UAC, microphone, DDS testgen) feeding into shared DSP pipeline with synchronization via portMUX spinlocks. Timing-critical audio processing with real-time constraints.
- Safe modification: Changes to sample rates, block sizes, or timing require careful re-testing with actual radios. Document audio clock assumptions. Test with both UAC and microphone inputs.
- Test coverage: Integration tests in `tests/tx_e2e/` verify message format but not audio chain end-to-end

**UI Mode Switching:**
- Files: `main/main.cpp:348-5000+` (UI modes and event handlers)
- Why fragile: Global `ui_mode` variable controls rendering and input handling. Mode-specific state (e.g., `status_edit_idx`, `band_page`, `debug_page`) scattered as static variables. Transitioning between modes without proper state cleanup risks memory leaks or stale data.
- Safe modification: When adding/removing UI modes, update `enter_mode()` state reset logic. Verify all mode-specific state variables are initialized when entering that mode.
- Test coverage: No automated UI testing; manual only

**Radio Control CAT Protocol:**
- Files: `main/radio_control_kh1_cat.cpp`, `main/radio_control_qmx.cpp`, `main/radio_control_qdx.cpp`
- Why fragile: Each radio has different CAT protocol (KH1 UART, QMX/QDX serial). Synchronization between local frequency tracking and actual radio frequency. Retrying on timeout without clear backoff strategy.
- Safe modification: Changes to any CAT protocol must be tested with actual hardware. Document expected response times and retry limits. Add protocol validation/checksums where possible.
- Test coverage: No unit tests; host_mock/ directory suggests some local testing infrastructure exists but status unknown

## Scaling Limits

**Callsign Hashtable Size:**
- Current capacity: 128 entries × 12-byte callsigns = ~1.5 KB memory
- Limit: Designed for ~50 unique callsigns in typical session. At 128 entries with linear probing, collision rate becomes high
- Actual capacity before performance degrades: ~80-90 unique callsigns (70% load factor rule of thumb)
- Scaling path: Increase CALLSIGN_HASHTABLE_SIZE constant (line 135) and profile memory impact. Consider chained hashing if very high volume expected. Current design adequate for portable QRP operation.

**Autoseq Queue Depth:**
- Current capacity: 30 total QSOs (active + inactive)
- Limit: Fixed array; cannot grow beyond 30
- Impact: During contest or very active band (40+ simultaneous QSO contexts), newer contacts are rejected
- Scaling path: Increase AUTOSEQ_MAX_QUEUE (main/autoseq.h:10) but monitor heap impact. Current 30 is generous for typical portable operation.

**FATFS Partition:**
- Current capacity: 3 MB dedicated to FATFS as described in CMakeLists.txt
- Limit: ~30,000+ QSO entries at ~100 bytes per ADIF record. Logs accumulate without automatic purging.
- Impact: After ~1 year of portable operation, storage fills and SD write commands fail
- Scaling path: Implement log rotation (archive old months) or remove old entries. Add pre-write filesystem capacity check.

**Heap Memory on ESP32-S3:**
- Current observation: Code monitors heap in `print_heap_usage()` (line 692) with multiple cap types (8bit, internal, DMA)
- Limit: ESP32-S3 has ~8 MB total, shared between application, WiFi, BLE, storage cache
- Risk area: std::string usage throughout codebase can fragment heap. FATFS operations allocate temporaries.
- Improvement: Profile long-running sessions (4+ hours) to detect heap drift. Consider reducing default vector/string sizes or using memory pools for frequently-allocated types.

## Dependencies at Risk

**FT8 Library (ft8_lib) TODOs:**
- Files: `components/ft8_lib/ft8/message.c`, `components/ft8_lib/ft8/decode.c`
- Risk: Multiple unresolved TODOs in message encoding/decoding:
  - Line 312: `TODO: check for suffixes` — /P, /R suffixes may not be encoded correctly in all contexts
  - Line 677: `TODO set byte 9? or must the caller do it?` — Frame structure unclear
  - Line 871: `TODO join fields via whitespace` — Field parsing may drop data
  - Line 1581: `TODO: Check for "R " prefix before a 4 letter grid` — Grid parsing incomplete
  - Line 1594: `TODO: check the range of dd` — Grid coordinate validation missing
  - Line 261, 287 in decode.c: `TODO: replace magic numbers with constants` — Code maintainability risk
- Impact: Edge cases in non-standard message formats may fail silently or produce incorrect decodes
- Migration plan: Profile real-world message patterns to identify unhandled cases. Upstream improvements to ft8_lib or implement local workarounds with regression tests.

**M5Stack Components (M5Unified, M5GFX) TODOs:**
- Files: Multiple in `components/M5Unified/`, `components/M5GFX/`
- Risk: Third-party library with many unresolved TODOs (power management, touch handling, display driver quirks)
- Observation: Component tests exist but not integrated into Mini-FT8 application tests
- Impact: Updates to these libraries may break device initialization or UI responsiveness
- Recommendation: Pin specific versions. Test each upgrade on actual hardware before deploying.

## Missing Critical Features

**No Logging to Remote Server:**
- Problem: All logs are local-only (FATFS or ADIF). No cloud sync or remote backup.
- Blocks: Automated spot posting, remote SWL monitoring, loss of QSO data if device lost/damaged
- Workaround: Manual copy-to-SD and transfer via USB-C

**No TX Schedule Persistence Across Reboots:**
- Problem: Unsent TX queue is cleared on reboot. If device crashes during TX cycle, pending messages are lost.
- Blocks: Reliable auto-sequencing for multi-hour events (POTA, Field Day)
- Workaround: Auto-retry logic (g_autoseq_max_retry) attempts resend if DX replies again

**No Over-The-Air Firmware Updates:**
- Problem: Firmware updates require USB cable and idf.py/esptool
- Blocks: Field deployments at remote POTA sites without laptop
- Current mitigation: Pre-build multiple variants (FT4 ON/OFF, radio profiles) to avoid in-field flashing needs

**No Dual-Stack (FT8+FT4 simultaneous operation):**
- Problem: Runtime protocol selection (FT8 XOR FT4) requires reboot to apply change
- Blocks: Quick protocol A/B testing, split-band operation
- Current design: Compile-time flag (ENABLE_FT4) and per-boot selection (Station.txt) intentional to save memory

## Test Coverage Gaps

**No Unit Tests for Main Application Logic:**
- What's not tested: UI mode transitions, station configuration load/save, ADIF/Cabrillo logging, RTC time synchronization
- Files: `main/main.cpp`, `main/core_api.cpp` — no corresponding *.test.cpp files
- Risk: Regressions in configuration handling (e.g., band selections, date/time) only caught during manual testing
- Priority: HIGH — configuration bugs break reproducibility of QSOs

**No Integration Tests for Autoseq State Machine:**
- What's not tested: QSO flow (CQ → TX1 → RX → TX2 → RX → etc.), retry logic, inactive zone reactivation, FreeText injection, Field Day cabrillo generation
- Files: `main/autoseq.cpp` — supporting doc (docs/AUTOSEQ_ARCHITECTURE.md) is detailed but code has no test suite
- Risk: State machine bugs discovered only during live QSO sessions; hard to reproduce
- Priority: HIGH — core of application reliability

**No E2E Tests for Radio Control CAT Protocols:**
- What's not tested: Frequency changes, TX/RX control, timeout/retry on unresponsive radios, multi-radio failover
- Files: `main/radio_control*.cpp` — would need actual hardware mocks or hardware CI
- Risk: Radio control failures silent until operator notices no TX
- Priority: MEDIUM — hardware-dependent; host_mock/ directory suggests framework exists but not integrated

**No Memory Leak Tests:**
- What's not tested: Long-running sessions (4+ hour POTA activation), repeated TX/RX cycles, storage operations
- Files: All (heap fragmentation risk)
- Risk: Device becomes unresponsive after 2-3 hours due to memory exhaustion
- Recommendation: Create stress test harness for long-duration operation (e.g., 8-hour simulation)
- Priority: MEDIUM — requires custom test infrastructure

**Minimal Protocol Encoding Tests:**
- What exists: `tests/tx_e2e/test_ta_format.cpp` verifies tone array format
- What's missing: End-to-end message encoding for all message types (CQ, TX1-TX5, non-standard calls), grid/report edge cases, FD/ARRL exchange formats
- Files: `components/ft8_lib/ft8/` — library exists but integration tests with autoseq minimal
- Priority: MEDIUM — edge cases may only appear during contests

---

*Concerns audit: 2026-07-04*
