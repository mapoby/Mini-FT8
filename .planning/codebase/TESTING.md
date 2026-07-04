# Testing Patterns

**Analysis Date:** 2026-07-04

## Test Framework

**Runner:**
- CMake with CTest integration
- Standalone C++ executables (no external test framework like Google Test)
- Manual test invocation with raw output assertions

**Assertion Library:**
- None; tests use:
  - Direct output comparison (string equality)
  - SNR/timing validation against thresholds
  - Custom helper validation functions (e.g., `verify_event_timing()`)

**Run Commands:**
```bash
# Build all tests
cd tests/tx_e2e
mkdir build
cd build
cmake ..
cmake --build .

# Run specific test
ctest -N                      # List available tests
ctest -V -R l1_encoder        # Run L1 encoder test with verbose output
./test_l1                     # Run directly without ctest

# Run host_mock tests
cd host_mock
make                          # Builds host_test.exe
./host_test test_qso.json     # Run JSON-driven scenario test
./host_test test_beacon.json  # Different scenario
```

## Test File Organization

**Location:**
- `tests/tx_e2e/` - Audio/encoding tests (executable on host system)
- `host_mock/` - State machine tests (JSON scenarios)
- `test_apps/` - ESP-IDF based tests (run on device, mentioned in CMakeLists.txt)

**Naming:**
- Test file: `test_*.cpp` (e.g., `test_l1_encoder.cpp`, `test_l2_state_machine.cpp`)
- Helper file: `*_helper.cpp`
- Golden data: `.json` or `.wav` files

**Structure:**
```
tests/
├── tx_e2e/
│   ├── CMakeLists.txt           # Defines test executables
│   ├── test_l1_encoder.cpp      # L1 test
│   ├── test_l2_state_machine.cpp # L2 test
│   ├── test_l3_tx_poll_timing.cpp
│   ├── test_l4_tx_timer_isolation.cpp
│   ├── test_golden_rx.cpp       # Golden WAV tests
│   ├── test_kh1_tone_map.cpp    # Tone mapping
│   ├── test_ta_format.cpp       # Format validation
│   ├── decode_helper.cpp        # Shared test utilities
│   ├── decode_helper.h
│   ├── synth.c/.h               # Audio synthesis
│   └── golden/                  # Reference WAV files
│       ├── MANIFEST.txt
│       ├── ft8_cq_w1xyz_fn42.wav
│       └── ...
host_mock/
├── Makefile
├── host_main.cpp
├── host_mocks.cpp/.h
├── json_parser.cpp/.h
├── test_*.json                  # Test scenarios
└── host_test.exe                # Compiled executable
```

## Test Structure

**Suite Organization:**
```cpp
// test_l1_encoder.cpp pattern
struct TestCase {
    const char* text;
    ftx_protocol_t proto;
    float base_hz;
};

static const TestCase CASES[] = {
    {"CQ W1XYZ FN42",       FTX_PROTOCOL_FT8, 1500.0f},
    {"CQ W1XYZ FN42",       FTX_PROTOCOL_FT4, 1500.0f},
    // ... more cases
};

int main() {
    int passed = 0, failed = 0;
    for (int i = 0; i < NUM_CASES; i++) {
        // Test one case
        if (success) {
            passed++;
        } else {
            failed++;
        }
    }
    printf("Results: %d/%d passed\n", passed, passed + failed);
    return (failed > 0) ? 1 : 0;
}
```

**Patterns:**
- Test cases in static array of structs
- Loop over CASES array, run each, print result
- Pass/fail determined by assertions and output checks
- Exit code: 0 = all passed, 1 = any failed

## Mocking

**Framework:** Manual mocks (no mock library)

**Patterns:**

**Host Mocks (for autoseq testing):**
- File: `host_mock/host_mocks.cpp`
- Implements ESP-IDF stubs: `esp_timer_get_time()`, `ESP_LOGI()`, etc.
- Allows autoseq.cpp to compile and run on host (non-embedded)

**Decode Mocks (for audio tests):**
- File: `tests/tx_e2e/decode_helper.cpp`
- Wraps ft8_lib decode functions
- Provides hash table management for test consistency
- Example (test_l1_encoder.cpp:73-75):
  ```cpp
  decode_clear_hashes();  // Clear hashes from previous tests
  ftx_message_t msg;
  ftx_message_rc_t rc = ftx_message_encode(&msg, decode_get_hash_if(), c.text);
  ```

**What to Mock:**
- ESP-IDF platform functions (timer, logging)
- File I/O (when reading test scenarios)
- Hardware (radio control, GPIO)

**What NOT to Mock:**
- Core FT8 encode/decode (test the real thing)
- State machine logic (verify actual behavior)
- Hash table operations (core to correctness)

## Fixtures and Factories

**Test Data:**

**Audio tests (test_l1_encoder.cpp, test_l2_state_machine.cpp):**
- Golden WAV files in `tests/tx_e2e/golden/` directory
- MANIFEST.txt lists expected checksums
- Tests can load and verify against golden files
- On failure, test writes WAV to disk for offline inspection

**Host mock tests (host_mock/):**
- JSON scenario files define test inputs
- Format: station config + periods of events (decoded messages, timers)
- Example (test_qso.json):
  ```json
  {
    "config": {
      "my_callsign": "W1ABC",
      "my_grid": "FN42",
      "tx_on_even": true,
      "max_retry": 5
    },
    "periods": [
      {
        "parity": 0,
        "to_me": [...],
        "timer_ms": 15000
      },
      ...
    ]
  }
  ```

**Location:**
- Golden WAVs: `tests/tx_e2e/golden/`
- Test scenarios: `host_mock/test_*.json`
- Helper code: `tests/tx_e2e/decode_helper.cpp`, `host_mock/json_parser.cpp`

**Factory Pattern:**
- `decode_helper.h` provides factory functions:
  - `decode_clear_hashes()` - Reset state for new test
  - `decode_pcm()` - Synthesize and decode audio
  - `write_wav()` - Write failure trace to disk

## Coverage

**Requirements:** No explicit coverage target enforced

**View Coverage:**
- Not configured (no GCOV or lcov integration)
- Manual inspection of code paths possible via test matrix

**Test Coverage Assessment (from CMakeLists.txt):**
- L1 tests (encoder): Message types × Protocols × Frequencies
- L2 tests (state machine): State transitions × Delays
- L3 tests (polling): Timing edge cases
- L4 tests (timer): Isolation between cores
- Golden RX: Against real broadcast WAVs
- Component tests: Tone mapping, format parsing

## Test Types

**Unit Tests:**
- **L1 Encoder:** Message text → FT8/FT4 tones → audio synthesis → decode round-trip
  - Verifies encode/decode correctness
  - Tests all message types and protocols
  - Detects SNR degradation
  - File: `tests/tx_e2e/test_l1_encoder.cpp`
  
- **KH1 Tone Mapping:** Validates frequency offset mapping for KH1 radio
  - File: `tests/tx_e2e/test_kh1_tone_map.cpp`
  
- **Format Validation:** TA command format parser
  - File: `tests/tx_e2e/test_ta_format.cpp`

**Integration Tests:**
- **L2 State Machine:** TX state transitions + timing + encode/decode
  - Verifies autoseq timer accuracy during TX
  - Tests timing with various loop delays
  - File: `tests/tx_e2e/test_l2_state_machine.cpp`
  
- **L3 TX Poll Timing:** Tests TX scheduler meets tight timing bounds
  - File: `tests/tx_e2e/test_l3_tx_poll_timing.cpp`
  
- **L4 TX Timer Isolation:** Verifies core 0/core 1 timer doesn't drift
  - File: `tests/tx_e2e/test_l4_tx_timer_isolation.cpp`
  
- **Golden RX:** Decode against reference broadcast recordings
  - File: `tests/tx_e2e/test_golden_rx.cpp`

**Behavioral Tests (host_mock):**
- **QSO Sequences:** Full CQ → reply → report exchange
  - File: `host_mock/test_qso.json`
  - Runs autoseq through state transitions
  - Verifies ADIF logging, message sequencing
  
- **Beacon Modes:** Even/odd slot pacing
  - File: `host_mock/test_beacon.json`
  
- **Field Day:** FD exchange parsing and Cabrillo logging
  - File: `host_mock/test_fd_*.json`
  
- **Deadlock/Reincarnation:** Edge cases in queue management
  - File: `host_mock/test_*_deadlock.json`, `test_*_reincarnation.json`

**E2E (End-to-End):**
- Currently run on device via `idf.py` and UART capture
- Not part of automated test suite (manual validation)

## Common Patterns

**Async Testing:**
- No async patterns in tests
- Timing verified through event trace analysis
- Example (test_l2_state_machine.cpp:40-41):
  ```cpp
  std::string timing_err = verify_event_timing(events, c.cfg, sym_ms, c.loop_delay_ms);
  if (!timing_err.empty()) return false;
  ```

**Error Testing:**
- Negative test cases included in test matrices
- Examples:
  - Invalid callsigns (non-FT8 compliant)
  - Out-of-range frequencies
  - State transitions from IDLE (should be rejected)
  
- Test case expectations:
  - Some return `false` (rejected)
  - Some return specific error codes
  - Some trigger log messages (verified via stderr capture)

**Failure Trace Collection:**
- On decode failure, test writes WAV to disk
  - Example (test_l1_encoder.cpp:114-117):
    ```cpp
    char wav_path[256];
    snprintf(wav_path, sizeof(wav_path), "fail_l1_%02d.wav", i + 1);
    if (write_wav(wav_path, pcm.data(), total_samples, SAMPLE_RATE) == 0)
        printf("FAIL (no decode) — WAV written to %s\n", wav_path);
    ```
  - Enables offline analysis (FFT, spectrogram, etc.)

**Test Data Normalization:**
- Callsigns normalized to uppercase before comparison
  - Function: `normalize_text()` (decode_helper.cpp)
  - Handles grid/report variations

**SNR Verification:**
- Tests report SNR of successful decode
  - Example (test_l1_encoder.cpp:125): `printf("PASS (SNR=%.1f)\n", decode_result.snr);`
  - No hard SNR requirement, just informational

## Test Execution

**Build (CMake):**
```bash
cd tests/tx_e2e
mkdir -p build
cd build
cmake ..           # Detects C/C++ compiler, validates dependencies
cmake --build .    # Compiles test executables
```

**Run Individual Test:**
```bash
./test_l1         # Runs all L1 encoder test cases
./test_l2         # Runs all L2 state machine cases
./test_golden_rx  # Runs golden WAV validation
```

**Run with CTest:**
```bash
ctest              # Run all tests, short output
ctest -V           # Verbose (show each test result)
ctest -R <regex>   # Run matching tests (e.g., -R l1)
ctest --rerun-failed  # Rerun tests that failed last time
```

**Run host_mock (autoseq behavior):**
```bash
cd host_mock
make               # Builds host_test.exe
./host_test test_qso.json
./host_test test_beacon.json
./host_test test_fd_exchange.json
```

## CI/CD Integration

**Current Status:**
- Tests are manual (no GitHub Actions or CI pipeline configured)
- CMakeLists.txt prepared for `cmake --build` and `ctest`
- Golden WAV files committed to repo for regression detection

**Recommended Workflow:**
1. Local: Run `cmake --build` + `ctest` before push
2. CI (if added): Run same commands on PR to catch regressions
3. Device: Manual test on Cardputer for integration verification

---

*Testing analysis: 2026-07-04*
