#include "power.h"
#include "config.h"
#include "storage.h"
#include "screen.h"
#include "keyboard.h"
#include "apps.h"
#include "webui.h"
#include "clock.h"
#include "esp_sleep.h"
#if ARDUINO_USB_MODE
#include "driver/usb_serial_jtag.h"
#endif

namespace Power {

static uint32_t timeout = SLEEP_AFTER_SEC;
static uint32_t lastInput = 0;
static bool asleep = false;
static bool appAwake = false;

bool sleeping()          { return asleep; }
void keepAwake(bool on)  { appAwake = on; }
void noteInput()         { lastInput = millis(); }
uint32_t timeoutSec()    { return timeout; }

bool setTimeoutSec(uint32_t s) {
  timeout = s;
  lastInput = millis();
  return Storage::writeText(POWER_CONFIG_FILE, "sleep=" + String((unsigned long)s) + "\n");
}

void begin() {
  String cfg;
  if (Storage::readText(POWER_CONFIG_FILE, cfg, 128)) {
    const int p = cfg.indexOf("sleep=");
    if (p >= 0) timeout = strtoul(cfg.c_str() + p + 6, nullptr, 10);
  }
  lastInput = millis();
  Serial.printf("[power] sleep after %lu s%s\n", (unsigned long)timeout, timeout ? "" : " (never)");
}

// Plugged into a computer (not just a charger): light sleep turns the ESP32-S3's
// USB off, which drops the Serial Monitor and makes uploads fail
static bool usbHost() {
#if ARDUINO_USB_MODE
  return usb_serial_jtag_is_connected();
#else
  return false;
#endif
}

static bool mustStayAwake() {
  return appAwake
      || usbHost()
      || WebUI::mode() != WebUI::Mode::Off      // the Uploader's web page would drop
      || Clock::busy();                         // finishing an internet time sync
}

static void sleepNow() {
  Serial.println("[power] going to sleep");
  asleep = true;
  AppMgr::requestRedraw(Refresh::Partial);     // redraw once with "zZ" instead of the clock
  AppMgr::tick();
  screen.sleep();                              // panel off (e-paper keeps its image), backlight off
  Serial.flush();

  // Light sleep in short naps, checking the keyboard between them
  while (true) {
    esp_sleep_enable_timer_wakeup(SLEEP_POLL_MS * 1000ULL);
    esp_light_sleep_start();
    if (kb.poll()) break;                      // the waking key is swallowed
  }

  asleep = false;
  screen.wake();
  lastInput = millis();
  Serial.println("[power] awake");
  AppMgr::requestRedraw(Refresh::Partial);     // the real time comes back
}

void loop() {
  if (!timeout) return;
  if (mustStayAwake()) { lastInput = millis(); return; }
  if (millis() - lastInput >= timeout * 1000UL) sleepNow();
}

}
