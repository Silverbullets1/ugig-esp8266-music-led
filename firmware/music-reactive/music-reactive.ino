/*
 * music-reactive.ino — Beat-Reactive WS2812B Strip for ESP8266 (NodeMCU / Wemos D1 mini)
 * Part of DevilX starter ticket for Kachang Sia (UGIG gig: ESP8266/Arduino build & fix)
 *
 * What it does:
 *   - Samples audio from a MAX4466/MAX9814 electret mic module on A0 (DC-biased, ~1.25V idle)
 *   - Auto-calibrates noise floor in the first 3 seconds (no pot tuning needed)
 *   - Detects beats via adaptive threshold (rolling average + k * stddev)
 *   - Beats trigger a brightness/color pulse; continuous loudness drives base color
 *   - Non-blocking: no delay() in loop; all timing via millis()
 *
 * Wiring (NodeMCU / Wemos D1 mini):
 *   MAX4466  OUT  -> A0
 *   MAX4466  VCC -> 3V3  (quieter than 5V; module has its own rail-noise filter)
 *   MAX4466  GND -> GND
 *   WS2812B  DIN -> D4 (GPIO2) via 330R series resistor
 *   WS2812B  5V  -> 5V (external supply recommended above ~15 LEDs; common GND!)
 *   WS2812B  GND -> GND
 *   OPTIONAL: 1000uF cap across WS2812B 5V/GND at the strip input
 *
 * Serial: 115200 baud. First 3 s = calibration (keep quiet), then live levels print every 500 ms.
 */

#include <ESP8266WiFi.h>   // disable WiFi radio noise + power draw
#include <FastLED.h>

#define LED_PIN     D4      // GPIO2 on NodeMCU/D1 mini
#define NUM_LEDS    16      // starter scale; strip scales cleanly up to 60
#define MIC_PIN     A0
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS  160     // ~0.8A max on 60 LEDs @5V; 16 LEDs is safe on USB

// ---- Beat detection tuning ----
#define CALIB_MS      3000UL  // noise-floor calibration window
#define BEAT_K        2.2f    // beats fire when level > avg + K*stddev
#define BEAT_COOLDOWN 180UL   // ms between beats (filters ringing)
#define PULSE_DECAY   0.86f   // per-frame pulse energy multiplier
#define FRAME_MS      8UL     // ~125 fps target

CRGB leds[NUM_LEDS];

// rolling statistics
static float  s_avg = 2.0f;    // will be overwritten by calibration
static float  s_std = 0.15f;
static float  s_min = 1023.f, s_max = 0.f;

// beat state
static float       s_pulse   = 0.f;      // decaying pulse energy 0..1+
static uint32_t    s_lastBeat = 0;
static uint32_t    s_frameLast = 0;
static bool        s_calibrating = true;
static uint32_t    s_calStart;
static float       s_calSum = 0.f, s_calSqSum = 0.f;
static uint32_t    s_calN = 0;
static float       s_levelSmooth = 0.f;
static uint8_t     s_hue = 140;        // base hue drifts with loudness

float readMicLevel() {
  // peak-to-peak over a short window = robust loudness proxy
  const int N = 24;
  int mn = 1023, mx = 0;
  for (int i = 0; i < N; i++) {
    int v = analogRead(MIC_PIN);
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delayMicroseconds(200);
  }
  return (float)(mx - mn);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);          // RF off: cleaner ADC + lower brownout risk
  WiFi.forceSleepBegin();

  pinMode(LED_PIN, OUTPUT);
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  // boot wipe so dead-pixel testing is possible without sound
  fill_solid(leds, NUM_LEDS, CRGB(0, 8, 0));
  FastLED.show();

  s_calStart = millis();
  Serial.println(F("\n[music-reactive] calibrating noise floor for 3s — keep quiet..."));
}

void loop() {
  uint32_t now = millis();

  // ---- calibration phase ----
  if (s_calibrating) {
    float lvl = readMicLevel();
    s_calSum += lvl; s_calSqSum += lvl * lvl; s_calN++;
    if (lvl < s_min) s_min = lvl;
    if (lvl > s_max) s_max = lvl;
    if (now - s_calStart >= CALIB_MS) {
      s_avg = s_calSum / s_calN;
      float var = s_calSqSum / s_calN - s_avg * s_avg;
      s_std = (var > 0.f) ? sqrtf(var) : 0.05f;
      s_calibrating = false;
      Serial.printf("[music-reactive] calibrated: avg=%.1f std=%.1f min=%.1f max=%.1f\n",
                    s_avg, s_std, s_min, s_max);
    }
    return;
  }

  // ---- frame gate (non-blocking) ----
  if (now - s_frameLast < FRAME_MS) return;
  s_frameLast = now;

  float lvl = readMicLevel();
  s_levelSmooth = 0.7f * s_levelSmooth + 0.3f * lvl;

  // ---- beat detect: level above adaptive threshold ----
  float threshold = s_avg + BEAT_K * s_std + 4.f;   // +4 ADC counts guard
  bool beat = (lvl > threshold) && (now - s_lastBeat > BEAT_COOLDOWN);
  if (beat) {
    s_lastBeat = now;
    float over = (lvl - threshold) / (s_std + 1.f);
    s_pulse = min(1.6f, s_pulse + 0.55f + over * 0.25f);  // harder hits = brighter pop
  }
  s_pulse *= PULSE_DECAY;
  if (s_pulse < 0.02f) s_pulse = 0.f;

  // ---- color: base hue drifts with sustained loudness ----
  float loud = constrain((s_levelSmooth - s_avg) / (3.f * s_std + 1.f), 0.f, 1.f);
  s_hue += (uint8_t)(loud * 3.f);
  uint8_t sat = 255 - (uint8_t)(loud * 60.f);   // louder = whiter

  // ---- render: dim base + beat pulse traveling brightness ----
  uint8_t baseV = 26 + (uint8_t)(loud * 40.f);
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    float pos = (float)i / (float)(NUM_LEDS - 1);
    float wave = s_pulse * (1.f - 0.35f * pos);           // center-out emphasis
    uint8_t v = baseV + (uint8_t)min(229.f, wave * 229.f);
    leds[i] = CHSV(s_hue + (uint8_t)(pos * 30.f), sat, v);
  }
  FastLED.show();

  // ---- telemetry every 500ms ----
  static uint32_t s_tel = 0;
  if (now - s_tel > 500UL) {
    s_tel = now;
    Serial.printf("lvl=%.0f avg=%.1f thr=%.1f beat=%s pulse=%.2f\n",
                  lvl, s_avg, threshold, beat ? "Y" : "n", s_pulse);
  }

  // ---- slow noise-floor tracking (drift compensation) ----
  static uint32_t s_nf = 0;
  if (now - s_nf > 2000UL && !beat) {
    s_nf = now;
    s_avg = 0.97f * s_avg + 0.03f * lvl;   // EMA only on non-beat frames
  }
}
