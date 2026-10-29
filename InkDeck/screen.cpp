#include "screen.h"
#include "storage.h"
#include "keyboard.h"
#include "sound.h"
#include <esp_heap_caps.h>

Screen screen;

// ---------------------------------------------------------------------
//  Backend-independent part
// ---------------------------------------------------------------------
static uint8_t* bigAlloc(size_t n) {          // PSRAM if the board has it
  uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = (uint8_t*)malloc(n);
  return p;
}

void Screen::begin() {
  const size_t n = (size_t)SCREEN_W * SCREEN_H;
  _c.grey = bigAlloc(n);
  if (_c.grey) memset(_c.grey, NO_GREY, n);
  else Serial.println("[screen] no memory for grey pictures: black and white only");
  if (canShowGrey()) {
    _greyShown = bigAlloc(n);
    if (_greyShown) memset(_greyShown, NO_GREY, n);
  }
  if (hasFastRefresh()) {
    _lastShown = bigAlloc(((SCREEN_W + 7) / 8) * SCREEN_H);
    if (_lastShown) memset(_lastShown, 0, ((SCREEN_W + 7) / 8) * SCREEN_H);
  }
  _c.setTextWrap(false);
  clear();
  backendBegin();
}

void Screen::refresh(Refresh mode) {
  bool full = (mode == Refresh::Clean) || _forceFull
           || (_fullEvery != REFRESH_NEVER && _partials >= _fullEvery)
           || (mode == Refresh::Full && _fullOnSwitch != REFRESH_NEVER && _partials >= _fullOnSwitch);
  const bool deep = full && hasDeepClean() && (mode == Refresh::Clean || _deepFull);
  _fastNow = !full && mode == Refresh::Partial && _lastShown &&
             (_fastMode == FAST_ALWAYS || (_fastMode == FAST_APPS && _fastHint));
  uint32_t t = millis();
  backendPush(full, deep);
  if (_lastShown) memcpy(_lastShown, _c.getBuffer(), ((SCREEN_W + 7) / 8) * SCREEN_H);
  _lastMs = millis() - t;
  if (full) { _partials = 0; _forceFull = false; _fulls++; }
  else      { _partials++; }
  Serial.printf("[screen] %s refresh took %lu ms\n", deep ? "deep" : full ? "full" : _fastNow ? "fast" : "partial",
                (unsigned long)_lastMs);
}

// Grey waveform timings; used by greyRefresh() on the 3.7" e-paper
static Screen::GreyTiming gT = Screen::greyDefaults();

Screen::GreyTiming Screen::greyTiming() const { return gT; }

bool Screen::setGreyTiming(const GreyTiming& t) {
  gT = t;
  redoGrey();
  return saveSettings();
}

void Screen::loadSettings() {
  String cfg;
  if (!Storage::readText(DISPLAY_CONFIG_FILE, cfg, 256)) return;
  auto num = [&](const char* key, uint8_t& v, int lo, int hi) {
    const int p = cfg.indexOf(key);
    if (p < 0) return;
    const long n = strtol(cfg.c_str() + p + strlen(key), nullptr, 10);
    if (n >= lo && n <= hi) v = (uint8_t)n;
  };
  num("greylight=", gT.light, 1, 63);
  num("greydark=", gT.dark, 1, 63);
  num("greyblack=", gT.black, 1, 63);
  num("greyclear=", gT.clear, 1, 63);
  num("greyrepeat=", gT.repeat, 0, 1);
  uint8_t fm = _fastMode;
  num("fast=", fm, 0, 2);
  _fastMode = (FastMode)fm;
  num("fastframes=", _fastFrames, 1, 40);
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
                                                 "\ndeep=" + String(_deepFull ? 1 : 0) +
                                                 "\ngreylight=" + String(gT.light) + "\ngreydark=" + String(gT.dark) +
                                                 "\ngreyblack=" + String(gT.black) + "\ngreyclear=" + String(gT.clear) +
                                                 "\ngreyrepeat=" + String(gT.repeat) +
                                                 "\nfast=" + String((int)_fastMode) + "\nfastframes=" + String(_fastFrames) + "\n");
}

// Black-and-white stand-in for a grey level (2x2 pattern, so grey areas
// still look grey on panels without grey shades)
static inline bool standIn(int x, int y, uint8_t level) {
  switch (level) {
    case 0:  return false;
    case 1:  return (x & 1) && (y & 1);       // 1 in 4
    case 2:  return ((x ^ y) & 1) != 0;       // 2 in 4
    default: return true;
  }
}

void Screen::setGrey(int x, int y, uint8_t level) {
  if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  if (level > 3) level = 3;
  _c.rawPixel(x, y, standIn(x, y, level));
  if (_c.grey) {
    _c.grey[y * SCREEN_W + x] = level;
    _c.greyUsed = true;
  }
}

bool Screen::needsGrey() const {
  if (!_c.grey || !_c.greyUsed) return false;
  const size_t n = (size_t)SCREEN_W * SCREEN_H;
  for (size_t i = 0; i < n; i++) {
    const uint8_t v = _c.grey[i];
    if (v == 1 || v == 2) return true;
  }
  return false;
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
      if (_c.greyUsed) {
        uint8_t& v = _c.grey[j * SCREEN_W + i];
        if (v != NO_GREY) v = 3 - v;          // grey flips to the opposite shade
      }
      _c.rawPixel(i, j, !_c.getPixel(i, j));
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
bool Screen::canShowGrey() const { return true; }      // the preview shows greys as colours
bool Screen::hasGreyWaveforms() const { return false; }
bool Screen::hasFastRefresh() const { return false; }
void Screen::loop() {}
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
  const uint8_t* grey = _c.greyUsed ? _c.grey : nullptr;
  const int bpr = (SCREEN_W + 7) / 8;

  tft.startWrite();
  tft.setAddrWindow(offX, offY, dstW, dstH);
  for (int dy = 0; dy < dstH; dy++) {
    int y0, y1;
    rowSpan(dy, y0, y1);
    for (int dx = 0; dx < dstW; dx++) {
      // weights in thirds: black = 3, dark grey = 2, light grey = 1
      int ink = 0, rd = 0, n = 0;
      for (int sy = y0; sy < y1; sy++) {
        const uint8_t* rowB = bw  + sy * bpr;
        const uint8_t* rowR = red + sy * bpr;
        for (int sx = colX0[dx]; sx < colX1[dx]; sx++) {
          const uint8_t m = 0x80 >> (sx & 7);
          const uint8_t gv = grey ? grey[sy * SCREEN_W + sx] : NO_GREY;
          if (gv != NO_GREY)          ink += gv;
          else if (rowB[sx >> 3] & m) ink += 3;
          else if (rowR[sx >> 3] & m) rd += 3;
          n += 3;
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
    if ((dy & 15) == 15) { kb.service(); Sound::loop(); }   // don't miss keys during a slow push
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
  #define EPD_CAN_GREY 0
  #define EPD_NAME "WeAct 2.9 BWR"
#elif DISPLAY_PANEL == PANEL_WEACT_290_BW
  static GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>
    epd(GxEPD2_290_BS(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_EPD_BUSY));
  #define EPD_HAS_RED 0
  #define EPD_CAN_GREY 0
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
    // For InkDeck's own refreshes (fast, grey), which talk to the controller directly
    bool hibernating() const { return _hibernating; }
    void markAwake() { _hibernating = false; _init_display_done = true; _power_is_on = false; }
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
  #define EPD_CAN_GREY EPD_GREY_PICTURES
  #define EPD_NAME "WeAct 3.7 BW"
#else
  #error "Unknown DISPLAY_PANEL"
#endif

#ifndef EPD_HAS_DEEP
  #define EPD_HAS_DEEP 0
#endif

bool Screen::hasRed() const { return EPD_HAS_RED; }
bool Screen::hasDeepClean() const { return EPD_HAS_DEEP; }
bool Screen::canShowGrey() const { return EPD_CAN_GREY; }
bool Screen::hasGreyWaveforms() const { return EPD_CAN_GREY; }
#if DISPLAY_PANEL == PANEL_WEACT_370_BW
static void fastEnd();              // below, with the fast refresh
#endif
void Screen::sleep() {
#if DISPLAY_PANEL == PANEL_WEACT_370_BW
  fastEnd();                        // a fast burst can't be left powered
#endif
  epd.hibernate(); _sleptSinceGrey = true;
}   // GxEPD2 wakes the panel again on the next display()
void Screen::wake()  {}
const char* Screen::backendName() const { return EPD_NAME; }

// Called over and over while the panel is busy refreshing: keep reading the
// keyboard so keys typed meanwhile are queued, not lost
static void whileBusy(const void*) {
  kb.service();
  Sound::loop();
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

#if DISPLAY_PANEL == PANEL_WEACT_370_BW
// ---------------------------------------------------------------------
//  4-grey refresh for the 3.7" (UC8253), done directly over SPI with our
//  own waveforms. GxEPD2 goes on handling every other refresh.
//  Each pixel's grey level is sent as two bits (the panel's "old" and "new"
//  pictures), and each of the four combinations gets its own waveform:
//  shake clean, then darken for a set time.
// ---------------------------------------------------------------------
static const int NW = GxEPD2_370_GDEY037T03::WIDTH;     // 240 (controller's own layout)
static const int NH = GxEPD2_370_GDEY037T03::HEIGHT;    // 416
static const size_t PLANE = NW / 8 * NH;
static uint8_t* planeOld = nullptr;
static uint8_t* planeNew = nullptr;
static SPISettings greySpi(10000000, MSBFIRST, SPI_MODE0);

static void gCmd(uint8_t c) {
  SPI.beginTransaction(greySpi);
  digitalWrite(PIN_DISP_DC, LOW);
  digitalWrite(PIN_DISP_CS, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_DISP_CS, HIGH);
  SPI.endTransaction();
}
static void gData(const uint8_t* d, size_t n) {
  SPI.beginTransaction(greySpi);
  digitalWrite(PIN_DISP_DC, HIGH);
  digitalWrite(PIN_DISP_CS, LOW);
  SPI.writeBytes(d, n);
  digitalWrite(PIN_DISP_CS, HIGH);
  SPI.endTransaction();
}
static void gData1(uint8_t d) { gData(&d, 1); }
static uint32_t gWait(const char* what, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  delay(1);
  while (digitalRead(PIN_EPD_BUSY) == LOW) {           // BUSY is LOW while working
    if (millis() - t0 > timeoutMs) { Serial.printf("[screen] grey: %s timed out\n", what); break; }
    kb.service();                                      // keys typed meanwhile are queued, not lost
    Sound::loop();
    delay(1);
  }
  return millis() - t0;
}

// One waveform. UC8253 layout, 7 bytes per group:
//   [group repeat][level|frames x4][state 1 repeat][state 2 repeat]
// levels: 0 = 0 V, 1 = VSH (darkens), 2 = VSL (lightens); VCOM 0 = VCOM_DC
struct GPhase { uint8_t lv, fr; };
static void gLut(uint8_t reg, const GPhase* ph, int n, int groups, int repeat = -1) {   // -1 = the grey setting
  uint8_t buf[56];
  memset(buf, 0, sizeof(buf));
  for (int g = 0; g < groups; g++) {
    bool used = false;
    for (int p = 0; p < 4; p++) {
      const int i = g * 4 + p;
      if (i < n && ph[i].fr) {
        buf[g * 7 + 1 + p] = (uint8_t)((ph[i].lv & 3) << 6) | (ph[i].fr > 63 ? 63 : ph[i].fr);
        used = true;
      }
    }
    if (used) buf[g * 7] = buf[g * 7 + 5] = buf[g * 7 + 6] = repeat < 0 ? gT.repeat : repeat;
  }
  gCmd(reg);
  gData(buf, groups * 7);
}
// balance push, shake dark, shake light, then darken for f frames (0 = white)
static int gWave(GPhase* ph, uint8_t f) {
  ph[0] = { 2, f };
  ph[1] = { 1, gT.clear };
  ph[2] = { 2, gT.clear };
  ph[3] = { 1, f };
  return 4;
}

// Landscape pixel -> controller RAM position (same mapping as GxEPD2's rotation)
static inline void toNative(int x, int y, int& nx, int& ny) {
#if EPD_ROTATION == 1
  nx = NW - 1 - y; ny = x;
#elif EPD_ROTATION == 3
  nx = y; ny = NH - 1 - x;
#elif EPD_ROTATION == 2
  nx = NW - 1 - x; ny = NH - 1 - y;
#else
  nx = x; ny = y;
#endif
}

static bool planesReady() {
  if (!planeOld) planeOld = (uint8_t*)malloc(PLANE);
  if (!planeNew) planeNew = (uint8_t*)malloc(PLANE);
  if (!planeOld || !planeNew) { Serial.println("[screen] out of memory for panel pictures"); return false; }
  return true;
}

// Black layer (landscape, 1 = ink) -> one controller picture (native, 1 = white)
static void packBW(const uint8_t* bw, uint8_t* plane) {
  memset(plane, 0, PLANE);
  const int bpr = (SCREEN_W + 7) / 8;
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++)
      if (!(bw[y * bpr + (x >> 3)] & (0x80 >> (x & 7)))) {
        int nx, ny;
        toNative(x, y, nx, ny);
        plane[(size_t)ny * (NW / 8) + nx / 8] |= 0x80 >> (nx & 7);
      }
}

static void hardReset() {
  digitalWrite(PIN_DISP_RST, HIGH); delay(10);
  digitalWrite(PIN_DISP_RST, LOW);  delay(10);
  digitalWrite(PIN_DISP_RST, HIGH); delay(10);
  gWait("reset", 1000);
}

// Back to GxEPD2's own waveforms, with both controller pictures = what the
// panel shows, so its next partial refresh only changes what really changes
static void handBack(const uint8_t* plane) {
  gCmd(0x00); gData1(0x1e); gData1(0x0d);              // soft reset
  delay(1);
  gCmd(0x00); gData1(0x1f); gData1(0x0d);              // OTP waveforms (GxEPD2's setting)
  gCmd(0x10); gData(plane, PLANE);
  gCmd(0x13); gData(plane, PLANE);
  epd.epd2.markAwake();
}

// ---------------------------------------------------------------------
//  Fast refresh: the panel's two pictures are "before" and "after"; pixels
//  that change get one short push towards their new colour, the rest none
//  (InkBoy / PaperBoy style, one step). The panel stays powered between
//  refreshes and is powered down after FAST_IDLE_MS without one.
// ---------------------------------------------------------------------
static bool fastOn = false;
static uint32_t fastLast = 0;

static void fastLuts(uint8_t frames) {
  const GPhase none[1] = { { 0, 0 } };
  const GPhase toBlack[1] = { { 1, frames } };
  const GPhase toWhite[1] = { { 2, frames } };
  const GPhase vcom[1] = { { 0, frames } };
  gLut(0x20, vcom, 1, 8, 1);                           // each played once
  gLut(0x21, none, 1, 6, 1);                           // white stays white: no drive
  gLut(0x22, toWhite, 1, 8, 1);                        // black -> white
  gLut(0x23, toBlack, 1, 8, 1);                        // white -> black
  gLut(0x24, none, 1, 8, 1);                           // black stays black: no drive
}

static void fastBegin(uint8_t frames, const uint8_t* shownBW) {
  if (!epd.epd2.hibernating()) epd.powerOff();         // GxEPD2 may have left it powered
  hardReset();                                         // also wakes it if it was asleep
  gCmd(0x00); gData1(0x1e); gData1(0x0d);
  delay(1);
  gCmd(0x00); gData1(0x3f); gData1(0x0d);              // waveforms from our registers
  gCmd(0x30); gData1(0x0F);                            // 80 Hz frames (12.5 ms)
  gCmd(0x50); gData1(0xD7);                            // border left alone
  fastLuts(frames);
  packBW(shownBW, planeOld);                           // what the panel shows now
  gCmd(0x04); gWait("power on", 1000);
  fastOn = true;
}

static void fastFrame(const uint8_t* bw) {
  packBW(bw, planeNew);
  gCmd(0x10); gData(planeOld, PLANE);
  gCmd(0x13); gData(planeNew, PLANE);
  gCmd(0x12);
  gWait("fast refresh", 3000);
  uint8_t* t = planeOld; planeOld = planeNew; planeNew = t;   // the new picture is now the shown one
  fastLast = millis();
}

static void fastEnd() {
  if (!fastOn) return;
  gCmd(0x02); gWait("power off", 1000);
  handBack(planeOld);
  fastOn = false;
  Serial.println("[screen] fast refresh: panel powered down");
}

#if EPD_CAN_GREY
static void greyRefresh(const uint8_t* bw, const uint8_t* grey) {
  if (!planesReady()) return;

  // 1. Build the two bit-planes. Bits are 1 = white.
  //    white = old 1 new 1, light = old 0 new 1, dark = old 1 new 0, black = 0 0
  memset(planeOld, 0, PLANE);
  memset(planeNew, 0, PLANE);
  const int bpr = (SCREEN_W + 7) / 8;
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      uint8_t lv = grey[y * SCREEN_W + x];
      if (lv == NO_GREY) lv = (bw[y * bpr + (x >> 3)] & (0x80 >> (x & 7))) ? 3 : 0;
      int nx, ny;
      toNative(x, y, nx, ny);
      const size_t i = (size_t)ny * (NW / 8) + nx / 8;
      const uint8_t m = 0x80 >> (nx & 7);
      if (lv == 0 || lv == 2) planeOld[i] |= m;
      if (lv == 0 || lv == 1) planeNew[i] |= m;
    }
  }

  // 2. Wake the controller and load our waveforms
  hardReset();
  gCmd(0x00); gData1(0x1e); gData1(0x0d);              // soft reset
  delay(1);
  gCmd(0x00); gData1(0x3f); gData1(0x0d);              // black/white mode, waveforms from registers
  gCmd(0x30); gData1(0x09);                            // 50 Hz frames
  gCmd(0x50); gData1(0xD7);                            // border left alone
  GPhase w[4], lt[4], dk[4], bk[4], vc[4];
  gWave(w, 0);
  gWave(lt, gT.light);
  gWave(dk, gT.dark);
  gWave(bk, gT.black);
  Serial.printf("[screen] grey timing: light %u dark %u black %u clear %u repeat %u\n",
                gT.light, gT.dark, gT.black, gT.clear, gT.repeat);
  for (int i = 0; i < 4; i++) vc[i] = { 0, bk[i].fr };
  gLut(0x20, vc, 4, 8);                                // VCOM
  gLut(0x21, w,  4, 6);                                // old 1 new 1 -> white
  gLut(0x22, lt, 4, 8);                                // old 0 new 1 -> light grey
  gLut(0x23, dk, 4, 8);                                // old 1 new 0 -> dark grey
  gLut(0x24, bk, 4, 8);                                // old 0 new 0 -> black

  // 3. Send the picture and refresh
  gCmd(0x10); gData(planeOld, PLANE);
  gCmd(0x13); gData(planeNew, PLANE);
  gCmd(0x04); gWait("power on", 1000);
  gCmd(0x12);
  const uint32_t ms = gWait("grey refresh", 15000);
  gCmd(0x02); gWait("power off", 1000);
  Serial.printf("[screen] grey refresh %lu ms\n", (unsigned long)ms);

  // 4. Hand the panel back to GxEPD2: its own waveforms again, and both of the
  //    controller's pictures set to the black/white version, so the next fast
  //    refresh only touches pixels that really change (the greys stay put).
  gCmd(0x00); gData1(0x1e); gData1(0x0d);
  delay(1);
  gCmd(0x00); gData1(0x1f); gData1(0x0d);
  memset(planeOld, 0, PLANE);
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++)
      if (!(bw[y * bpr + (x >> 3)] & (0x80 >> (x & 7)))) {
        int nx, ny;
        toNative(x, y, nx, ny);
        planeOld[(size_t)ny * (NW / 8) + nx / 8] |= 0x80 >> (nx & 7);
      }
  gCmd(0x10); gData(planeOld, PLANE);
  gCmd(0x13); gData(planeOld, PLANE);
}
#endif  // EPD_CAN_GREY
#endif  // 3.7"

#if DISPLAY_PANEL == PANEL_WEACT_370_BW
bool Screen::hasFastRefresh() const { return true; }
void Screen::loop() {
  if (fastOn && millis() - fastLast > FAST_IDLE_MS) fastEnd();
}
#else
bool Screen::hasFastRefresh() const { return false; }
void Screen::loop() {}
#endif

void Screen::backendPush(bool full, bool deep) {
#if DISPLAY_PANEL == PANEL_WEACT_370_BW
  // Fast refresh: only for plain partial updates with no greys involved
  if (_fastNow && !needsGrey() && !_greyOnPanel && planesReady()) {
    if (fastOn && _fastLutDirty) fastLuts(_fastFrames);
    if (!fastOn) fastBegin(_fastFrames, _lastShown);
    _fastLutDirty = false;
    fastFrame(_c.getBuffer());
    return;
  }
  fastEnd();                        // anything else: back to GxEPD2 first
#endif
#if EPD_CAN_GREY
  // Greys need their own (slower, flashing) refresh, but only when they're new
  // or changed: other updates use the normal fast refresh around them. A
  // refresh that would be full anyway also redraws the greys (it cleans too).
  const bool wantGrey = needsGrey();
  const size_t n = (size_t)SCREEN_W * SCREEN_H;
  if (wantGrey && _greyShown) {
    if (full || !_greyOnPanel || _sleptSinceGrey || memcmp(_c.grey, _greyShown, n) != 0) {
      greyRefresh(_c.getBuffer(), _c.grey);
      memcpy(_greyShown, _c.grey, n);
      _greyOnPanel = true;
      _sleptSinceGrey = false;
      return;
    }
  } else if (_greyOnPanel) {
    full = true;                    // the last greys must be cleaned off properly
    _greyOnPanel = false;
    if (_greyShown) memset(_greyShown, NO_GREY, n);
  }
#endif
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
