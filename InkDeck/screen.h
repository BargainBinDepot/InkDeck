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

enum class Refresh { Partial, Full };

constexpr uint16_t INK   = 1;   // pixel set
constexpr uint16_t PAPER = 0;   // pixel clear

class Screen {
public:
  void begin();

  GFXcanvas1& g()   { return _c; }     // black layer
  GFXcanvas1& red() { return _r; }     // accent layer
  bool hasRed() const;

  void clear() { _c.fillScreen(PAPER); _r.fillScreen(PAPER); }

  // Push canvases to the panel. Full = flash/clean, Partial = fast.
  // Partial is automatically promoted to Full every FULL_REFRESH_EVERY calls.
  void refresh(Refresh mode);
  void forceFullNext() { _forceFull = true; }
  uint32_t partialCount() const { return _partials; }
  uint32_t fullCount() const { return _fulls; }
  uint32_t lastRefreshMs() const { return _lastMs; }

  // Text helpers (built-in 6x8 font, scaled by size), black layer
  void text(int x, int y, const char* s, uint8_t size = 1);
  void textCentered(int cx, int y, const char* s, uint8_t size = 1);
  int  textWidth(const char* s, uint8_t size = 1) const { return (int)strlen(s) * 6 * size; }

  // Flip every pixel in a rect on the black layer (selection highlight)
  void invertRect(int x, int y, int w, int h);

  const char* backendName() const;

  void sleep();                   // panel powered down (e-paper keeps its image) / backlight off
  void wake();

private:
  GFXcanvas1 _c{SCREEN_W, SCREEN_H};
  GFXcanvas1 _r{SCREEN_W, SCREEN_H};
  uint32_t _partials = 0;
  uint32_t _fulls = 0;
  uint32_t _lastMs = 0;
  bool _forceFull = false;

  void backendBegin();
  void backendPush(bool full);
};

extern Screen screen;
