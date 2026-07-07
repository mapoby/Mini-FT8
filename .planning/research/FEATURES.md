# Feature Research

**Domain:** CAT-controlled USB transceiver integration for FT8/FT4 digital modes (Yaesu FTX-1 backend)
**Researched:** 2026-07-04
**Confidence:** MEDIUM (Hamlib/WSJT-X community behavior verified via multiple independent sources; exact FTX-1 firmware quirks unverified — FTX-1 is new, so evidence is extrapolated from FT-991A/FT-710/FTDX-10, which share the same Yaesu CAT command family and DATA-U mode design)

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist because QMX/QDX/KH1 already provide them in Mini-FT8. Missing any of these makes FTX-1 support feel broken relative to the existing radios.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| VFO-A frequency sync via CAT (`FA`) before RX/TX | Every existing backend (QMX `FA`, KH1 `FA`) sets frequency on band/mode change; FT8 decode is frequency-locked to the dial | LOW | FTX-1 `FA<9-digit>;` differs from QMX's 11-digit and KH1's 7-digit (10 Hz) — format must match exactly or the radio silently ignores the command (same failure class already hit and fixed in `qmx_set_tone_hz`) |
| Mode set to DATA-U (`MD0C;`) before TX | FT8/FT4 needs a digital-friendly IF/AGC/filter mode; plain USB uses different filter width and mic path (VOX would key from voice audio, not FSK tones) | LOW | Must be sent once at sync time, not per-TX; sending it inside the TX hot path risks a mode-glitch mid-transmission (see Pitfalls: DATA-U → USB fallback) |
| CAT-triggered PTT (`TX1;` / `TX0;`) | QMX and KH1 already key via CAT command (QMX `TX;`/`RX;`, KH1 `HK1;`/`HK0;`); user expectation is CAT-only PTT, no separate hardware line | LOW | Requires `RPTT SELECT` menu = OFF on the radio so RTS/DTR from the CP210x VCP driver cannot ALSO try to key PTT — this is the single most common source of the "double PTT" bug across all Yaesu CAT-PTT setups, not FTX-1-specific |
| Bidirectional USB audio (mic in for RX decode, speaker out for TX tones) | QMX already streams both directions over UAC; user plugs in one USB cable and expects RX waterfall + TX audio to "just work" | MEDIUM | FTX-1 UAC descriptor (sample rate/bit depth/channel count) is unconfirmed until tested against hardware; existing `UAC_PROFILE_GENERIC_USB` negotiation loop is the right starting point, not QMX's hardcoded profile |
| End-to-end FT8 QSO (RX decode → autoseq → TX) | Core Value in PROJECT.md — FTX-1 parity with QMX/QDX/KH1 | MEDIUM | Depends on all of the above; this is the integration test, not a separate feature |
| Return-to-RX frequency/mode after TX | KH1 backend explicitly restores dial frequency after every TX (`kh1_end_tx` forces `FA`); users on FT8 split practices expect the rig to land back on the RX dial without manual intervention | LOW | Directly transferable pattern from KH1; FTX-1 CAT is Kenwood-style single-VFO like KH1/QMX, not split-VFO like some other Yaesu rigs, so no separate VFO-B bookkeeping needed |

### Differentiators (Competitive Advantage)

Not required for parity with QMX/QDX/KH1, but explicitly named in PROJECT.md as in-scope for this milestone and valuable beyond bare FT8 function.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| TX power read/set via CAT (`PC1<3-digit>;`) | None of QMX/QDX/KH1 backends currently expose power control through the vtable (`radio_control_ops_t` has no `set_power` hook) — this would be a genuinely new capability, letting users manage the FTX-1 field head's 5–10 W range from the Cardputer UI instead of the radio's own menu | MEDIUM | Needs a new vtable hook (`set_power` or similar) since the existing interface doesn't have one — touches the shared backend header, not just the new file; also needs UI wiring in station configuration to actually be useful, or it's dead code |
| Runtime USB Audio format negotiation for a UAC device with unknown descriptors | QMX's profile hardcodes VID/PID/format; FTX-1 forces the generic-negotiation path to become a real, exercised code path rather than a vestigial branch | MEDIUM | This isn't "extra" so much as validating existing infrastructure under a second real device — valuable regardless of FTX-1 because it makes the generic profile production-proven for future radios too |
| CP210x virtual COM CAT via `usb_host_vcp` | First non-CDC-ACM USB serial backend in the project; opens the door to FTDI/CH340-based radios and interfaces in future milestones without new driver work | MEDIUM-HIGH | This is infrastructure investment disguised as a single-radio feature — genuinely differentiates the project's radio-support ceiling going forward |

### Anti-Features (Commonly Requested, Often Problematic — Explicitly Out of Scope per PROJECT.md)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|------------------|-------------|
| Split VFO (separate RX/TX VFO-A/VFO-B) | Standard Yaesu CAT supports VFO-A/B split, and some FT8 workflows (compound-call DXpeditions) use split | FTX-1 CAT-1 port design here mirrors QMX's single-VFO model per PROJECT.md; FT8 rarely needs split (same-frequency TX/RX is the norm) and adding VFO-B bookkeeping doubles the state machine surface for a use case with no confirmed user need | Single VFO-A frequency sync, same pattern as QMX/KH1; revisit only if a real split-operation need surfaces |
| RTS/DTR hardware PTT toggling | It's the "traditional" digital-mode PTT method most Windows software defaults to, and some users may reflexively wire it up | Explicitly rejected in PROJECT.md — CAT `TX1;`/`TX0;` is the chosen path; enabling RTS/DTR *in addition* to CAT PTT is the textbook cause of "double PTT" (radio receives two conflicting key commands, can hang mid-TX or key erratically) — must actively confirm `RPTT SELECT`=OFF, not just avoid wiring RTS/DTR in firmware | CAT-only PTT (`TX1;`/`TX0;`), with the `RPTT SELECT` radio menu setting documented as a required user-side precondition |
| CAT-3 wired UART connection | Alternate physical path already proven for KH1 (inverted UART), tempting to reuse for FTX-1 as a fallback if USB CP210x integration proves difficult | Explicitly out of scope per PROJECT.md — user requirement is USB-only; CAT-3 also only carries CAT (not audio), so it wouldn't reduce scope, only add a second serial path with no audio benefit | If `usb_host_vcp`/CP210x proves blocking, the correct escalation is more research/hardware debugging on the CP210x path, not falling back to wired UART |
| SPA-1 external amplifier power control (5–100 W) | Power control is being added for the FTX-1 field head anyway (`PC` command); tempting to extend the same command family to control an attached amp | Explicitly out of scope per PROJECT.md — no confirmed external amp in use; amplifier CAT control uses different command semantics/ranges and would require hardware not present for validation | Field-head-only `PC1` power range (005–010 W); revisit only if a user with an SPA-1 requests it in a future milestone |
| CAT-2 (Standard COM) second virtual port usage | The FTX-1 exposes two COM ports; using both (like some Yaesu digital-mode guides suggest — one for CAT, one for RTS-based PTT) is a common pattern in general Yaesu/WSJT-X community setups | Explicitly out of scope per PROJECT.md — combining PTT and freq/mode CAT on the single CAT-1 (Enhanced COM) port mirrors QMX's single-port design and avoids a second `usb_host_vcp` COM port needing its own lifecycle management | Single-port CAT-1 for both frequency/mode and PTT, exactly as scoped |
| Auto-detecting/handling the "DATA-U silently reverts to USB mode on TX" firmware quirk with a workaround CAT re-send loop | Community reports (ftdx-10 users) describe some Yaesu digital rigs occasionally reverting from DATA-U to USB mode during FT8 TX; a tempting defensive fix is to re-send `MD0C;` on every TX cycle "just in case" | Turns a possibly rig/firmware/menu-specific edge case (unconfirmed on FTX-1 specifically) into a permanent per-TX CAT round-trip that adds latency to the TX hot path and cost visible to every user, for a bug that may not reproduce on this radio/firmware at all | Set DATA-U once at sync time (matching QMX/KH1 pattern of mode-set-on-sync, not mode-set-on-every-TX); only add defensive re-assertion if real hardware testing confirms the FTX-1 actually exhibits this quirk |

## Feature Dependencies

```
FTX-1 backend selectable in station config
    └──requires──> usb_host_vcp CP210x CAT connection established
                       └──requires──> FA frequency sync
                       └──requires──> MD0C DATA-U mode set
                                          └──requires (TX only)──> TX1;/TX0; PTT
                                                                       └──conflicts──> RTS/DTR hardware PTT (RPTT SELECT must be OFF)
    └──requires──> UAC bidirectional audio stream (mic RX + speaker TX)
                       └──requires──> runtime format negotiation (unknown descriptors)

PC power read/set (differentiator)
    └──requires──> new set_power hook in radio_control_ops_t vtable
    └──enhances──> station configuration UI (otherwise unreachable by user)

CP210x usb_host_vcp integration
    └──enhances──> future radio backends (FTDI/CH340-based rigs), beyond this milestone
```

### Dependency Notes

- **FTX-1 backend requires CP210x CAT connection:** Unlike QMX (genuine CDC-ACM), the FTX-1's CAT-1 port is a CP210x vendor-specific USB-to-UART bridge. The existing CDC-ACM open/scan path in `stream_uac.cpp` will not enumerate it; `usb_host_vcp` must be integrated and working before any CAT command (`FA`, `MD`, `TX`, `PC`) can be sent.
- **MD0C mode set requires FA sync to already exist as a code path, but is otherwise independent:** Order matters less between FA/MD than between MD and TX — the radio must be in DATA-U before PTT is asserted, or PTT may key the radio in the wrong mode (wrong filter, wrong TX audio routing), producing an out-of-band or garbled FT8 transmission. Existing QMX/KH1 patterns confirm mode-then-PTT ordering (both set MD before TX in `qmx_begin_tx` / mode set in `kh1_sync_frequency_mode` before any `HK1;`).
- **TX1;/TX0; conflicts with RTS/DTR:** This is the most consequential dependency for reliability. If `usb_host_vcp`'s CP210x driver asserts RTS/DTR by default on port open (a common driver behavior) while `RPTT SELECT` on the radio is left at its factory default of controlling PTT from RTS, the radio will receive two independent PTT triggers per TX cycle — the CAT `TX1;` and a spurious RTS toggle from port open/close. This must be verified against real hardware (the user has a physical unit) rather than assumed from documentation alone.
- **Bidirectional UAC audio requires runtime negotiation:** QMX's `UAC_PROFILE_QMX` hardcodes format assumptions valid only for that specific VID/PID. The FTX-1's descriptors are unconfirmed, so it must use (and likely extend) the `UAC_PROFILE_GENERIC_USB` negotiation loop rather than a new hardcoded profile — this is a dependency on existing infrastructure being genuinely bidirectional-capable, which today's generic profile is documented as "mic-only" in PROJECT.md context. This gap must close before FTX-1 audio can work at all.
- **PC power control enhances but does not gate core FT8 function:** Frequency sync, mode set, PTT, and audio are all required for a working QSO; power control is purely additive and can be deferred to a later phase without blocking FT8/FT4 parity.

## MVP Definition

### Launch With (v1)

Minimum viable product — FTX-1 reaches functional parity with QMX/QDX/KH1 for FT8/FT4 QSOs.

- [ ] CP210x CAT connection via `usb_host_vcp` — without this, no CAT commands can reach the radio at all
- [ ] `FA` frequency sync (9-digit Hz format) — FT8 decode requires correct dial frequency
- [ ] `MD0C;` DATA-U mode set at sync time — required for correct TX/RX audio path and filtering
- [ ] `TX1;`/`TX0;` CAT PTT, with `RPTT SELECT`=OFF confirmed on hardware — core TX capability, avoiding double-PTT
- [ ] Bidirectional UAC audio (mic in, speaker out) with runtime format negotiation — RX decode and TX tone generation both depend on this
- [ ] End-to-end FT8 and FT4 QSO validated on real hardware — the actual acceptance criterion from PROJECT.md

### Add After Validation (v1.x)

Features to add once the core CAT+audio loop is proven working on real hardware.

- [ ] `PC1` TX power read/set — add once basic TX/RX is confirmed reliable; requires new vtable hook plus UI wiring, so natural second phase
- [ ] Any defensive DATA-U re-assertion logic — only add if real-hardware testing during MVP actually reproduces the DATA-U→USB reversion quirk seen in FT-991A/FTDX-10 community reports; do not pre-build a workaround for an unconfirmed FTX-1 bug

### Future Consideration (v2+)

Explicitly deferred per PROJECT.md Out of Scope, not part of this milestone at all.

- [ ] Split VFO support — no confirmed FT8 use case requiring it
- [ ] SPA-1 external amplifier power control — no confirmed amp in use
- [ ] CAT-2 second COM port usage — single-port design is sufficient and simpler
- [ ] CAT-3 wired UART fallback — contradicts USB-only requirement; only reconsider if CP210x path proves technically blocked

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|----------------------|----------|
| CP210x CAT via usb_host_vcp | HIGH | MEDIUM-HIGH | P1 |
| FA frequency sync | HIGH | LOW | P1 |
| MD0C DATA-U mode set | HIGH | LOW | P1 |
| TX1;/TX0; CAT PTT (RPTT SELECT=OFF) | HIGH | LOW-MEDIUM | P1 |
| Bidirectional UAC audio w/ negotiation | HIGH | MEDIUM-HIGH | P1 |
| PC power read/set | MEDIUM | MEDIUM | P2 |
| Defensive DATA-U re-assert (if confirmed needed) | LOW-MEDIUM | LOW | P2 (conditional) |
| Split VFO | LOW | MEDIUM | P3 (out of scope) |
| SPA-1 amp control | LOW | MEDIUM | P3 (out of scope) |

**Priority key:**
- P1: Must have for this milestone (FT8/FT4 parity)
- P2: Should have, add once P1 is proven on hardware
- P3: Explicitly out of scope this milestone per PROJECT.md

## Competitor Feature Analysis

"Competitors" here are read as the existing Mini-FT8 radio backends (QMX/KH1) and the general Yaesu/WSJT-X/Hamlib community ecosystem, since FTX-1 is a new peer within the same product, not an external rival.

| Feature | QMX backend (existing) | KH1 backend (existing) | General WSJT-X/Hamlib community practice (Yaesu rigs) | FTX-1 (this milestone) |
|---------|--------------------------|----------------------------|-------------------------------------------------------|-------------------------|
| Frequency sync | `FA<11-digit>;`, sent with `MD6;`/`FR0;`/`FT0;` preamble each sync | `FA<7-digit,10Hz>;`, cached (skip if unchanged), forced resend after TX | Hamlib "Yaesu" backends send `FA`/`FB` per rig capability; frequency is polled/set independent of PTT state | `FA<9-digit>;`, single VFO-A, matching PROJECT.md's Kenwood-style single-port design |
| Mode set | `MD6;` sent per sync/TX call | `MD2;` sent at sync and audio-start | Community setups use DATA-U (menu digital mode) rather than plain USB+VOX for FT8, citing narrower filter and cleaner keying | `MD0C;` (DATA-U) set once at sync, not resent per TX, per existing pattern |
| PTT | CAT `TX;`/`RX;`, no RTS/DTR | CAT `HK1;`/`HK0;`, no RTS/DTR | Best practice across Yaesu CAT setups: CAT-only PTT via rig command, with hardware RTS/DTR PTT explicitly disabled to avoid double-PTT — this is the dominant recommendation, not a Mini-FT8-specific choice | CAT `TX1;`/`TX0;`, `RPTT SELECT`=OFF required — matches both internal precedent and external best practice |
| Power control | Not implemented | Not implemented (`MP020;` power-menu commands exist but for KH1's ATU tuning sequence, not general power control) | Varies by rig; Hamlib exposes `set_level(RIG_LEVEL_RFPOWER)` where supported | `PC1<3-digit>;` — new capability, differentiator relative to existing Mini-FT8 backends |
| Audio | Hardcoded UAC profile (known VID/PID, 24-bit/48kHz/stereo) | External USB mic audio (not through radio_control backend) | N/A (audio is out of Hamlib's CAT scope entirely; handled by OS/soundcard layer in WSJT-X) | Generic-profile runtime negotiation, first real bidirectional test of that code path |

## Sources

- [CAT Control and Rig Control Software Guide — Ham Radio Base](https://www.hamradiobase.com/ham-radio-operating-cat-control/) — MEDIUM confidence, general CAT/PTT method overview, corroborates CAT-PTT-vs-RTS/DTR tradeoff
- [rigctl(1) man page — Hamlib](https://www.mankier.com/1/rigctl) — MEDIUM confidence, confirms `set_ptt`/PTT-type override options (RIG/DTR/RTS/CM108/GPIO) exist in Hamlib as a general mechanism
- [FT-891 Digital Settings Menu Guide — TheModernHam](https://themodernham.com/ft-891-the-ultimate-digital-settings-menu-guide-for-digital-modes/) — MEDIUM confidence, Yaesu digital-mode menu conventions (adjacent model, not FTX-1 itself)
- [FT-991 setup for digital modes — N1AV](https://www.n1rwy.org/?p=157) — MEDIUM confidence, confirms two-COM-port USB pattern and DATA-U vs USB choice on a closely related Yaesu digital-capable rig
- [ftdx-10@groups.io: "Mode switches from DATA-U to USB in FT8"](https://groups.io/g/ftdx-10/topic/mode_switches_from_data_u_to/103490731) — LOW-MEDIUM confidence (community forum thread, single source, could not fully fetch content; treat the DATA-U→USB reversion quirk as unconfirmed on FTX-1 specifically until validated on real hardware)
- [FT-710 CAT/FT8 settings tutorial](https://ft710.blogspot.com/2023/01/ft8-settings-at-710-side.html) — LOW-MEDIUM confidence, blog-level source on a closely related current-generation Yaesu digital rig
- `C:\GitHub\Mini-FT8\main\radio_control_qmx.cpp` and `C:\GitHub\Mini-FT8\main\radio_control_kh1_cat.cpp` — HIGH confidence, primary internal source for established Mini-FT8 backend patterns (frequency sync ordering, mode-before-PTT, cache/force FA resend, PTT-only-no-RTS/DTR precedent)
- `C:\GitHub\Mini-FT8\.planning\PROJECT.md` — HIGH confidence, authoritative scope boundary for this milestone (CAT commands, in/out of scope decisions, constraints)

---
*Feature research for: Yaesu FTX-1 CAT+USB-audio radio backend integration*
*Researched: 2026-07-04*
