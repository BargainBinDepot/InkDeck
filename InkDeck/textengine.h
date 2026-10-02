#pragma once
// =====================================================================
//  Text engine — word-wrapped pagination for long text files (books).
//  Ported from the Mini eReader firmware (openBook / displayPage), so
//  books paginate exactly as they did there.
//
//  Chapter markers: \x01Chapter title\x01 anywhere in the text. They are
//  hidden when drawing and returned as a chapter list.
//
//  Image markers: \x02img <file> <width> <height>\x02 on a line of their own.
//  With a line height (lh, pixels) an image takes ceil(height / lh) rows
//  (at most a whole page) and moves to the next page if it doesn't fit.
//  pageLines() returns the marker as a line, followed by empty lines for the
//  rest of the image's rows, so the app can draw it there. With lh = 0
//  images are skipped entirely.
// =====================================================================
#include <Arduino.h>
#include <vector>
#include "storage.h"

namespace TextEngine {

struct Chapter {
  String   title;
  uint32_t offset;   // byte offset of the marker
};

// Byte offset where every page starts (first is always 0), plus chapters.
void paginate(Storage::ByteReader& r, int cols, int rows, int lh,
              std::vector<uint32_t>& pages, std::vector<Chapter>& chapters);

// The lines of one page starting at the reader's current position.
std::vector<String> pageLines(Storage::ByteReader& r, int cols, int rows, int lh);

}
