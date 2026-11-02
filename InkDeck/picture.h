#pragma once
// =====================================================================
//  Picture — a JPEG or PNG (from an EPUB) as an eReader picture: fitted to
//  the reading area (never enlarged; a tall one turned a quarter-turn when
//  that makes it 20% bigger), reduced to the screen's shades and saved as a
//  BMP. The same rules as the browser converter (web.html toBmp): line art
//  (90% of pixels near black or white) is a clean black and white, anything
//  else is dithered in the chosen style.
//
//  Needs the JPEGDEC and PNGdec libraries (Arduino Library Manager), unless
//  EPUB_PICTURES is 0 in config.h.
// =====================================================================
#include <Arduino.h>

namespace Picture {

struct Style {
  uint8_t kind = 0;       // 0 natural, 1 classic, 2 linear (the converter's picture styles)
  uint8_t shades = 4;     // 4 greys, or 2 (black and white)
  bool rotate = true;     // turn tall pictures when that makes them bigger
};

bool supported();                                   // built with the decoders

// false = skipped (why says: unsupported format, a tiny spacer, broken data, ...)
bool convert(const uint8_t* data, size_t len, int maxW, int maxH, const Style& style,
             const String& bmpPath, int& w, int& h, String& why);

}
