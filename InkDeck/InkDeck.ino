// =====================================================================
//  InkDeck firmware v0.21
//  ESP32-S3 + CardKB + e-paper (or the ST7789 TFT preview)
//
//  v0.1   display HAL, CardKB input, paged launcher, app framework
//  v0.1.1 e-paper backends (2.9 BWR / 2.9 BW / 3.7 BW), red accent layer
//  v0.1.2 fake-temperature fast refresh for the 2.9 BWR
//  v0.1.3 TFT previews the 3.7" (fit-to-screen or 1:1 crop), UI scales 2x on 3.7"
//  v0.2   microSD storage, Notes saved to /notes, Files browser/viewer
//  v0.3   Lua apps from /apps (sandboxed), Uploader app: WiFi + web page
//         with Apps / Notes / per-app / System tabs
//  v0.3.1 Lua long-long fix for ESP32 core 3.x
//  v0.3.2 Uploader scans for WiFi and joins from the device; hotspot is a manual fallback
//  v0.4   Text engine (book pagination), FreeMono fonts for apps, file.rename,
//         apps can ship their own web page as their Uploader tab (tab.page=)
//  v0.5   Home screen: pages scroll up/down, app icons (icon.bmp / built-in),
//         'i' = app info popup (version, author, description, size)
//  v0.6   Italic, bold-italic and 18pt bold fonts for apps; font metrics cached
//  v0.7   Fn key combos passed to apps, shared clipboard, Fn+Enter = app info,
//         Fn+C/V in Notes, Fn+V in the WiFi password box
//  v0.8   Browser editors in the Uploader: Notes (text) and apps with tab.editor=
//         (Markdown gets a live preview + toolbar)
//  v0.9   Real clock: NTP over WiFi, optional DS3231/PCF8563 RTC, timezone +
//         set-from-browser on the web page; apps sorted A-Z; Notes is now a
//         Lua app (shared /notes folder); sys.time() / sys.date() for apps
//  v0.10  Mac OS 7 look: pinstriped title bar on the device (title centred,
//         close box, clock); Uploader restyled as a Mac OS 7 window
//  v0.11  Files: Right opens, Left goes back, delete files/folders with a
//         confirmation (file counts); System split into About and Settings,
//         Clock settings (12/24-hour, time zone, sync now)
//  v0.12  Renamed InkDeck. Home screen: Finder-style window (info bar, scroll
//         bar), Tab menu, move apps, folders (/system/home.txt)
//  v0.12.1 Home screen: item-count bar removed, icons centred in their cells
//  v0.13  Mac OS 5/6-style 32x32 icons, pixel-doubled on the 3.7"
//  v0.14  sys.random() (hardware RNG), screen.image() for BMPs in apps
//  v0.15  Sleep after a minute without keys (light sleep, screen kept);
//         crash-safe saves with boot-time repair; file.read refuses files
//         over the limit instead of cutting them off
//  v0.16  Firmware updates over WiFi (Uploader > System), Previous firmware
//         (roll back) in Settings. Needs the 16M partition scheme.
//  v0.17  Pictures in books: \x02img\x02 markers in the text engine, screen.image
//         fitted into a rectangle, bounded image cache. Fixed page drift after
//         words longer than a line.
//  v0.18  WeAct 3.7" panel. Boot-time save check no longer crawls on cards
//         with big picture folders; app switches no longer force a full
//         refresh, automatic full refresh every 100 partials (was 10).
//  v0.19  Settings: "Full refresh after" N updates and "Full on app switch".
//         Keys typed while the screen refreshes are queued instead of lost.
//  v0.20  3.7": third refresh level, the deep clean (stock waveform, ~3 s).
//         Settings > "Deep clean screen", and "Full refresh type" Fast/Deep.
//  v0.21  Fixes: firmware updates no longer rejected when the bare tag comes
//         first; renaming to different capitals no longer deletes the file;
//         Files / Get Info quick on huge folders; no sleep while on USB.
//
//  Arduino IDE: Tools -> Partition Scheme -> "Huge APP (3MB No OTA/1MB SPIFFS)"
//  with Flash Size 4MB (the default partition is too small for WiFi + Lua).
// =====================================================================
#include "config.h"
#include "screen.h"
#include "keyboard.h"
#include "apps.h"
#include "storage.h"
#include "clock.h"
#include "power.h"
#include "ota.h"

// Lua's parser recurses; give the main loop more stack than the default 8 KB
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(32 * 1024);
#endif

// Every build carries this tag. The updater looks for it to make sure an
// uploaded .bin really is InkDeck firmware, and to read its version.
static const char FW_TAG[] __attribute__((used)) = "INKDECK-FW:" FW_VERSION;

// Keep a freshly updated firmware on probation until it has run for a bit
// (if the core's bootloader supports it, a crash before then rolls back)
bool verifyRollbackLater() { return true; }

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== " BRAND_NAME " " FW_VERSION " booting ===");
  Serial.println(FW_TAG);

  screen.begin();
  kb.begin();
  Storage::begin();
  screen.loadSettings();
  Clock::begin();
  Power::begin();
  AppMgr::begin();

  Serial.println("=== ready ===");
}

void loop() {
  // Every key typed meanwhile (e.g. during the last refresh), then one redraw for all of them
  while (uint8_t key = kb.poll()) {
    Power::noteInput();
    AppMgr::handleKey(key);
  }
  AppMgr::tick();

  static bool markedGood = false;              // ran 20 s without crashing: keep this firmware
  if (!markedGood && millis() > 20000) { Ota::markGood(); markedGood = true; }

  Power::loop();                 // sleeps after a minute without keys (blocks until a key wakes it)
  delay(2);
}
