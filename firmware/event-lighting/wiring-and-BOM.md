# Event Lighting — Wiring Diagram + BOM

## Wiring (sunrise fade, WS2812B)

```
                    +5V PSU (4A)
                     |      |
        +1000uF ---- +      +-------------------+
        (16V, across strip power at strip INPUT end) |
                                                    |
  ESP8266 (NodeMCU/Wemos D1 mini)                   |
  ---------------                                   |
  3V3  --[not used by strip]                        |
  GND  ---------------------------------+           |
  D4/GPIO2 --[470R]---> DIN (strip) ----+-----------+
                                        |
  (strip: 5V | DIN | GND)               GND
```

ASCII layout:

```
   ESP8266                470R          WS2812B strip (60 LED/m, 1m)
  ┌─────────┐          ┌───┐      ┌──────────────────────────┐
  │     D4 ●├──────────┤   ├─────►│ DIN                      │
  │     GND ├──────────┴───┘◄─────│ GND ───┐                 │
  │         │                     │ 5V ◄───┼──┐              │
  └─────────┘                     └────────┼──┼──────────────┘
                                           │  │
                    +5V 4A PSU ────────────┘  │
                    PSU GND ──────────────────┘
                    (+1000uF/16V across 5V/GND at strip input)
```

**Rules that matter:**
1. **470R in series with DIN** at the strip input — kills ringing on the data line.
2. **1000uF electrolytic across 5V/GND at the strip input** — absorbs inrush when strip powers up (protects the first LED).
3. **Common ground** — ESP, PSU, strip all share GND. Data reference must match.
4. Power injection at BOTH ends if you extend past ~1.5 m (voltage droop makes the tail brown/orange).
5. Strip powered from PSU, **never from the ESP's 3V3 or USB 5V** (60 LEDs × 60 mA = 3.6 A worst case).

## BOM

| Qty | Part | Spec | Purpose | ~Cost |
|-----|------|------|---------|-------|
| 1 | ESP8266 dev board | Wemos D1 mini / NodeMCU | controller | $3 |
| 1 | WS2812B strip | 60 LED/m, 1 m, IP30 | lighting | $4 |
| 1 | PSU | 5 V, 4 A (20 W) | strip + ESP power | $6 |
| 1 | Electrolytic cap | 1000 uF / 16 V | strip inrush buffer | $0.20 |
| 1 | Resistor | 470 R, 1/4 W | data-line ring killer | $0.05 |
| 1 | Electrolytic cap | 470 uF / 10 V | ESP rail stability | $0.10 |
| — | hookup wire | 22 AWG silicone | power/data runs | $1 |
| — | breadboard or solder | — | assembly | — |
| | | | **Total** | **~$14.35** |

## Power budget

| State | Current |
|-------|---------|
| ESP8266 (wifi off, running scene) | ~35 mA |
| Strip — ember phase (near off) | ~60 mA |
| Strip — full warm white (peak) | ~2.8 A @ 60 LEDs (60 mA/LED worst) |
| **Design PSU** | 5 V / 4 A (headroom for full-white + losses) |

Scene never exceeds ~70% white in the hold phase, so typical draw is ~1.9 A. PSU sized for the worst case anyway.
