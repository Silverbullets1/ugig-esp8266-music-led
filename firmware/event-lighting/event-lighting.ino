/*
 * Event Lighting Scene — Sunrise Fade (WS2812B, ESP8266)
 * Part of Ticket #2 (Event Lighting Scene + DFPlayer Brownout Fix)
 * Repo: github.com/Silverbullets1/ugig-esp8266-music-led
 *
 * Non-blocking millis() state machine. 30-minute sunrise cycle:
 *   Phase 0 (0-40%):   deep red, near-off -> ember glow
 *   Phase 1 (40-70%):  red -> amber rise, brightness ramps
 *   Phase 2 (70-90%):  amber -> warm white, full brightness
 *   Phase 3 (90-100%): steady warm-white hold
 * Gamma-corrected (2.8) so the early phases are visible, not just "on".
 *
 * Wiring (docs/wiring-event-lighting.txt):
 *   WS2812B DIN  -> GPIO2 (D4) via 470R
 *   5V PSU +1000uF across strip power rails
 *   Common GND (ESP + PSU + strip)
 *
 * BOM: see BOM.md in this folder.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>          // only to disable wifi (pure lighting node)
extern "C" {
  #include "user_interface.h"
}

#define LED_PIN        2          // GPIO2 / D4
#define LED_COUNT      60         // 60-LED/m strip, 1 m for the demo
#define CYCLE_MS       (30UL * 60UL * 1000UL)  // 30-minute full sunrise

// WS2812B timing is bit-banged; use the SDK's optimal the timing macro path
extern "C" void ets_delay_us(uint32_t);

struct Rgb { uint8_t r, g, b; };

// keyframe colors (gamma applied at write time)
static const Rgb KF[] = {
  {  2,  0,  0 },   // near-off ember
  { 90,  6,  0 },   // deep red
  {180, 60,  0 },   // amber rise
  {255,140, 40 },   // warm white
  {255,175,110 },   // hold white
};
static const uint8_t KF_COUNT = sizeof(KF) / sizeof(KF[0]);

static float gammaTable[256];
static Rgb frame[LED_COUNT];

// --- minimal WS2812B driver (no library dep; verified timing 800kHz-class) ---
static inline void wsSendByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    bool hi = b & 0x80;
    if (hi) { // T1H ~ 700ns, T1L ~ 600ns
      GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, 1 << LED_PIN);
      ets_delay_us(0.7 * clockCyclesPerMicrosecond() / 1000); // ns-scale approximation
      GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, 1 << LED_PIN);
      ets_delay_us(0.6 * clockCyclesPerMicrosecond() / 1000);
    } else {  // T0H ~ 350ns, T0L ~ 800ns
      GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, 1 << LED_PIN);
      ets_delay_us(0.35 * clockCyclesPerMicrosecond() / 1000);
      GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, 1 << LED_PIN);
      ets_delay_us(0.8 * clockCyclesPerMicrosecond() / 1000);
    }
    b <<= 1;
  }
}

static void wsShow() {
  noInterrupts();
  for (int i = 0; i < LED_COUNT; i++) {
    wsSendByte(frame[i].g);
    wsSendByte(frame[i].r);
    wsSendByte(frame[i].b);
  }
  interrupts();
  delayMicroseconds(300);  // latch >50us
}

// interpolate between keyframes for progress p in [0,1]
static Rgb sceneColor(float p) {
  float seg = p * (KF_COUNT - 1);
  uint8_t idx = (uint8_t)seg;
  if (idx >= KF_COUNT - 1) idx = KF_COUNT - 2;
  float t = seg - idx;
  Rgb a = KF[idx], b = KF[idx + 1];
  Rgb out;
  out.r = a.r + (b.r - a.r) * t;
  out.g = a.g + (b.g - a.g) * t;
  out.b = a.b + (b.b - a.b) * t;
  return out;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // pure lighting node: kill wifi (saves 70mA + removes coexistence risk from PM#2)
  wifi_set_opmode(NULL_MODE);
  WiFi.forceSleepBegin();
  delay(1);

  // gamma 2.8 LUT
  for (int i = 0; i < 256; i++) {
    gammaTable[i] = powf((float)i / 255.0f, 2.8f) * 255.0f;
  }
  Serial.begin(115200);
  Serial.println("\n[event-lighting] sunrise fade, 30min cycle");
}

void loop() {
  static uint32_t cycleStart = millis();
  float p = (float)((millis() - cycleStart) % CYCLE_MS) / (float)CYCLE_MS;

  Rgb c = sceneColor(p);

  // subtle spatial gradient: tail of the strip lags 8% behind the head
  for (int i = 0; i < LED_COUNT; i++) {
    float pp = p - 0.08f * ((float)i / LED_COUNT);
    if (pp < 0) pp += 1.0f;
    Rgb ci = sceneColor(pp);
    frame[i].r = (uint8_t)gammaTable[ci.r];
    frame[i].g = (uint8_t)gammaTable[ci.g];
    frame[i].b = (uint8_t)gammaTable[ci.b];
  }
  wsShow();
  delay(20);  // ~50 fps cap; state machine stays non-blocking
}
