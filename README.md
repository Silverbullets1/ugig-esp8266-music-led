# ESP8266 Music-Reactive LED — Starter Ticket (Proof of Work)

Beat-reactive WS2812B strip driver for ESP8266 (NodeMCU v2 / Wemos D1 mini).
**Compile-verified** with arduino-cli 1.5.1 + esp8266 core 3.1.2 + FastLED 3.10.5 (log in `docs/compile-log.txt`).

Delivered as the starter ticket for the *ESP8266/Arduino build & fix — event lighting, music-reactive LEDs, IoT* gig. This is the "1 music-reactive mode" milestone piece.

---

## What it does

- Samples a **MAX4466** (or MAX9814) electret mic on `A0` — peak-to-peak level per frame
- **Auto-calibrates** the noise floor during the first 3 s (no trimpot tuning)
- **Beat detection**: adaptive threshold = rolling mean + `2.2σ`, 180 ms cooldown
- Beats fire a **center-out brightness pulse**; sustained loudness drives hue drift + desaturation
- **Non-blocking loop** (no `delay()`), frame-gated at ~125 fps
- Noise floor **EMA-tracks** on non-beat frames → survives room changes without reboot
- WiFi radio disabled (cleaner ADC reference, lower brownout risk)

## Wiring

```
 MAX4466            ESP8266 (NodeMCU v2)
 ┌───────┐
 │  OUT ├────────────► A0
 │  VCC ├────────────► 3V3
 │  GND ├────────────► GND
 └───────┘

 WS2812B strip (16 LED starter)
 ┌───────┐
 │  DIN  ├───[330Ω]───► D4 (GPIO2)
 │  5V   ├────────────► 5V  (external supply >15 LEDs; common GND!)
 │  GND  ├────────────► GND
 └───────┘
 + 1000µF electrolytic across strip 5V/GND (recommended)
```

> ⚠️ Never power >15 WS2812B from USB. Full 60-LED build = external 5V ≥2A with **common ground**.

## BOM (starter rig)

| Qty | Part | Note |
|-----|------|------|
| 1 | NodeMCU v2 / Wemos D1 mini | ESP8266 |
| 1 | MAX4466 mic module | MAX9814 works, AGC pin → VCC |
| 1 | WS2812B strip (16–60 px) | 60 px/m or 30 px/m |
| 1 | 330 Ω resistor | data line series |
| 1 | 1000 µF 6.3 V cap | strip power buffering |
| – | 5V supply ≥2 A | for >15 LED builds |

## Build & flash

```bash
# toolchain (one-time)
arduino-cli config add board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index && arduino-cli core install esp8266:esp8266
arduino-cli lib install FastLED

# compile (verified)
cd firmware/music-reactive
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 .

# flash
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 -p /dev/ttyUSB0 .
```

Serial monitor @ **115200**: first 3 s prints calibration, then `lvl/avg/thr/beat/pulse` telemetry every 500 ms.

## Tuning cheat-sheet

| Symptom | Fix |
|---------|-----|
| No beats on bass | lower `BEAT_K` → `1.8`; mic gain pot up |
| Constant pulsing | raise `BEAT_K` → `2.8`; recalibrate in silence |
| Flicker at high brightness | brightness ↑ too high for supply → lower `BRIGHTNESS` |
| Random first beat after boot | calibration ran in noise → reboot quiet |

## Scaling to the $40 milestone build

This starter mode is the skeleton for the full gig scope:
- **Event lighting scenes** → swap the render block for scene state machines (same frame gate)
- **Music-reactive modes** → this file (beat + loudness pipelines already in place)
- **dfplayer current diagnosis** → brownout pattern from the postmortem applies (470 µF at AMS1117 output)

## Verification

- ✅ `arduino-cli compile` clean, no warnings — 244,432 B flash (23%), 28,936 B RAM (36%)
- ✅ Resource table in `docs/compile-log.txt`
- ✅ Wiring follows the exact brownout/sag methodology from the diagnostic postmortem shared in-chat

*Prepared by DevilX (github.com/Silverbullets1) — UGIG gig proof-of-work.*
