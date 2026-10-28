#pragma once
// =====================================================================
//  Screen HAL
//  Apps draw into 1-bit canvases at the panel's logical resolution:
//    g()   = black ink layer
//    red() = accent layer (only shown if hasRed(); black ink wins on overlap)
//  The backend (TFT preview or e-paper) pushes the canvases to glass.
//  Apps never talk to a display library directly.
// =====================================================================
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "config.h"

// Partial = fast update. Full = the screen changed a lot (app switch, new mode):
// cleaned with a full refresh only if fullOnSwitch() partials have built up
// since the last one, otherwise partial. Clean = always a full refresh, and the
// deep one where the panel has it.
//
// The 3.7" (UC8253) has three refresh levels, picked by the temperature its
// waveform is looked up for:
//   partial  ~0.35 s  no flash, ghosting builds up      (forced 110 C, GxEPD2 partial)
//   fast     ~1 s     one flash, clears most ghosting   (forced 90 C, GxEPD2 full)
//   deep     ~3 s     several flashes, cleanest         (real temperature, stock waveform)
// Ordered by strength: a pending redraw keeps the strongest one requested.
enum class Refresh { Partial, Full, Clean };

constexpr uint16_t INK   = 1;   // pixel set
constexpr uint16_t PAPER = 0;   // pixel clear
constexpr uint8_t  NO_GREY = 0xFF;

// The black layer: an ordinary 1-bit canvas that also keeps an optional grey
// level per pixel (pictures: 0 = white, 1 = light grey, 2 = dark grey,
// 3 = black) and forgets it for any pixel something is drawn over.
class InkCanvas : public GFXcanvas1 {
public:
  InkCanvas(uint16_t w, uint16_t h) : GFXcanvas1(w, h) {}
  uint8_t* grey = nullptr;        // one byte per pixel, NO_GREY = plain pixel
  bool greyUsed = false;          // false = no grey pixels anywhere (fast path)

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (greyUsed && x >= 0 && y >= 0 && x < width() && y < height()) grey[y * width() + x] = NO_GREY;
    GFXcanvas1::drawPixel(x, y, color);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    forget(x, y, w, 1);
    GFXcanvas1::drawFastHLine(x, y, w, color);
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    forget(x, y, 1, h);
    GFXcanvas1::drawFastVLine(x, y, h, color);
  }
  void fillScreen(uint16_t color) override {
    if (grey && greyUsed) memset(grey, NO_GREY, (size_t)width() * height());
    greyUsed = false;
    GFXcanvas1::fillScreen(color);
  }
  void rawPixel(int16_t x, int16_t y, bool ink) { GFXcanvas1::drawPixel(x, y, ink ? INK : PAPER); }

private:
  void forget(int x, int y, int w, int h) {
    if (!greyUsed) return;
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > width()) x1 = width();
    if (y1 > height()) y1 = height();
    for (int j = y; j < y1; j++)
      if (x1 > x) memset(grey + j * width() + x, NO_GREY, x1 - x);
  }
};

class Screen {
public:
  void begin();

  GFXcanvas1& g()   { return _c; }     // black layer
  GFXcanvas1& red() { return _r; }     // accent layer
  bool hasRed() const;

  void clear() { _c.fillScreen(PAPER); _r.fillScreen(PAPER); }

  // Grey pictures. setGrey() gives a pixel a level 0 (white) .. 3 (black) and
  // puts a black-and-white stand-in on the black layer, so panels without
  // greys still show the picture. Drawing over a grey pixel turns it back
  // into a plain black/white pixel.
  void setGrey(int x, int y, uint8_t level);
  bool canShowGrey() const;               // this panel really shows grey shades

  // Push canvases to the panel (see Refresh above). Any refresh is promoted
  // to a full one after fullEvery() partials in a row.
  void refresh(Refresh mode);
  void forceFullNext() { _forceFull = true; }

  // Full-refresh policy (System > Settings), saved in DISPLAY_CONFIG_FILE.
  // Both count partial refreshes since the last full one; REFRESH_NEVER turns it off.
  static const uint16_t REFRESH_NEVER = 0xFFFF;
  void loadSettings();                                    // after Storage::begin()
  uint16_t fullEvery() const    { return _fullEvery; }    // automatic full refresh after N partials
  uint16_t fullOnSwitch() const { return _fullOnSwitch; } // app switch etc. (Refresh::Full) cleans after N; 0 = always
  bool setFullEvery(uint16_t n)    { _fullEvery = n;    return saveSettings(); }
  bool setFullOnSwitch(uint16_t n) { _fullOnSwitch = n; return saveSettings(); }
  bool hasDeepClean() const;                              // panel has a slower, cleaner full refresh
  bool deepFull() const         { return _deepFull; }     // automatic full refreshes use it too
  bool setDeepFull(bool on)     { _deepFull = on; return saveSettings(); }
  uint32_t partialCount() const { return _partials; }
  uint32_t fullCount() const { return _fulls; }
  uint32_t lastRefreshMs() const { return _lastMs; }

  // Text helpers (built-in 6x8 font, scaled by size), black layer
  void text(int x, int y, const char* s, uint8_t size = 1);
  void textCentered(int cx, int y, const char* s, uint8_t size = 1);
  int  textWidth(const char* s, uint8_t size = 1) const { return (int)strlen(s) * 6 * size; }

  // Flip every pixel in a rect on the black layer (selection highlight).
  // Grey pixels flip too (light grey <-> dark grey).
  void invertRect(int x, int y, int w, int h);

  const char* backendName() const;

  void sleep();                   // panel powered down (e-paper keeps its image) / backlight off
  void wake();

private:
  InkCanvas  _c{SCREEN_W, SCREEN_H};
  uint8_t*   _greyShown = nullptr;   // grey levels the e-paper is showing now
  bool       _greyOnPanel = false;
  bool       _sleptSinceGrey = false;  // panel was put to sleep: redo greys on the next refresh
  bool needsGrey() const;              // any light/dark grey pixels to show?
  GFXcanvas1 _r{SCREEN_W, SCREEN_H};
  uint32_t _partials = 0;
  uint32_t _fulls = 0;
  uint32_t _lastMs = 0;
  bool _forceFull = false;
  uint16_t _fullEvery = FULL_REFRESH_EVERY;
  uint16_t _fullOnSwitch = FULL_REFRESH_SOFT;
  bool _deepFull = false;

  bool saveSettings();

  void backendBegin();
  void backendPush(bool full, bool deep);
};

extern Screen screen;
