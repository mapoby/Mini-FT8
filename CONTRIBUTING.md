<!-- generated-by: gsd-doc-writer -->
# Contributing to Mini-FT8

Thanks for your interest in contributing to Mini-FT8. This document covers the practical steps for
getting a change merged. For environment setup, see GETTING-STARTED.md; for local build/test workflow,
see DEVELOPMENT.md.

## Hardware dependency note

Mini-FT8 is embedded firmware for the M5Stack Cardputer ADV, and many changes touch radio-specific
CAT/audio paths (QMX, QDX, KH1, FTX-1), GPS modules, or the DS3231 RTC module. Some of this can be
exercised without hardware (protocol-level logic, state machines, and the host-side tests under
`tests/tx_e2e/`), but a meaningful part of the codebase — USB descriptor negotiation, audio FIFO
timing, CAT command framing — can only be fully verified against the physical radio or module it
targets.

Hardware access is not a strict requirement to contribute, but if your change touches
hardware-facing code:
- Say explicitly in your PR description whether the change was **hardware-verified** (tested against
  the physical radio/module) or **not hardware-verified** (compiles/passes host tests only, logic
  reviewed but unconfirmed on real hardware).
- If not hardware-verified, note which radio/module would be needed to confirm it, so a reviewer
  with that hardware can test before merge.

## Development setup

See GETTING-STARTED.md for prerequisites (ESP-IDF v5.5.1 toolchain) and first build/flash, and
DEVELOPMENT.md for the day-to-day local development loop.

## Coding standards

- No automatic formatter is configured (no `.clang-format`); match the surrounding file's existing
  style.
- Compiler warnings are enabled via `-Wall -Wextra -Wno-unused-parameter` in
  `tests/tx_e2e/CMakeLists.txt` (host-side test suite; the root firmware build uses ESP-IDF's own
  warning configuration instead); keep new code warning-clean under these flags.
- Follow the naming and structure conventions already in use in the codebase: `snake_case` for
  functions and files, `PascalCase` for types, `SCREAMING_SNAKE_CASE` for constants, `g_` prefix for
  extern globals, `s_` prefix for module-static state.
- MSVC is unsupported for the host-side test suite (`tests/tx_e2e/`) due to its C99 VLA requirement;
  use MinGW/GCC or Clang if building that suite on Windows.
- No CI lint/test enforcement is currently configured in this repository — treat a clean local build
  and, where applicable, a hardware-verified test as the bar for a mergeable change.

## Commit and PR guidelines

- Keep commits focused on a single logical change.
- Write commit messages as short, imperative statements (e.g., `fix: correct FTX-1 TX arming race`),
  lowercase after the first word.
- Reference the affected radio or subsystem in the commit message when the change is radio-specific
  (e.g., FTX-1, QMX, QDX, KH1, GPS, RTC).
- In the PR description, state whether hardware-facing changes were hardware-verified (see above).
- No `.github/PULL_REQUEST_TEMPLATE.md` currently exists in this repository, so there is no required
  checklist — describe what changed, why, and how it was tested (host tests, hardware, or both).
- Open the PR against `main`.

## Reporting issues

This repository does not currently have `.github/ISSUE_TEMPLATE/` templates configured. Open a GitHub
Issue with:
- A clear description of the problem or feature request.
- Which radio/module is involved, if applicable (QMX, QDX, KH1, FTX-1, GPS, DS3231 RTC).
- Steps to reproduce, expected vs. actual behavior, and firmware/build details (ESP-IDF version,
  Cardputer ADV hardware revision if known).
- Whether the issue was observed on physical hardware or suspected from code review.

## License

By contributing, you agree that your contributions will be licensed under the project's MIT License
(see `LICENSE`).
