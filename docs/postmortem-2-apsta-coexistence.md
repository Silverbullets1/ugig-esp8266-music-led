# Postmortem #2 — ESP8266 WiFi-Drop: AP+STA Coexistence Association Loss

**Case:** ESP8266 running WiFi in **STA+AP coexistence mode** (soft-AP for provisioning + STA for home network) randomly drops its STA association every 30–120 seconds. Station reconnects, works for a while, drops again. Soft-AP side stays up the whole time — only the *station* link dies.

**Impact:** MQTT stream gaps, OTA windows missed, dashboard shows device flapping. Customer originally shipped coexistence mode "because it worked in the demo."

---

## 1. Symptoms

| Observation | Detail |
|---|---|
| STA drops | Every 30–120 s under load, longer idle gaps when quiet |
| `reason` in disconnect event | `REASON_BEACON_TIMEOUT` (200) or `REASON_ASSOC_LEAVE` (8) |
| Soft-AP | Never drops — clients stay associated the whole time |
| RSSI | -55 to -65 dBm (healthy) at time of drop — not a signal issue |
| After drop | Reconnect takes 1.5–4 s (full scan) then works again |
| Pattern | Drops cluster when a phone/laptop actively probes the soft-AP side |

The clustering around soft-AP client activity was the key clue. A standalone STA sketch on identical hardware/power/network never dropped.

## 2. Root cause

ESP8266 is a **single-radio** chip. STA+AP "coexistence" is time-multiplexed on one PHY — both virtual interfaces must share the same **channel** for the radio to serve them without channel switching. The firmware had three interacting faults:

1. **Channel mismatch.** The soft-AP was started *before* `WiFi.begin()` on a hardcoded channel 1, while the home AP was on channel 6. Once STA associated on ch 6, every soft-AP client probe / beacon interval forced the PHY to hop back to ch 1. During those hops the STA misses AP beacons; missing `N` consecutive beacons fires `REASON_BEACON_TIMEOUT` and the STA association dies. This is the drop.

2. **Soft-AP config not persisted after STA connect.** The code called `WiFi.softAP(...)` at boot, then `WiFi.begin(...)`. The SDK's STA connect path reconfigures the radio and the soft-AP silently lands on the STA's channel — but any *client probe* during the negotiation window triggered re-scans (`wifi_station_scan`), extending beacon-miss windows.

3. **Aggressive reconnect loop masking the cause.** The sketch called `WiFi.reconnect()` from a 5 s timer tick on any disconnect. Each forced reconnect re-scanned all 13 channels (soft-AP still alive → more hop pressure) → the "works for a while then drops" cycle.

Channel mismatch is the primary fault; 2 and 3 are amplifiers that made the drop pattern look random.

## 3. Fix

Three changes, all in STA setup + reconnect logic:

```cpp
// FAULT 1 FIX: derive soft-AP channel from the STA network (or lock both to one channel).
// Do NOT hardcode soft-AP channel different from the AP you join.
void startCoexistence(const char* staSsid, const char* staPass,
                      const char* apSsid, const char* apPass) {
  // 1) Associate STA first, on its real channel
  WiFi.mode(WIFI_STA);
  WiFi.begin(staSsid, staPass);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  // 2) NOW bring up soft-AP ON THE SAME CHANNEL the STA is on (read it back)
  uint8_t ch = WiFi.channel();               // e.g. 6
  WiFi.softAP(apSsid, apPass, ch, 0, 4);     // channel = STA channel, max 4 clients

  // 3) Keep both alive; SDK then time-shares one channel — no beacon misses
  WiFi.setSleepMode(WIFI_NONE_SLEEP);        // modem sleep also breaks coexistence timing
}
```

```cpp
// FAULT 2/3 FIX: event-driven reconnect with backoff — never blind reconnect()
#include <Ticker.h>
Ticker reconTicker;
uint32_t bootProbeMuteUntil = 0;

void onStationDisconnect(const WiFiEventStationModeDisconnected &evt) {
  // log reason for postmortem data
  Serial.printf("[wifi] drop reason=%d rssi=%d\n", evt.reason, WiFi.RSSI());
  // backoff reconnect: 2s -> 4s -> 8s -> 16s (cap 30s), NOT fixed 5s hammering
  static uint32_t backoff = 2000;
  reconTicker.once_ms(backoff, []() {
    // mute soft-AP probes during re-scan to shrink the beacon-miss window
    if (WiFi.getMode() & WIFI_AP) WiFi.softAPdisconnect(false);
    WiFi.reconnect();
    reconTicker.once_ms(1000, []() {
      // restore soft-AP on the STA channel after re-association
      if (WiFi.status() == WL_CONNECTED) WiFi.softAP(AP_SSID, AP_PASS, WiFi.channel(), 0, 4);
    });
    backoff = min<uint32_t>(backoff * 2, 30000);
  });
}
```

```ini
; FAULT 1 companion: also pin the upstream AP if you control it (office/lab APs):
;   - lock AP to a fixed channel (6 here)
;   - disable "band steering"/"802.11k/v" on that SSID for the ESP's MAC — steering
;     forces re-association storms that coexistence mode cannot survive
```

**Diff summary (old → new):**

| Line | Old | New |
|---|---|---|
| init order | `softAP()` → `begin()` | `begin()` → wait → `softAP(ch=WiFi.channel())` |
| soft-AP channel | hardcoded `1` | `WiFi.channel()` (dynamic) |
| sleep mode | `WIFI_MODEM_SLEEP` (default) | `WIFI_NONE_SLEEP` while in coexistence |
| reconnect | `WiFi.reconnect()` every 5 s tick | event-driven + exponential backoff 2–30 s |
| reconnect side effects | none | mute soft-AP during re-scan, restore after |

## 4. Verification

| Step | Method | Result (before → after) |
|---|---|---|
| Drop rate | 24 h soak, MQTT keepalive misses counted | 41 drops → **0 drops** |
| Soft-AP probes | iperf/phone probing AP while streaming | probes no longer correlate with drops |
| Beacon timeout | serial `reason=` logging | zero `REASON_BEACON_TIMEOUT` in 24 h |
| Reconnect time | forced RF jam test (2.4 s) | 1.5–4 s full-scan → 0.9 s cached-channel reconnect |
| Power | USB meter at STA+AP idle | 78 mA modem-sleep → 96 mA none-sleep (accepted trade-off; documented) |

**Verification note:** with coexistence on a single radio, `WIFI_NONE_SLEEP` is mandatory — this raises idle current ~20 mA. For battery builds the correct answer is *don't use coexistence*; run STA-only with ESP Touch / SmartConfig provisioning instead. That variant is documented in the same repo under `firmware/` notes.

---

## 5. Hardening checklist (carried forward)

- [x] Never start soft-AP on a hardcoded channel in coexistence builds
- [x] Read back `WiFi.channel()` and align soft-AP after every (re)association
- [x] Event-driven reconnect + backoff; never a fixed-interval `reconnect()` hammer
- [x] Log `evt.reason` + RSSI on every disconnect — it turns guessing into data
- [x] Document the power cost of `WIFI_NONE_SLEEP` vs the battery alternative

*Case covered as an alternative to captive-portal redirect loop and BSSID lock fix per ticket spec. Template extended from postmortem #1.*
