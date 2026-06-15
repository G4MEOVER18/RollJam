# RollJam Research

```
 ██████╗ ██╗  ██╗███╗   ███╗███████╗ ██████╗ ██╗   ██╗███████╗██████╗
██╔════╝ ██║  ██║████╗ ████║██╔════╝██╔═══██╗██║   ██║██╔════╝██╔══██╗
██║  ███╗███████║██╔████╔██║█████╗  ██║   ██║██║   ██║█████╗  ██████╔╝
██║   ██║╚════██║██║╚██╔╝██║██╔══╝  ██║   ██║╚██╗ ██╔╝██╔══╝  ██╔══██╗
╚██████╔╝     ██║██║ ╚═╝ ██║███████╗╚██████╔╝ ╚████╔╝ ███████╗██║  ██║
 ╚═════╝      ╚═╝╚═╝     ╚═╝╚══════╝ ╚═════╝   ╚═══╝  ╚══════╝╚═╝  ╚═╝
```

**Rolling Code Jam + Capture + Replay PoC** for Flipper Zero.

[![Firmware](https://img.shields.io/badge/Firmware-G4MEOVER--FW%20v1.0-blue)](https://github.com/G4MEOVER18/G4MEOVER-FW)
[![API](https://img.shields.io/badge/API-87.1%20mntm--012-green)](https://github.com/G4MEOVER18/G4MEOVER-FW)
[![Author](https://img.shields.io/badge/Author-G4MEOVER18-red)](https://github.com/G4MEOVER18)

---

## Attack Overview

The RollJam attack (Samy Kamkar, DEF CON 23) exploits rolling-code systems by:

1. **Jamming** the target frequency so the car never receives the keyfob's code
2. **Capturing** the code the keyfob transmitted (despite jamming)
3. **Replaying** the captured code later to unlock the car

```
Phase 1 — Jam + RX (180ms jam / 80ms RX cycles):
  Keyfob sends Code A → Car jammed, ignores it → Flipper captures Code A

Phase 2 — Same cycle:
  Keyfob sends Code B → Car jammed, ignores it → Flipper captures Code B

Phase 3 — Replay:
  Flipper transmits Code A → Car opens ✓
  Code B stays valid → attacker's spare key
```

### Implementation

- Interlaced jam+RX timing: 180ms OOK noise burst → 80ms RX window → repeat
- Signal capture: interrupt-driven, packed LevelDuration (bit31=level, bits0-30=duration µs)
- Signal replay: async TX directly from capture buffer
- Frequency: 433.92 MHz · Modulation: AM650 (OOK) · Internal CC1101 only

---

## Installation

Copy `rolljam_research.fap` to `/apps/Sub-GHz/` on your Flipper Zero SD card.

**Requires:** Flipper Zero with G4MEOVER-FW or Momentum firmware, internal CC1101.

---

## Building from source

```bash
ufbt          # in the rolljam/ directory
```

---

## Related projects

| Repo | Description |
|------|-------------|
| [RollLab](https://github.com/G4MEOVER18/RollLab) | Rollback / sync-window / signal analysis tool |
| [ProtoPirate](https://github.com/G4MEOVER18/ProtoPirate) | 27+ protocol decoder/encoder for Car Keyfobs |
| [G4MEOVER-FW](https://github.com/G4MEOVER18/G4MEOVER-FW) | Custom Flipper Zero firmware (Momentum fork) |

---

## Legal

For **authorized security testing, CTF competitions, and educational research only**.  
Use only on hardware you own or have explicit written permission to test.

---

## Support the project

- **Bitcoin:** `39vZWmnUwDReQ15BwqQXzyqVQ6U8LardEf`

**Kontakt:** [g4me.over.18@gmail.com](mailto:g4me.over.18@gmail.com)
- **PayPal:** [paypal.me/Freakbank1](https://paypal.me/Freakbank1)

---

**G4MEOVER18** · [GitHub](https://github.com/G4MEOVER18)
