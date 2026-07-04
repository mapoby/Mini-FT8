# Coding Conventions

**Analysis Date:** 2026-07-04

## Naming Patterns

**Files:**
- Source files: `lowercase_with_underscores.cpp` or `.h`
- Example: `audio_source.cpp`, `autoseq.h`, `core_api_internal.h`

**Functions:**
- All functions use snake_case
- Example: `autoseq_init()`, `hashtable_lookup()`, `storage_is_active_log_name()`, `core_cmd_set_call()`

**Types (structs, classes):**
- PascalCase for all type names
- Example: `QsoContext`, `ProtocolConfig`, `RxDecodeEntry`, `StationConfig`

**Enums:**
- Enum class names: PascalCase
- Enum values: PascalCase
- Example: `enum class AutoseqState { CALLING, REPLYING, REPORT, IDLE }`
- Example: `enum class CoreCqType { CQ = 0, SOTA, POTA, QRP, FD, FREETEXT }`

**Constants and Macros:**
- SCREAMING_SNAKE_CASE
- Example: `AUTOSEQ_MAX_QUEUE`, `CALLSIGN_HASHTABLE_SIZE`, `FT8_SAMPLE_RATE`, `ENABLE_FT4`

**Global Variables:**
- Extern/public globals: `g_prefix_name`
- Example: `g_protocol`, `g_qso_xmit`, `g_decode_in_progress`, `g_band_sel`, `g_offset_hz`
- Accessed from multiple files (declared in main.cpp, extern in other modules)

**Static/Internal Variables:**
- Module-static variables: `s_prefix_name`
- Example: `s_queue`, `s_active_count`, `s_tx_msg_buffer`, `s_pending_ft_text`, `s_my_call`
- Used for encapsulation within a single translation unit

## Code Style

**Formatting:**
- No automatic formatter configured (.clang-format not present)
- Manual formatting observed:
  - 4-space indentation (observed in all source files)
  - Opening braces on same line for functions
  - Long lines sometimes split across multiple lines
  - Consistent spacing around operators

**Linting:**
- No .clang-tidy or lint configuration files present
- Compiler flags in CMakeLists.txt: `-Wall -Wextra -Wno-unused-parameter`
- MSVC unsupported (C99 VLA requirement) per `tests/tx_e2e/CMakeLists.txt`

## Import Organization

**Order:**
1. C++ standard library headers (with `c` prefix): `#include <cstdio>`, `#include <cstdint>`, `#include <string>`
2. System/FreeRTOS headers: `#include "esp_log.h"`, `#include "freertos/FreeRTOS.h"`
3. Local project headers: `#include "autoseq.h"`, `#include "core_api.h"`
4. Conditional includes based on build flags

**Examples from `main.cpp` (lines 1-65):**
```cpp
#define DEBUG_LOG 1

#include <cstdio>
#include <cmath>
#include "esp_log.h"
extern "C" {
  #include "ft8/decode.h"
  #include "ft8/constants.h"
  // ...
}

#include "board_power.h"
#include "ui.h"
#include <vector>
#include <string>
#include "freertos/FreeRTOS.h"
```

**Path Aliases:**
- No custom path aliases used; imports are relative to include directories
- Include directories set via CMakeLists.txt `target_include_directories()`

## Error Handling

**Patterns:**
- Return `bool` for success/failure: `true` = success, `false` = failure
  - Example: `bool autoseq_drop_index(int idx)` returns false if index out of range
  - Example: `bool core_cmd_set_call(const std::string& call)` returns false on validation error
  
- Return `int` status codes for enum-style errors
  - Example: `ftx_message_rc_t` from ft8_lib
  
- Input validation at function entry
  - Example from `hashtable_lookup()` (main.cpp:262-265):
    ```cpp
    bool hashtable_lookup(...) {
        if (!callsign)
            return false;
        // ...
    }
    ```

- Early returns for error conditions (fail-fast pattern)
  - Example from `hashtable_add()` (main.cpp:202-220): checks preconditions first, returns early if invalid

- Optional pointer validation before use
  - Example: `if (!ctx) return false;` (autoseq.cpp:98)

- No exceptions used (embedded system)

## Logging

**Framework:** ESP-IDF logging macros

**Log Levels:**
- `ESP_LOGI(TAG, ...)` - Informational (common state transitions, normal operations)
- `ESP_LOGW(TAG, ...)` - Warning (recoverable errors, retry scenarios)
- `ESP_LOGE(TAG, ...)` - Error (critical failures; not found in analyzed code but pattern established)

**Patterns:**
- Each module defines a static `const char* TAG` at file top
  - Example (autoseq.cpp:18): `static const char* TAG = "AUTOSEQ";`
  - Example (main.cpp:346): `static const char* TAG = "FT8";`
  
- Log state transitions with relevant context
  - Example (autoseq.cpp:166): `ESP_LOGI(TAG, "Started CQ on slot %d", slot_parity);`
  
- Debug prints via `fprintf(stderr, ...)` for development
  - Example (test_l1_encoder.cpp:93-94): `fprintf(stderr, "  synth ok %d samples\n", total_samples);`
  
- Conditional debug logging with `DEBUG_LOG` macro
  - Example (main.cpp:1): `#define DEBUG_LOG 1`

**What to Log:**
- State machine transitions (entering/leaving states)
- Queue operations (add, remove, clear)
- User actions (tap RX, drop QSO, set config)
- Error conditions with context (callsign, slot, retry count)
- Timing issues (slot boundaries, delays)

## Comments

**When to Comment:**
- Invariants and preconditions (why a check is needed)
  - Example (autoseq.cpp:93-101): Explains when `has_exchanged()` returns true and why REPLYING is safe to evict
  
- Complex algorithms (state machine transitions, hash probe logic)
  - Example (main.cpp:169-200): Detailed explanation of hashtable trim logic with priority scheme
  
- Non-obvious data structure layouts
  - Example (main.cpp:137-142): Explains callsign_hashtable struct fields and bit packing strategy
  
- Section headers and grouping
  - Format: `// ============== Section Name ==============`
  - Example (autoseq.cpp:20-21, 71, 112)

**Do NOT Comment:**
- Obvious code (don't repeat what code says)
- Standard patterns (well-known algorithms like linear probing)

**JSDoc/TSDoc:**
- Not used; this is C/C++ embedded code
- Some header comments for API boundaries (see `core_api.h` lines 1-22)

**Header Comments (for public APIs):**
- Multi-line comment block above #pragma once explaining module purpose
- Example (core_api.h:1-22): Explains "Functional-core API", "notify + pull" pattern, thread safety

## Function Design

**Size Guidelines:**
- Most functions stay under 50 lines
- State machine handlers (autoseq) may be 100+ lines (acceptable due to complexity)
- Larger functions are broken into logical sections with comment separators

**Parameters:**
- Prefer value types for simple data (`int`, `bool`, `float`)
- Use const references for strings and vectors: `const std::string&`, `const std::vector<>`
- Output parameters passed as non-const references or pointers
  - Example (core_api.h:144): `void core_get_rx_list(std::vector<RxDecodeEntry>& out);`
  - Example (core_api.h:175): `CoreWaterfallCb cb` (function pointer for callback)

**Return Values:**
- Prefer returning status via return code, not output parameters
- Use `bool` for success/failure
- Use `std::string` for text returns (RVO/move semantics handle efficiency)
- Return const pointers when returning static data (`const char*`, `const ProtocolConfig*`)

**Callbacks:**
- Function pointer typedefs for callback registration
  - Example (core_api.h:170-171):
    ```cpp
    using CoreChangeCb    = void (*)(void);
    using CoreWaterfallCb = void (*)(const WaterfallRow& row);
    ```
- Callbacks are registered once at init time; no removal API

## Module Design

**Exports:**
- Header files use `#pragma once`
- Public functions declared in .h, implemented in .cpp
- Static module-level functions declared in .cpp (not in .h)
- Extern variables declared in .h, defined in one .cpp (usually main.cpp)

**Barrel Files:**
- Not used; each .h maps to one .cpp with same base name

**Encapsulation:**
- Heavy use of `static` for module-level state (autoseq.cpp has ~10 static variables)
- `extern` globals provide controlled access to main.cpp state
- Callbacks enable bidirectional communication without tight coupling

## Key Architectural Patterns

**Notify + Pull Pattern (core_api.h:10-16):**
1. On init, pull a snapshot with `core_get_*()` (thread-safe)
2. Register callback with `core_on_*_changed()` 
3. When callback fires, pull fresh snapshot and update

**State Machine Pattern (autoseq.cpp):**
- `AutoseqState` enum for states
- `QsoContext` struct holds per-QSO state
- `TxMsgType` enum for message types
- `set_state()` and `generate_response()` handle transitions
- Comments explain invariants (e.g., next_tx may only be TX1-TX5, never TX6)

**Linear Probing Hash Table (main.cpp:145-304):**
- Fixed-size array (`CALLSIGN_HASHTABLE_SIZE = 128`)
- Age tracking in upper 8 bits of hash value
- 22-bit hash payload stored (6000 unique callsigns in 128 slots)
- Comments explain full table scan due to deletion holes

---

*Convention analysis: 2026-07-04*
