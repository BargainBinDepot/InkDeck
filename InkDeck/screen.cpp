#include "screen.h"
#include "storage.h"
#include "keyboard.h"

Screen screen;

// ---------------------------------------------------------------------
//  Backend-independent part
// ---------------------------------------------------------------------
void Screen::begin() {
  _c.setTextWrap(false);
  clear();
  backendBegin();
}

void Screen::refresh(Refresh mode) {
  bool full = (mode == Refresh::Clean) || _forceFull
           || (_fullEvery != REFRESH_NEVER && _partials >= _fullEvery)
           || (mode == Refresh::Full && _fullOnSwitch != REFRESH_NEVER && _partials >= _fullOnSwitch);
  const bool deep = full && hasDeepClean() && (mode == Refresh::Clean || _deepFull);
  uint32_t t = millis();
  backendPush(full, deep);
  _lastMs = millis() - t;
  if (full) { _partials = 0; _forceFull = false; _fulls++; }
  else      { _partials++; }
  Serial.printf("[screen] %s refresh took %lu ms\n", deep ? "deep" : full ? "full" : "partial", (unsigned long)_lastMs);
}

void Screen::loadSettings() {
  String cfg;
  if (!Storage::readText(DISPLAY_CONFIG_FILE, cfg, 128)) return;
  int p = cfg.indexOf("every=");
  if (p >= 0) _fullEvery = strtoul(cfg.c_str() + p + 6, nullptr, 10);
  p = cfg.indexOf("switch=");
  if (p >= 0) _fullOnSwitch = strtoul(cfg.c_str() + p + 7, nullptr, 10);
  _deepFull = cfg.indexOf("deep=1") >= 0;
  Serial.printf("[screen] full refresh every %u partials, on app switch after %u, %s\n",
                _fullEvery, _fullOnSwitch, _deepFull ? "deep" : "fast");
}

bool Screen::saveSettings() {
  return Storage::writeText(DISPLAY_CONFIG_FILE, "every=" + String(_fullEvery) + "\nswitch=" + String(_fullOnSwitch) +
                                                 "\ndeep=" + String(_deepFull ? 1 : 0) + "\n");
}

void Screen::text(int x, int y, const char* s, uint8_t size) {
  _c.setTextSize(size);
  _c.setTextColor(INK);
  _c.setCursor(x, y);
  _c.print(s);
}

void Screen::textCentered(int cx, int y, const char* s, uint8_t size) {
  text(cx - textWidth(s, size) / 2, y, s, size);
}

void Screen::invertRect(int x, int y, int w, int h) {
  for (int j = y; j < y + h; j++) {
    if (j < 0 || j >= SCREEN_H) continue;
    for (int i = x; i < x + w; i++) {
      if (i < 0 || i >= SCREEN_W) continue;
      _c.drawPixel(i, j, _c.getPixel(i, j) ? PAPER : INK);
    }
  }
}

// =====================================================================
//  TFT preview backend (ST7789)
//  Shows the target e-paper panel on the 320x240 TFT with a dark bezel.
//  If the panel is bigger than the TFT, it's either shrunk to fit with
//  smoothing (TFT_PREVIEW_FIT 1) or shown 1:1 and cropped (0).
// =====================================================================
#if DISPLAY_PANEL == PANEL_TFT_PREVIEW

#include <SPI.h>
#include <Adafruit_ST7789.h>

static Adafruit_ST7789 tft(&SPI, PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST);

// Colours as RGB888 so partially-covered pixels can be blended
static const uint8_t RGB_INK[3]   = {  32,  32,  32 };
static const uint8_t RGB_PAPER[3] = { 222, 219, 214 };
static const uint8_t RGB_RED[3]   = { 180,  32,  32 };
static const uint16_t COL_BEZEL   = 0x18C3;
static const uint16_t COL_LABEL   = 0x7BEF;

static inline uint16_t rgb565(int r, int g, int b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static uint16_t COL_INK, COL_PAPER, COL_RED;

static int16_t offX = 0, offY = 0, dstW = 0, dstH = 0;
static int16_t srcCropX = 0, srcCropY = 0;
static uint16_t colX0[320], colX1[320];   // source column span for each TFT column
static uint16_t lineBuf[320];

bool Screen::hasRed() const { return TARGET_HAS_RED; }
bool Screen::hasDeepClean() const { return TARGET_PANEL == PANEL_WEACT_370_BW; }
void Screen::sleep() { digitalWrite(PIN_TFT_BL, LOW); }
void Screen::wake()  { digitalWrite(PIN_TFT_BL, HIGH); }
const char* Screen::backendName() const { return "TFT as " TARGET_NAME; }

// Source row span for a given TFT row
static inline void rowSpan(int dy, int& y0, int& y1) {
#if TFT_PREVIEW_FIT
  y0 = (int32_t)dy * SCREEN_H / dstH;
  y1 = (int32_t)(dy + 1) * SCREEN_H / dstH;
  if (y1 <= y0) y1 = y0 + 1;
#else
  y0 = srcCropY + dy;
  y1 = y0 + 1;
#endif
}

void Screen::backendBegin() {
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  SPI.begin(PIN_DISP_SCK, -1, PIN_DISP_MOSI, PIN_DISP_CS);
  tft.init(TFT_NATIVE_W, TFT_NATIVE_H);
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);

  COL_INK   = rgb565(RGB_INK[0],   RGB_INK[1],   RGB_INK[2]);
  COL_PAPER = rgb565(RGB_PAPER[0], RGB_PAPER[1], RGB_PAPER[2]);
  COL_RED   = rgb565(RGB_RED[0],   RGB_RED[1],   RGB_RED[2]);

  const int tw = tft.width(), th = tft.height();

#if TFT_PREVIEW_FIT
  float s = (float)tw / SCREEN_W;
  if ((float)th / SCREEN_H < s) s = (float)th / SCREEN_H;
  if (s > 1.0f) s = 1.0f;
  dstW = (int16_t)(SCREEN_W * s);
  dstH = (int16_t)(SCREEN_H * s);
  for (int dx = 0; dx < dstW; dx++) {
    colX0[dx] = (int32_t)dx * SCREEN_W / dstW;
    colX1[dx] = (int32_t)(dx + 1) * SCREEN_W / dstW;
    if (colX1[dx] <= colX0[dx]) colX1[dx] = colX0[dx] + 1;
  }
#else
  dstW = SCREEN_W < tw ? SCREEN_W : tw;
  dstH = SCREEN_H < th ? SCREEN_H : th;
  srcCropX = constrain(TFT_CROP_X, 0, SCREEN_W - dstW);
  srcCropY = constrain(TFT_CROP_Y, 0, SCREEN_H - dstH);
  for (int dx = 0; dx < dstW; dx++) { colX0[dx] = srcCropX + dx; colX1[dx] = colX0[dx] + 1; }
#endif

  offX = (tw - dstW) / 2;
  offY = (th - dstH) / 2;

  tft.fillScreen(COL_BEZEL);
  if (offY + dstH + 12 <= th) {
    tft.setTextColor(COL_LABEL);
    tft.setTextSize(1);
    tft.setCursor(offX, offY + dstH + 4);
    tft.printf("%s preview %dx%d", TARGET_NAME, SCREEN_W, SCREEN_H);
    if (dstW != SCREEN_W || dstH != SCREEN_H)
      tft.printf(TFT_PREVIEW_FIT ? " (scaled %d%%)" : " (cropped)", dstW * 100 / SCREEN_W);
  }

  Serial.printf("[screen] TFT %dx%d showing %s %dx%d as %dx%d at %d,%d\n",
                tw, th, TARGET_NAME, SCREEN_W, SCREEN_H, dstW, dstH, offX, offY);
}

void Screen::backendPush(bool full, bool deep) {
  for (int i = 0; full && i < (deep ? 3 : 1); i++) {
    // Mimic the e-paper full-refresh flash (the deep one flashes a few times)
    tft.fillRect(offX, offY, dstW, dstH, COL_INK);
    delay(60);
    tft.fillRect(offX, offY, dstW, dstH, COL_PAPER);
    delay(60);
  }

  const uint8_t* bw  = _c.getBuffer();
  const uint8_t* red = _r.getBuffer();
  const int bpr = (SCREEN_W + 7) / 8;

  tft.startWrite();
  tft.setAddrWindow(offX, offY, dstW, dstH);
  for (int dy = 0; dy < dstH; dy++) {
    int y0, y1;
    rowSpan(dy, y0, y1);
    for (int dx = 0; dx < dstW; dx++) {
      int ink = 0, rd = 0, n = 0;
      for (int sy = y0; sy < y1; sy++) {
        const uint8_t* rowB = bw  + sy * bpr;
        const uint8_t* rowR = red + sy * bpr;
        for (int sx = colX0[dx]; sx < colX1[dx]; sx++) {
          const uint8_t m = 0x80 >> (sx & 7);
          if (rowB[sx >> 3] & m)      ink++;
          else if (rowR[sx >> 3] & m) rd++;
          n++;
        }
      }
      if (ink == 0 && rd == 0) lineBuf[dx] = COL_PAPER;
      else if (ink == n)       lineBuf[dx] = COL_INK;
      else if (rd == n)        lineBuf[dx] = COL_RED;
      else {
        const int pap = n - ink - rd;
        const int r = (RGB_PAPER[0] * pap + RGB_INK[0] * ink + RGB_RED[0] * rd) / n;
        const int g = (RGB_PAPER[1] * pap + RGB_INK[1] * ink + RGB_RED[1] * rd) / n;
        const int b = (RGB_PAPER[2] * pap + RGB_INK[2] * ink + RGB_RED[2] * rd) / n;
        lineBuf[dx] = rgb565(r, g, b);
      }
    }
    tft.writePixels(lineBuf, dstW);
    if ((dy & 15) == 15) kb.service();          // don't miss keys during a slow push
  }
  tft.endWrite();

#if EMULATE_EPD_TIMING
  delay(full ? EPD_FULL_MS : EPD_PARTIAL_MS);
#endif
}

// =====================================================================
//  E-paper backends (GxEPD2)
// =====================================================================
#else

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>

#if DISPLAY_PANEL == PANEL_WEACT_290_BWR
  // Stock GxEPD2 driver with refresh() swapped for the fake-temperature version.
  // SSD1680 commands: 0x18 temp sensor select, 0x1A write temperature register,
  // 0x22 0x91 = load waveform for that temperature, 0x22 0xC7 = refresh with it.
  class GxEPD2_290_C90c_Fast : public GxEPD2_290_C90c {
  public:
    using GxEPD2_290_C90c::GxEPD2_290_C90c;
    using GxEPD2_290_C90c::refresh;
    void refresh(bool partial_update_mode = false) {
      if (EPD_FAST_TEMP_C == 0) { GxEPD2_290_C90c::refresh(partial_update_mode); return; }
      _writeCommand(0x18); _writeData(0x80);                            // internal sensor
      _writeCommand(0x22); _writeData(0xB1); _writeCommand(0x20);
      _waitWhileBusy("readTemp", 300);
      _writeCommand(0x1A); _writeData(EPD_FAST_TEMP_C); _writeData(0x00);  // fake temperature
      _writeCommand(0x22); _writeData(0x91); _writeCommand(0x20);
      _waitWhileBusy("loadLUT", 300);
      _writeCommand(0x22); _writeData(0xC7); _writeCommand(0x20);        // refresh with that LUT
      _waitWhileBusy("fastRefresh", full_refresh_time);
      _power_is_on = false;
    }
  };
  static GxEPD2_3C<GxEPD2_290_C90c_Fast, GxEPD2_290_C90c_Fast::HEIGHT>
    epd(GxEPD2_290_C90c_Fast(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_EPD_BUSY));
  #define EPD_HAS_RED 1
  #define EPD_NAME "WeAct 2.9 BWR"
#elif DISPLAY_PANEL == PANEL_WEACT_290_BW
  static GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>
    epd(GxEPD2_290_BS(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_EPD_BUSY));
  #define EPD_HAS_RED 0
  #define EPD_NAME "WeAct 2.9 BW"
#elif DISPLAY_PANEL == PANEL_WEACT_370_BW
  // GxEPD2's driver uses two of the UC8253's three refresh levels: partial
  // (temperature forced to 110 C) and a fast full refresh (forced to 90 C,
  // ~1 s). With deepClean set, a full refresh instead uses the stock waveform
  // for the real temperature (~3 s, several flashes): the cleanest result.
  // Same commands as the driver's _Update_Full, minus the forced temperature;
  // the driver's soft reset after every refresh has already cleared it.
  class GxEPD2_370_Deep : public GxEPD2_370_GDEY037T03 {
  public:
    using GxEPD2_370_GDEY037T03::GxEPD2_370_GDEY037T03;
    using GxEPD2_370_GDEY037T03::refresh;
    bool deepClean = false;
    void refresh(bool partial_update_mode = false) {
      if (partial_update_mode || !deepClean || _initial_refresh) {
        GxEPD2_370_GDEY037T03::refresh(partial_update_mode);
        return;
      }
      _writeCommand(0x50); _writeData(0x97);                  // VCOM and data interval, as for a full refresh
      _writeCommand(0x04); _waitWhileBusy("powerOn", power_on_time);
      _writeCommand(0x12); _waitWhileBusy("deepRefresh", 3 * full_refresh_time);
      _writeCommand(0x02); _waitWhileBusy("powerOff", power_off_time);
      _power_is_on = false;
    }
  };
  static GxEPD2_BW<GxEPD2_370_Deep, GxEPD2_370_Deep::HEIGHT>
    epd(GxEPD2_370_Deep(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_EPD_BUSY));
  #define EPD_HAS_RED 0
  #define EPD_HAS_DEEP 1
  #define EPD_NAME "WeAct 3.7 BW"
#else
  #error "Unknown DISPLAY_PANEL"
#endif

#ifndef EPD_HAS_DEEP
  #define EPD_HAS_DEEP 0
#endif

bool Screen::hasRed() const { return EPD_HAS_RED; }
bool Screen::hasDeepClean() const { return EPD_HAS_DEEP; }
void Screen::sleep() { epd.hibernate(); }      // GxEPD2 wakes the panel again on the next display()
void Screen::wake()  {}
const char* Screen::backendName() const { return EPD_NAME; }

// Called over and over while the panel is busy refreshing: keep reading the
// keyboard so keys typed meanwhile are queued, not lost
static void whileBusy(const void*) {
  kb.service();
  delay(1);
}

void Screen::backendBegin() {
  SPI.begin(PIN_DISP_SCK, -1, PIN_DISP_MOSI, PIN_DISP_CS);
  epd.init(115200, true, 50, false);   // prints busy timings to Serial
  epd.epd2.setBusyCallback(whileBusy);
  epd.setRotation(EPD_ROTATION);
  epd.setTextWrap(false);
  Serial.printf("[screen] %s, %dx%d\n", EPD_NAME, epd.width(), epd.height());
}

void Screen::backendPush(bool full, bool deep) {
#if EPD_HAS_DEEP
  epd.epd2.deepClean = deep;
#else
  (void)deep;
#endif
  epd.setFullWindow();
  epd.fillScreen(GxEPD_WHITE);
#if EPD_HAS_RED
  epd.drawBitmap(0, 0, _r.getBuffer(), SCREEN_W, SCREEN_H, GxEPD_RED);
#endif
  epd.drawBitmap(0, 0, _c.getBuffer(), SCREEN_W, SCREEN_H, GxEPD_BLACK);  // black drawn last so it wins
  epd.display(!full);
}

#endif
