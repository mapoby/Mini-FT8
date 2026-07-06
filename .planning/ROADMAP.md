# Roadmap: Mini-FT8 — Yaesu FTX-1 Support

## Overview

This milestone adds the Yaesu FTX-1 as a fourth radio backend to Mini-FT8, following the existing QMX/QDX/KH1 pattern. The FTX-1's single USB port exposes two independent surfaces that must both work before FT8/FT4 QSOs are possible: a CP210x vendor-class CAT channel and bidirectional USB audio. Work proceeds horizontally through complete technical layers — vtable plumbing first (no hardware needed), then USB transport bring-up (highest risk, hardware-gated), then the CAT protocol and audio layers built on top of that transport (can proceed in parallel), and finally end-to-end integration testing that proves the whole stack together on real hardware.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Backend Vtable Plumbing** - FTX-1 selectable as a radio type with a stubbed ops vtable, no hardware required
- [x] **Phase 2: CP210x USB Bring-up & CAT Connection** - Firmware reliably opens a CAT channel to the physical FTX-1 over its CP210x virtual COM port (completed 2026-07-06)
- [x] **Phase 3: CAT Command Implementation** - Frequency, mode, and PTT control work over CAT against the physical radio (completed 2026-07-06)
- [ ] **Phase 4: Bidirectional UAC Audio Negotiation** - Mic RX and speaker TX audio stream correctly over USB against the physical radio
- [ ] **Phase 5: End-to-End Integration and Parity Testing** - Full FT8 and FT4 QSOs complete end-to-end on the physical FTX-1

## Phase Details

### Phase 1: Backend Vtable Plumbing
**Goal**: The FTX-1 backend exists as a selectable radio type with a complete (stubbed) `radio_control_ops_t` implementation, mirroring the QMX/KH1 backend pattern. No hardware required.
**Depends on**: Nothing (first phase)
**Requirements**: PLUMB-01, PLUMB-02
**Success Criteria** (what must be TRUE):
  1. User can select "FTX-1" from the station configuration radio list, alongside QMX/QDX/KH1
  2. Firmware builds and boots with `radio_control_ftx1.cpp` registered and its ops vtable fully populated (stub `ready()` returns false pending real hardware bring-up)
  3. Selecting FTX-1 in configuration does not crash the firmware or affect QMX/QDX/KH1 selection paths
**Plans**: 1 plan

- [x] 01-01-PLAN.md — FTX-1 stub backend, dispatch/build registration, and station-config wiring (all 12 enumeration sites)

### Phase 2: CP210x USB Bring-up & CAT Connection
**Goal**: Firmware reliably opens and maintains a CAT connection to the physical FTX-1 over its CP210x USB virtual COM port, with the vendor-class interface never misclaimed by existing CDC-ACM logic.
**Depends on**: Phase 1
**Requirements**: CAT-01, CAT-02, CAT-03
**Success Criteria** (what must be TRUE, hardware-verifiable):
  1. Plugging the FTX-1 into the Cardputer causes it to enumerate and open the CP210x CAT-1 port reliably across repeated cold-boot/replug cycles
  2. Firmware log confirms RTS/DTR are explicitly deasserted immediately after CP210x port open, and the FTX-1 shows no spurious PTT/keyup on its own display
  3. A QMX device still enumerates and opens correctly via the existing CDC-ACM path with no regression
  4. The FTX-1's CP210x vendor-class interface is never claimed by CDC-ACM scan/install logic (no scan conflicts or errors in the log when the FTX-1 is connected)
**Plans**: 3 plans

- [x] 02-01-PLAN.md — CP210x component dependency, UAC_PROFILE_FTX1/AUDIO_SOURCE_FTX1_CP210X enums, and the get_radio_profile_binding() QMX-alias bugfix (CAT-01, CAT-02)
- [x] 02-02-PLAN.md — cp210x_try_open() port open + RTS/DTR deassert, cdc_try_open() profile hard-gate, install-gate broadening, and ftx1_ready() wiring (CAT-01, CAT-02, CAT-03)
- [x] 02-03-PLAN.md — Physical FTX-1 hardware bring-up checkpoint: enumeration, no-spurious-PTT, no-misclaim, QMX no-regression, PID/interface-index confirmation (CAT-01, CAT-02, CAT-03)

### Phase 3: CAT Command Implementation
**Goal**: Firmware controls the FTX-1's frequency, mode, and PTT entirely via CAT commands, with correct sequencing and no RTS/DTR hardware toggling.
**Depends on**: Phase 2
**Requirements**: SYNC-01, SYNC-02, SYNC-03, PTT-01, PTT-02
**Success Criteria** (what must be TRUE, hardware-verifiable):
  1. Setting a frequency in Mini-FT8 updates the FTX-1's displayed VFO-A frequency correctly, across the 000030000–470000000 Hz range
  2. The FTX-1 display shows DATA-U mode after sync, and CAT traffic logs confirm `MD0C;` is sent once at sync time, not resent on every TX
  3. Pressing TX in Mini-FT8 keys the FTX-1 (visible PTT indicator on the radio) via `TX1;` only, with no RTS/DTR toggling observed
  4. After TX completes, the FTX-1 unkeys via `TX0;` only after audio has fully drained, and the radio's RX frequency/mode display is restored to its pre-TX state
**Plans**: 2 plans

- [x] 03-01-PLAN.md — CP210x CAT-1 line coding (38400 8N1) + real FTX-1 CAT command implementation (FA/MD/TX, clamp, drain-before-unkey, post-TX restore) (SYNC-01, SYNC-02, SYNC-03, PTT-01, PTT-02)
- [x] 03-02-PLAN.md — Physical FTX-1 hardware checkpoint: clean -Werror build + frequency/DATA-U/PTT/restore validation and CAT-1 PTT-scoping resolution (SYNC-01, SYNC-02, SYNC-03, PTT-01, PTT-02)

### Phase 4: Bidirectional UAC Audio Negotiation
**Goal**: Firmware streams RX audio from the FTX-1's USB mic input into the decode pipeline and TX tone audio to its USB speaker output, using runtime format negotiation validated against the real device.
**Depends on**: Phase 2 (can proceed in parallel with Phase 3)
**Requirements**: AUDIO-01, AUDIO-02, AUDIO-03
**Success Criteria** (what must be TRUE, hardware-verifiable):
  1. Mic audio from the FTX-1 appears in Mini-FT8's waterfall display continuously, without dropouts, during a live RX session
  2. Mini-FT8-generated FSK tone audio sent to the FTX-1's speaker input produces a clean, undistorted transmitted signal (confirmed by monitoring receiver or SDR)
  3. Runtime format negotiation succeeds against the FTX-1's actual (previously unconfirmed) USB Audio descriptor without any hardcoded QMX-style profile
  4. Simultaneous bidirectional audio plus CP210x CAT traffic runs through multiple RX/TX cycles with no FIFO overrun/underrun in the logs
**Plans**: 2 plans

- [x] 04-01-PLAN.md — FTX-1 mic candidate-scan, speaker guard widening, and FIFO partition widening in stream_uac.cpp (AUDIO-01, AUDIO-02, AUDIO-03)
- [ ] 04-02-PLAN.md — Physical FTX-1 hardware checkpoint: clean build, negotiated mic format, TX tone quality, and combined-load FIFO stability across multiple cycles (AUDIO-01, AUDIO-02, AUDIO-03)

### Phase 5: End-to-End Integration and Parity Testing
**Goal**: A full FT8 or FT4 QSO — RX decode, autoseq, and TX — works end-to-end with the FTX-1, at parity with QMX/QDX/KH1.
**Depends on**: Phase 3, Phase 4
**Requirements**: E2E-01, E2E-02
**Success Criteria** (what must be TRUE, hardware-verifiable):
  1. A complete FT8 QSO (RX decode -> autoseq -> TX) completes successfully against a real or loopback contact using the physical FTX-1
  2. A complete FT4 QSO (RX decode -> autoseq -> TX) completes successfully against a real or loopback contact using the physical FTX-1
  3. Sustained operation across multiple consecutive TX/RX cycles shows no regressions in the CAT or audio behavior established in Phases 2-4
**Plans**: 2 plans

- [ ] 04-01-PLAN.md — FTX-1 mic candidate-scan, speaker guard widening, and FIFO partition widening in stream_uac.cpp (AUDIO-01, AUDIO-02, AUDIO-03)
- [ ] 04-02-PLAN.md — Physical FTX-1 hardware checkpoint: clean build, negotiated mic format, TX tone quality, and combined-load FIFO stability across multiple cycles (AUDIO-01, AUDIO-02, AUDIO-03)

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Backend Vtable Plumbing | 1/1 | Complete    | 2026-07-05 |
| 2. CP210x USB Bring-up & CAT Connection | 3/3 | Complete    | 2026-07-06 |
| 3. CAT Command Implementation | 0/2 | Not started | - |
| 4. Bidirectional UAC Audio Negotiation | 1/2 | In progress | - |
| 5. End-to-End Integration and Parity Testing | 0/TBD | Not started | - |
