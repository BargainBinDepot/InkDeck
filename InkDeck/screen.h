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
// since the last one, otherwise partial. Clean = always a full refresh.
// Ordered by strength: a pending redraw keeps the strongest one requested.
enum class Refresh { Partial, Full, Clean };

constexpr uint16_t INK   = 1;   // pixel set
constexpr uint16_t PAPER = 0;   // pixel clear

class Screen {
public:
  void begin();

  GFXcanvas1& g()   { return _c; }     // black layer
  GFXcanvas1& red() { return _r; }     // accent layer
  bool hasRed() const;

  void clear() { _c.fillScreen(PAPER); _r.fillScreen(PAPER); }

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
  uint16_t _fullEvery = FULL_REFRESH_EVERY;
  uint16_t _fullOnSwitch = FULL_REFRESH_SOFT;

  bool saveSettings();

  void backendBegin();
  void backendPush(bool full);
};

extern Screen screen;
