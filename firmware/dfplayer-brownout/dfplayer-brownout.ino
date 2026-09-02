/*
 * DFPlayer-Mini Brownout Fix — ESP8266
 * Part of Ticket #2. Extends repo: github.com/Silverbullets1/ugig-esp8266-music-led
 *
 * SYMPTOM (this postmortem case):
 *   DFPlayer Mini resets/reboots mid-play — classic brownout. Usually when volume
 *   is set high at play start, or on track transitions. Serial spam "SD fail" or
 *   the module plays 1-2 s then dies. Power rail dips below DFPlayer's 2.8V BOR.
 *
 * ROOT CAUSE (per template: bisect boot vs runtime, meter the rail):
 *   1. DFPlayer draws current SPIKES up to ~200mA when starting a track
 *      (SD card spin-up + DAC charge) — on a rail shared with ESP8266 wifi bursts
 *      (WiFi TX ~170mA peaks), the shared 3V3/5V rail sags below BOR.
 *   2. ESP8266 hardware Serial TX floods DFPlayer's 16-byte RX buffer when
 *      commands queue up during the brownout window -> watchdog starvation loop.
 *   3. Volume set to max (30) BEFORE play start = worst-case inrush.
 *
 * FIX (this sketch + wiring):
 *   - HW: 470uF electrolytic + 100nF ceramic AT DFPlayer VCC pin (not at PSU)
 *   - HW: separate 5V rail for DFPlayer, common GND, 1N4007 anti-backfeed
 *   - SW: soft-start volume ramp (vol 0 -> target over ~1.2s AFTER play cmd)
 *   - SW: busy-pin monitoring + auto-recover single retry on brownout blip
 *   - SW: command pacing >= 100ms apart (16-byte buffer never overflows)
 */

#include <Arduino.h>
#include <SoftwareSerial.h>

#define DF_RX        5        // D1 — ESP receives DFPlayer TX
#define DF_TX        4        // D2 — ESP sends DFPlayer RX (1K series recommended)
#define DF_BUSY      14       // D5 — DFPlayer BUSY (LOW = playing)

SoftwareSerial dfSerial(DF_RX, DF_TX);

// ---- DFPlayer command framing ----
static const uint8_t CMD_PLAY       = 0x03;
static const uint8_t CMD_VOL        = 0x06;
static const uint8_t CMD_TRACK      = 0x03;  // with arg
static uint8_t targetVol = 22;               // out of 30 — never start at 30
static uint8_t currentVol = 0;               // soft-start begins silent

static uint32_t lastCmdMs = 0;
static const uint16_t CMD_PACE_MS = 110;     // >100ms: 16-byte RX buffer safe

static void dfSend(uint8_t cmd, uint8_t hi = 0, uint8_t lo = 0) {
  // pace: guarantee >= CMD_PACE_MS between command STARTS
  uint32_t now = millis();
  if (now - lastCmdMs < CMD_PACE_MS) delay(CMD_PACE_MS - (now - lastCmdMs));
  lastCmdMs = millis();

  uint8_t buf[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, hi, lo, 0x00, 0x00, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) sum += buf[i];
  sum = -sum;
  buf[8] = sum >> 8; buf[7] = sum & 0xFF;   // note: DFPlayer checksum is (0xFFFF - sum)
  // correct checksum: two's complement
  uint16_t chk = 0;
  for (int i = 1; i < 7; i++) chk += buf[i];
  chk = 0xFFFF - chk + 1;
  buf[7] = chk >> 8; buf[8] = chk & 0xFF;
  dfSerial.write(buf, 10);
}

// soft-start: bring volume up in steps AFTER play begins (inrush spreads out)
static void softStartVolume() {
  currentVol = 0;
  dfSend(CMD_VOL, 0, 0);                    // start silent
  while (currentVol < targetVol) {
    uint8_t step = (targetVol - currentVol > 4) ? 4 : (targetVol - currentVol);
    currentVol += step;
    dfSend(CMD_VOL, 0, currentVol);
    delay(150);                              // 150ms/step, ~1.2s total ramp
  }
}

static bool dfPlaying() {
  return digitalRead(DF_BUSY) == LOW;        // BUSY LOW == playing
}

// brownout watchdog: if BUSY went HIGH unexpectedly shortly after play start,
// the module browned out — ONE clean retry (full soft-start again)
static uint32_t playStartMs = 0;
static bool retried = false;

static void startTrack(uint16_t track) {
  dfSend(CMD_TRACK, track >> 8, track & 0xFF);
  delay(50);
  softStartVolume();
  playStartMs = millis();
  retried = false;
}

void setup() {
  pinMode(DF_BUSY, INPUT_PULLUP);
  dfSerial.begin(9600);
  Serial.begin(115200);
  delay(1200);                                // DFPlayer boot time (SD init)
  dfSend(CMD_VOL, 0, 0);                      // park silent
  startTrack(1);                              // demo: play 0001.mp3
}

void loop() {
  // brownout blip detector: within 15s of start, BUSY high = died mid-play
  if (dfPlaying() == false && playStartMs != 0) {
    if (millis() - playStartMs < 15000 && !retried) {
      Serial.println("[dfplayer] brownout blip detected — soft-start retry");
      delay(800);                             // let the rail recover
      startTrack(1);
      retried = true;
    } else if (millis() - playStartMs >= 15000) {
      playStartMs = 0;                        // natural end of track — no retry
    }
  }
  delay(50);
}
