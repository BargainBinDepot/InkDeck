#pragma once
// =====================================================================
//  App icons: 1-bit images drawn on the home screen and info popup.
//  Lua apps use icon.bmp in their folder (any uncompressed BMP:
//  1/4/8/24/32-bit; dark pixels = ink). 64x64 is ideal. Bigger images
//  are scaled down to fit; smaller ones are drawn 1:1.
// =====================================================================
#include <Arduino.h>
#include <vector>

struct Icon {
  int w = 0, h = 0;
  std::vector<uint8_t> bits;      // packed rows, MSB = leftmost, 1 = ink
  bool valid() const { return w > 0 && h > 0; }
};

namespace Icons {

// From a packed 1-bit image (like the built-in icons), fitted into box x box
void fromPacked(const uint8_t* src, int w, int h, int box, Icon& out);

// Load a BMP file from the SD card, fitted into box x box. False if unusable.
bool loadBmp(const String& path, int box, Icon& out);

// Load a BMP fitted inside boxW x boxH: shrunk if bigger, never enlarged
bool loadBmpFit(const String& path, int boxW, int boxH, Icon& out);

// Draw with the top-left corner at x,y
void draw(const Icon& icon, int x, int y);

}
