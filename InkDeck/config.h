#pragma once
// =====================================================================
//  InkDeck — hardware config
//  Target: ESP32-S3 DevKitC-1 style board (N8R8 / N16R8)
//
//  Pins deliberately AVOIDED on the S3:
//    0, 3, 45, 46  strapping pins
//    19, 20        native USB (D-/D+)
//    26-32         SPI flash
//    33-37         octal PSRAM (R8 modules)
//    38 / 48       onboard RGB LED (depends on DevKitC revision)
//    43, 44        UART0 TX/RX
// =====================================================================

#define FW_VERSION "v0.22"
#define BRAND_NAME "InkDeck"

// ---------------- Which display is plugged in ----------------
#define PANEL_TFT_PREVIEW    1   // ST7789 2.8" TFT pretending to be an e-paper (see PREVIEW_TARGET)
#define PANEL_WEACT_290_BWR  2   // WeAct 2.9" black/white/red (GDEM029C90, SSD1680) - full refresh only, slow
#define PANEL_WEACT_290_BW   3   // WeAct 2.9" black/white (DEPG0290BS, SSD1680)
#define PANEL_WEACT_370_BW   4   // WeAct 3.7" black/white (GDEY037T03, UC8253) - the final panel

#define DISPLAY_PANEL   PANEL_WEACT_370_BW

// When the TFT is plugged in, which e-paper should it pretend to be?
#define PREVIEW_TARGET  PANEL_WEACT_370_BW

// ---------------- Derived: the panel the UI is laid out for ----------------
#if DISPLAY_PANEL == PANEL_TFT_PREVIEW
  #define TARGET_PANEL PREVIEW_TARGET
#else
  #define TARGET_PANEL DISPLAY_PANEL
#endif

#if TARGET_PANEL == PANEL_WEACT_370_BW
  #define SCREEN_W 416
  #define SCREEN_H 240
  #define TARGET_NAME "3.7 BW"
#elif TARGET_PANEL == PANEL_WEACT_290_BWR
  #define SCREEN_W 296
  #define SCREEN_H 128
  #define TARGET_NAME "2.9 BWR"
#else
  #define SCREEN_W 296
  #define SCREEN_H 128
  #define TARGET_NAME "2.9 BW"
#endif

#define TARGET_HAS_RED  (TARGET_PANEL == PANEL_WEACT_290_BWR)

// UI scale: 2x text/icons on the denser 3.7" panel so it stays readable
#define UI_SCALE  ((SCREEN_H >= 200) ? 2 : 1)

// ---------------- Display SPI bus (FSPI) ----------------
// Same wires for the TFT and every e-paper panel.
#define PIN_DISP_SCK   12   // e-paper "SCL"
#define PIN_DISP_MOSI  11   // e-paper "SDA"
#define PIN_DISP_CS    10
#define PIN_DISP_DC     9
#define PIN_DISP_RST    8   // e-paper "RES"
#define PIN_TFT_BL      7   // TFT backlight...
#define PIN_EPD_BUSY    7   // ...or e-paper BUSY (same pin, never both connected)

// ---------------- TFT-only settings ----------------
#define TFT_NATIVE_W   240
#define TFT_NATIVE_H   320
#define TFT_ROTATION   1          // landscape (320x240)
#define TFT_SPI_HZ     10000000   // 40 MHz was too fast over jumper wires
#define TFT_INVERT     false      // this panel is not inverted (true = IPS-style inversion)

// Preview mode when the target panel is bigger than the TFT (3.7" = 416x240):
//   1 = shrink the whole panel to fit (smoothed, ~77%)
//   0 = 1:1 pixels, cropped to 320x240 starting at TFT_CROP_X / TFT_CROP_Y
#define TFT_PREVIEW_FIT  1
#define TFT_CROP_X       0
#define TFT_CROP_Y       0

// ---------------- E-paper-only settings ----------------
#define EPD_ROTATION   1          // landscape; use 3 if the image is upside down

// 2.9" BWR only: "fake temperature" speed hack.
//   0 = stock waveform, 100 = fastest (weaker red / more ghosting)
#define EPD_FAST_TEMP_C  100

// ---------------- I2C bus (CardKB now, RTC later) ----------------
// NOTE: on this CardKB cable, YELLOW = SDA and WHITE = SCL
#define PIN_I2C_SDA    17
#define PIN_I2C_SCL    18
#define I2C_HZ         100000
#define CARDKB_ADDR    0x5F
#define KB_POLL_MS     10

// ---------------- microSD (own SPI bus: HSPI) ----------------
// Kept off the display bus, same idea as the e-reader.
#define PIN_SD_SCK     14
#define PIN_SD_MOSI    15
#define PIN_SD_MISO    16
#define PIN_SD_CS      21
#define SD_SPI_HZ      10000000   // conservative for jumper wires

// ---------------- Lua apps ----------------
#define LUA_MEM_LIMIT_KB   1024     // RAM cap per app (uses PSRAM when the board has it)
#define LUA_TIMEOUT_MS     3000     // one callback running longer than this is stopped
#define LUA_SOURCE_MAX     65536    // biggest main.lua we'll load
#define LUA_FILE_MAX       262144   // biggest file an app can read in one go
#define LUA_TICK_MS        100      // how often an app's tick() runs

// ---------------- WiFi / web uploader ----------------
// WiFi is only on while the Uploader app is open.
#define WIFI_CONFIG_FILE   "/system/wifi.txt"   // line 1 = SSID, line 2 = password (set from the web page)
#define WIFI_CONNECT_MS    15000                // give up on home WiFi after this and start the hotspot
#define AP_PASSWORD        "inkdeck1"           // hotspot password (8+ characters)
#define MDNS_NAME          "inkdeck"            // -> http://inkdeck.local

// ---------------- Clock ----------------
// Optional RTC module (DS3231 or PCF8563) goes on the I2C bus with the CardKB.
#define TIME_CONFIG_FILE   "/system/time.txt"
#define HOME_LAYOUT_FILE   "/system/home.txt"     // home screen order and folders
#define DEFAULT_TZ         "PST8PDT,M3.2.0,M11.1.0"   // Pacific; change it on the web page's System tab
#define AUTO_TIME_SYNC     1     // at boot, if the time is unknown, join the saved WiFi briefly to get it

// ---------------- Sleep ----------------
#define POWER_CONFIG_FILE  "/system/power.txt"
#define SLEEP_AFTER_SEC    60        // default; change in System > Settings > Sleep (0 = never)
#define SLEEP_POLL_MS      150       // how often the keyboard is checked while asleep

// ---------------- Battery meter (see battery.h for the wiring) ----------------
#define BATT_MONITOR     0       // 1 once the divider is fitted: 4-bar meter in the title bar
#define PIN_BATT_ADC     4       // ADC1 channel (safe with WiFi on): middle of the battery divider
#define PIN_USB_SENSE    -1      // GPIO on a 100k/100k divider from the charger's USB 5V, or -1 (no charging icon)
#define BATT_DIVIDER     2.0f    // (top + bottom) / bottom: 100k + 100k = 2.0
#define BATT_CAL         1.00f   // fine-tune: multimeter volts / volts shown in System > About
#define BATT_SHUTDOWN_V  3.45f   // "Battery empty" + deep sleep below this. Set it to where your board's
                                 // 3.3V regulator gives up: ~3.45 for an LDO like the ME6211, ~4.2 for an AMS1117
#define BATT_READ_SEC    30      // how often to measure
#define BATT_RECHECK_MIN 10      // while shut down: wake this often to see if it's been charged

// ---------------- Reserved for later ----------------
#define PIN_WAKE_BTN    5   // RTC-capable GPIO for deep-sleep wake

// ---------------- Refresh policy ----------------
// Defaults; both can be changed in System > Settings (saved in DISPLAY_CONFIG_FILE)
#define FULL_REFRESH_EVERY  100   // force a full refresh after N partials in a row (ghosting control)
#define FULL_REFRESH_SOFT   30    // app switches etc. get a full refresh only after this many partials
#define DISPLAY_CONFIG_FILE "/system/display.txt"
#define EMULATE_EPD_TIMING  0     // TFT only: 1 = add fake e-paper delays
#define EPD_PARTIAL_MS      400
#define EPD_FULL_MS         3000

#if DISPLAY_PANEL == PANEL_WEACT_290_BWR
  #define REFRESH_SETTLE_MS    800   // wait for typing to pause before a slow refresh
  #define STATUS_CLOCK_REDRAW  0     // don't do a slow refresh just for the clock
#else
  #define REFRESH_SETTLE_MS    0
  #define STATUS_CLOCK_REDRAW  1
#endif
