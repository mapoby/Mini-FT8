# Requirements: Mini-FT8 — Yaesu FTX-1 Support

**Defined:** 2026-07-04
**Core Value:** A user with a Yaesu FTX-1 can plug it into the Cardputer over USB and run full FT8/FT4 QSOs — RX decode, autoseq, and TX — exactly as they already can with QMX/QDX/KH1.

## v1 Requirements

### Backend Plumbing

- [x] **PLUMB-01**: User can select "FTX-1" as the active radio in station configuration, alongside QMX/QDX/KH1
- [x] **PLUMB-02**: `radio_control_ftx1.cpp` implements the existing `radio_control_ops_t` vtable, mirroring the QMX/KH1 backend pattern

### CAT Connection

- [x] **CAT-01**: Firmware opens a CAT connection to the FTX-1's CP210x USB virtual COM port via the `espressif/usb_host_cp210x_vcp` component
- [x] **CAT-02**: CDC-ACM install/scan logic remains strictly profile-gated so it never misclaims the FTX-1's vendor-class CP210x interface
- [x] **CAT-03**: Firmware explicitly deasserts RTS/DTR immediately after opening the CP210x port, as defense-in-depth against double-PTT

### Frequency and Mode Sync

- [ ] **SYNC-01**: Firmware sets VFO-A frequency via CAT (`FA<9-digit Hz>;`, range 000030000–470000000 Hz)
- [ ] **SYNC-02**: Firmware sets DATA-U operating mode via CAT (`MD0C;`) once at sync time, not resent per TX
- [ ] **SYNC-03**: Firmware restores RX dial frequency/mode after each TX, mirroring the KH1 backend's post-TX restore pattern

### PTT Control

- [ ] **PTT-01**: Firmware keys PTT via CAT (`TX1;`) and unkeys via CAT (`TX0;`) — no RTS/DTR hardware toggling
- [ ] **PTT-02**: Firmware sequences mode-set before PTT-key, and drains audio before unkeying, mirroring the QMX backend's drain-before-unkey pattern

### Audio Streaming

- [x] **AUDIO-01**: Firmware streams RX audio from the FTX-1's USB audio input (mic) into the existing FT8/FT4 decode pipeline, using runtime format negotiation (not a hardcoded profile) since the FTX-1's USB Audio descriptor is unconfirmed
- [x] **AUDIO-02**: Firmware streams TX (FSK tone) audio to the FTX-1's USB audio output (speaker) during transmit, reusing the QMX DDS/UAC-OUT pre-open pattern
- [x] **AUDIO-03**: USB host FIFO partitioning is re-tuned (not copy-pasted from QMX) to support a third simultaneous USB client (CP210x bulk CAT + bidirectional UAC audio)

### End-to-End Validation

- [ ] **E2E-01**: A full FT8 QSO (RX decode → autoseq → TX) is validated end-to-end against physical FTX-1 hardware
- [ ] **E2E-02**: A full FT4 QSO (RX decode → autoseq → TX) is validated end-to-end against physical FTX-1 hardware

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Power Control

- **POWER-01**: User can read/set FTX-1 TX power via CAT (`PC1<3-digit W>;`, 005–010 W field-head range) via a new `set_power` vtable hook
- **POWER-02**: Station configuration UI exposes power control, otherwise the hook is unreachable dead code

### Conditional Hardening

- **HARDEN-01**: Defensive re-assertion of DATA-U mode during TX — only if real-hardware testing during v1 confirms the FTX-1 actually exhibits the DATA-U→USB mode-reversion quirk reported on related Yaesu models (FT-991A/FTDX-10); do not pre-build for an unconfirmed bug

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Split VFO (separate RX/TX VFO-A/VFO-B) | No confirmed FT8 use case; FTX-1 CAT mirrors QMX/KH1's single-VFO model |
| CAT-2 (Standard COM) second virtual port usage | Single-port design (CAT-1 only) mirrors QMX; avoids a second port's lifecycle management |
| CAT-3 wired UART fallback | Contradicts the stated USB-only requirement; only reconsider if the CP210x/USB path proves technically blocked |
| SPA-1 external amplifier power control (5–100 W) | No confirmed external amp in use; different command semantics/range than the field head |
| Generic C++ `usb_host_vcp` factory/service | Requires C++ exceptions, incompatible with this codebase's plain-C/`esp_err_t` style; superseded by the plain-C `usb_host_cp210x_vcp` |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| PLUMB-01 | Phase 1 | Complete |
| PLUMB-02 | Phase 1 | Complete |
| CAT-01 | Phase 2 | Complete |
| CAT-02 | Phase 2 | Complete |
| CAT-03 | Phase 2 | Complete |
| SYNC-01 | Phase 3 | Pending |
| SYNC-02 | Phase 3 | Pending |
| SYNC-03 | Phase 3 | Pending |
| PTT-01 | Phase 3 | Pending |
| PTT-02 | Phase 3 | Pending |
| AUDIO-01 | Phase 4 | Complete |
| AUDIO-02 | Phase 4 | Complete |
| AUDIO-03 | Phase 4 | Complete |
| E2E-01 | Phase 5 | Pending |
| E2E-02 | Phase 5 | Pending |

**Coverage:**
- v1 requirements: 15 total
- Mapped to phases: 15
- Unmapped: 0 ✓

---
*Requirements defined: 2026-07-04*
*Last updated: 2026-07-04 after initial definition*
