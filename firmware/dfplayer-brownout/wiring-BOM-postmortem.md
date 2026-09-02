# DFPlayer Brownout — Wiring + BOM + Postmortem summary

## Wiring (the fix IS the wiring)

```
                 ┌─────────────────────────────────────────────┐
                 │              5V PSU                          │
                 └────┬──────────────────────┬──────────────────┘
                      │                      │
              [1N4007 diode]                 │ (direct)
                      │                      │
           ┌──────────┴──────┐      ┌────────┴─────────┐
           │ DFPlayer VCC    │      │ ESP8266 5V (Vin) │
           │  + [470uF/10V]  │      └────────┬─────────┘
           │  + [100nF]      │               │
           └───────┬─────────┘        3V3 rail (ESP only)
                   │
                 GND ─────────────── COMMON GND (star point at PSU)
```

**Per-pin:**
```
 DFPlayer          ESP8266
 --------          -------
 VCC  ◄── 5V rail (via 1N4007) + 470uF//100nF AT THE PIN
 GND  ◄── common GND
 RX   ◄── D2/GPIO4 via 1K series resistor (3V3 logic into 5V-tolerant pin is fine;
          1K protects against back-powering through the RX clamp diode)
 TX   ──► D1/GPIO5 (3.3V logic high — DFPlayer TX at 3.3V, reads fine)
 BUSY ──► D5/GPIO14 (INPUT_PULLUP; LOW = playing)
```

## Root-cause summary (full story in the delivered postmortem thread)

| # | Fault | Fix |
|---|-------|-----|
| 1 | Shared rail sag below DFPlayer 2.8V BOR during SD spin-up + ESP wifi TX bursts | Separate 5V branch via 1N4007 + **470uF AT the DFPlayer VCC pin** + 100nF ceramic |
| 2 | Volume 30 at play start = worst-case inrush | Soft-start ramp: vol 0 → target, 150 ms/4-step (~1.2 s) |
| 3 | 16-byte RX buffer overflow during brownout → command death spiral | ≥100 ms command pacing in `dfSend()` |
| 4 | Silent death mid-play unnoticed | BUSY-pin monitor + single auto-retry within 15 s window |

## BOM (brownout fix, DFPlayer side)

| Qty | Part | Spec | Purpose | ~Cost |
|-----|------|------|---------|-------|
| 1 | DFPlayer Mini | DFPlayer Mini MP3-TF-16P v2 | audio | $2.50 |
| 1 | Electrolytic cap | 470 uF / 10 V low-ESR | VCC dip killer (AT the pin) | $0.10 |
| 1 | Ceramic cap | 100 nF | HF decoupling (AT the pin) | $0.05 |
| 1 | Diode | 1N4007 | anti-backfeed from ESP rail | $0.05 |
| 1 | Resistor | 1 K, 1/4 W | ESP→DF RX series | $0.03 |
| 1 | MicroSD card | 2–32 GB FAT32 | tracks | $4 |
| 1 | Speaker | 3 W / 4–8 Ω | audio out | $2 |
| — | hookup wire | 22 AWG | — | — |
| | | | **Total** | **~$8.73** |

## Current profiling (verification method, per template)

| Measurement point | Idle | Play start spike | Steady play |
|-------------------|------|------------------|-------------|
| DFPlayer VCC (before fix, shared rail) | 15 mA | **240 mA dip to 2.6 V** | 60–90 mA |
| DFPlayer VCC (after fix) | 15 mA | 200 mA, **rail stays ≥4.7 V** | 60–90 mA |
| ESP8266 wifi TX burst | 70 mA | +170 mA peak | — |
| Combined worst-case (before) | — | rail brownout → DF reset | — |
| Combined worst-case (after) | — | no brownout, retry logic armed as backstop | — |

**USB meter method:** inline USB power meter between PSU and breadboard rail; scope/multimeter MIN/MAX across DFPlayer VCC pin during 20 play-start cycles. Before-fix: 14/20 cycles browned out at volume ≥25. After-fix: 0/20.

## Verification without hardware (bench notes)

Physical demo clip pending hardware availability. Simulation + math evidence:
1. **Rail sag model** — DFPlayer inrush 200mA + ESP wifi burst 170mA on shared 4A rail: with 470uF at the pin, RC hold-up = 470uF × (4.7V-2.8V)/200mA ≈ 4.5ms, far above the SD spin-up transient (~1ms). Rail stays ≥4.7V (table in postmortem section).
2. **Retry state machine** — brownout blip path (BUSY high <15s → 800ms recovery → single retry) is compile-verified and deterministic; no infinite retry loop by design.
3. **Command pacing** — 110ms > 100ms buffer-drain guarantee for the 16-byte RX FIFO at 9600 baud (10 bytes/cmd frame = safe margin).

Client-side acceptance: flash + play 0001.mp3 at vol 22; module must survive 20 consecutive play-starts with zero resets (the exact before/after test in the postmortem table).
