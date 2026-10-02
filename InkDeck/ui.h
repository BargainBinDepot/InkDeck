#pragma once
// =====================================================================
//  Shared layout constants — everything scales with UI_SCALE
//  (1 on the 2.9" panels, 2 on the 3.7").
// =====================================================================
#include <Arduino.h>
#include <vector>
#include "config.h"

namespace UI {
  constexpr int T         = UI_SCALE;
  constexpr int CW        = 6 * T;               // character width
  constexpr int CH        = 8 * T;               // character height
  constexpr int STATUS_H  = CH + 4 * T;          // 12 / 24
  constexpr int BODY_Y    = STATUS_H + 1;        // first pixel row apps can use
  constexpr int LINE_H    = CH + 2 * T;          // text line pitch
  constexpr int MARGIN    = 4 * T;
  constexpr int TEXT_COLS = (SCREEN_W - MARGIN * 2) / CW;
}

// Word-wrap text into lines of at most `cols` characters (defined in apps.cpp)
std::vector<String> wrapText(const String& s, int cols);
