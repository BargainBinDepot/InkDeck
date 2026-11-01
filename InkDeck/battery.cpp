#include "battery.h"
#include "config.h"
#include "screen.h"
#include "ui.h"
#include "webui.h"
#include "clock.h"
#include "esp_sleep.h"
#include "sound.h"
#include "storage.h"

namespace Battery {

bool usbSensed() { return BATT_MONITOR && PIN_USB_SENSE >= 0; }

#if BATT_MONITOR

static float vSmooth = 0;          // 0 = no reading yet
static int   bars = -1;
static bool  usb = false;
static int   lowReadings = 0;      // readings in a row at or below BATT_SHUTDOWN_V
static int   goneReadings = 0;     // readings in a row with no battery voltage
static uint32_t lastRead = 0;

// Bar thresholds (resting LiPo voltage, light load). Roughly:
// >= 3.95 V ~75-100%, >= 3.82 V ~50%, >= 3.73 V ~25%, >= 3.62 V ~10%, below = empty
static const float LEVEL_V[4] = { 3.62f, 3.73f, 3.82f, 3.95f };
static const float HYST = 0.03f;   // a bar comes back only 30 mV above its threshold

static float pinV = 0;              // last raw reading at PIN_BATT_ADC (for About / the log)
static float calF = BATT_CAL;       // multimeter volts / measured volts (System > Battery)

static float readOnce() {
  // Average many samples; analogReadMilliVolts uses the chip's factory calibration
  uint32_t sum = 0;
  for (int i = 0; i < 32; i++) sum += analogReadMilliVolts(PIN_BATT_ADC);
  pinV = sum / 32.0f / 1000.0f;
  return pinV * BATT_DIVIDER * calF;
}

static bool readUsb() {
#if PIN_USB_SENSE >= 0
  return digitalRead(PIN_USB_SENSE) == HIGH;
#else
  return false;
#endif
}

static int barsFor(float v) {
  int b = 0;
  while (b < 4 && v >= LEVEL_V[b]) b++;
  return b;
}

void measureNow() {
  const float v = readOnce();
  usb = readUsb();
  lastRead = millis();
  Serial.printf("[batt] GPIO %d = %.3f V -> battery %.2f V\n", PIN_BATT_ADC, pinV, v);
  // No battery voltage (switch off, nothing wired): hide the meter only after 3 such
  // readings in a row, and show it again only above 2.8 V, so it can't blink on and off
  if (v < 2.5f || (bars < 0 && v < 2.8f)) {
    if (bars < 0 || ++goneReadings >= 3) { vSmooth = 0; bars = -1; lowReadings = 0; goneReadings = 0; }
    return;
  }
  goneReadings = 0;
  // Smooth: the first reading is taken as is, later ones move it 25% of the way
  vSmooth = vSmooth == 0 ? v : vSmooth + 0.25f * (v - vSmooth);

  const int b = barsFor(vSmooth);
  if (bars < 0 || usb || b < bars) bars = b;         // on battery the level only falls...
  else if (b > bars && vSmooth >= LEVEL_V[bars] + HYST) bars = b;   // ...unless clearly higher (e.g. after a charge)

  lowReadings = (!usb && v <= BATT_SHUTDOWN_V) ? lowReadings + 1 : 0;
}

bool  present()  { return bars >= 0; }
int   level()    { return bars; }
float volts()    { return vSmooth; }
bool  charging() { return usb; }
bool  critical() { return lowReadings >= 2; }       // two readings in a row: not just a spike
float pinVolts() { return pinV; }
float cal()      { return calF; }

void loadSettings() {
  String cfg;
  if (Storage::readText(BATT_CONFIG_FILE, cfg, 64)) {
    const int p = cfg.indexOf("cal=");
    const float f = p >= 0 ? cfg.substring(p + 4).toFloat() : 0;
    if (f > 0.7f && f < 1.3f) calF = f;
  }
  if (calF != BATT_CAL) { Serial.printf("[batt] calibration x%.3f\n", calF); vSmooth = 0; measureNow(); }
}

void nudgeCal(int dir) {
  if (vSmooth <= 0) return;
  const float f = calF * (vSmooth + 0.01f * dir) / vSmooth;
  if (f < 0.7f || f > 1.3f) return;
  vSmooth *= f / calF;                               // shown at once, without waiting for a reading
  calF = f;
  bars = barsFor(vSmooth);                           // and the meter follows straight away
  Storage::writeText(BATT_CONFIG_FILE, "cal=" + String(calF, 4) + "\n");
}
uint32_t sinceReadSec() { return lastRead ? (millis() - lastRead) / 1000 : 0; }

// Typical LiPo resting voltage at each 10% (under the light load InkDeck draws)
int percent() {
  if (!present()) return -1;
  static const float V[11] = { 3.30f, 3.60f, 3.68f, 3.73f, 3.77f, 3.80f, 3.84f, 3.89f, 3.96f, 4.06f, 4.18f };
  if (vSmooth <= V[0]) return 0;
  for (int i = 1; i < 11; i++)
    if (vSmooth < V[i]) return (int)((i - 1) * 10 + 10 * (vSmooth - V[i - 1]) / (V[i] - V[i - 1]) + 0.5f);
  return 100;
}

String label() {
  if (!present()) return "none seen (GPIO " + String(PIN_BATT_ADC) + " = " + String(pinV, 2) + " V)";
  static const char* names[] = { "empty", "1/4", "1/2", "3/4", "full" };
  return String(vSmooth, 2) + " V, " + names[bars] + (usb ? ", charging" : "");
}

// "Battery empty" on the panel (it stays visible without power), then deep sleep.
// Wakes every BATT_RECHECK_MIN minutes, or when USB power arrives if that's wired.
static void shutDown(bool drawScreen) {
  Serial.printf("[batt] %.2f V: empty, shutting down\n", vSmooth);
  Sound::stop();
  if (drawScreen) {
    using namespace UI;
    screen.clear();
    screen.textCentered(SCREEN_W / 2, SCREEN_H / 2 - 14 * T, "Battery empty", 2 * T);
    screen.textCentered(SCREEN_W / 2, SCREEN_H / 2 + 6 * T, "Charge InkDeck to use it again", T);
    screen.refresh(Refresh::Full);
    screen.sleep();
  }
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)BATT_RECHECK_MIN * 60ULL * 1000000ULL);
#if PIN_USB_SENSE >= 0
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USB_SENSE, 1);
#endif
  esp_deep_sleep_start();
}

void begin() {
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);   // 0..~3.1 V at the pin
#if PIN_USB_SENSE >= 0
  pinMode(PIN_USB_SENSE, INPUT);
#endif
  measureNow();
  Serial.printf("[batt] %s\n", label().c_str());

  // Woken from the empty-battery sleep: unless it has been charged a bit, go
  // straight back without touching the screen (it still says "Battery empty")
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED && present() && !usb &&
      vSmooth < BATT_SHUTDOWN_V + 0.10f)
    shutDown(false);
}

void loop() {
  // Every BATT_READ_SEC, and not while WiFi is on: its current spikes pull the voltage down
  if (millis() - lastRead < BATT_READ_SEC * 1000UL && lastRead) return;
  if (WebUI::mode() != WebUI::Mode::Off || Clock::busy()) { lastRead = millis(); return; }
  measureNow();
  if (critical()) shutDown(true);
}

#else  // BATT_MONITOR 0: no meter

void begin() {}
void loop() {}
void measureNow() {}
bool  present()  { return false; }
int   level()    { return -1; }
float volts()    { return 0; }
bool  charging() { return false; }
bool  critical() { return false; }
String label()   { return "not monitored"; }
int   percent()  { return -1; }
float pinVolts() { return 0; }
void loadSettings() {}
float cal() { return 1; }
void nudgeCal(int) {}
uint32_t sinceReadSec() { return 0; }

#endif

}
