#include "textengine.h"

namespace TextEngine {

// Rows an image marker's content ("img <file> <w> <h>") takes: ceil(h / lh),
// at least 1 and at most a whole page. 0 when images are off (lh = 0).
static int imageRows(const String& content, int rows, int lh) {
  if (lh <= 0) return 0;
  const int sp = content.lastIndexOf(' ');
  const int h = sp >= 0 ? content.substring(sp + 1).toInt() : 0;
  int n = (h + lh - 1) / lh;
  if (n < 1) n = 1;
  if (n > rows) n = rows;
  return n;
}

void paginate(Storage::ByteReader& r, int cols, int rows, int lh,
              std::vector<uint32_t>& pages, std::vector<Chapter>& chapters) {
  if (cols < 1) cols = 1;
  if (rows < 1) rows = 1;
  pages.clear();
  chapters.clear();
  pages.push_back(0);

  int lineLen = 0, lineCount = 0, wordLen = 0;
  uint32_t wordStart = 0, markerStart = 0;
  bool inMarker = false, afterMarker = false;
  bool breakAfterImage = false;              // an image filled the page: the next page starts after its line
  String title;

  while (true) {
    const uint32_t pos = r.pos();
    const int ch = r.read();
    if (ch < 0) break;
    const char c = (char)ch;

    if (c == '\r') continue;
    if (afterMarker) {                       // the newline right after a marker isn't a blank line
      afterMarker = false;
      if (c == '\n') {
        if (breakAfterImage) { pages.push_back(r.pos()); breakAfterImage = false; }
        continue;
      }
    }
    if (breakAfterImage) { pages.push_back(pos); breakAfterImage = false; }

    // \x02img file w h\x02 image markers
    if (c == '\x02') {
      String content;
      int m;
      while ((m = r.read()) >= 0 && m != '\x02') if (content.length() < 80) content += (char)m;
      afterMarker = true;
      const int need = imageRows(content, rows, lh);
      if (need == 0) continue;                               // images off
      // finish the word and line in progress (same rule as a space)
      if (wordLen > 0) {
        const int needed = (lineLen == 0) ? wordLen : lineLen + 1 + wordLen;
        if (needed > cols) {
          lineCount++;
          if (lineCount >= rows) { pages.push_back(wordStart); lineCount = 0; }
          lineLen = wordLen;
        } else {
          lineLen = needed;
        }
        wordLen = 0;
      }
      if (lineLen > 0) { lineCount++; lineLen = 0; }
      if (lineCount >= rows) { pages.push_back(pos); lineCount = 0; }
      // doesn't fit in what's left of this page: it starts the next one
      if (lineCount > 0 && lineCount + need > rows) { pages.push_back(pos); lineCount = 0; }
      lineCount += need;
      if (lineCount >= rows) { lineCount = 0; breakAfterImage = true; }
      continue;
    }

    // \x01title\x01 chapter markers
    if (c == '\x01') {
      if (!inMarker) {
        inMarker = true;
        markerStart = pos;
        title = "";
        // Every chapter starts on a fresh page. Finish the word in progress
        // (same rule as a space), then break unless we're already at the top.
        if (wordLen > 0) {
          const int needed = (lineLen == 0) ? wordLen : lineLen + 1 + wordLen;
          if (needed > cols) {
            lineCount++;
            if (lineCount >= rows) { pages.push_back(wordStart); lineCount = 0; }
            lineLen = wordLen;
          } else {
            lineLen = needed;
          }
          wordLen = 0;
        }
        if (lineLen > 0) { lineCount++; lineLen = 0; }
        if (lineCount > 0 && pages.back() != markerStart) {
          pages.push_back(markerStart);
          lineCount = 0;
        }
      } else {
        inMarker = false;
        afterMarker = true;
        title.trim();
        if (title.length()) chapters.push_back({ title, markerStart });
      }
      continue;
    }
    if (inMarker) { if (title.length() < 60) title += c; continue; }

    if (c == ' ' || c == '\t' || c == '\n') {
      if (wordLen > 0) {
        const int needed = (lineLen == 0) ? wordLen : lineLen + 1 + wordLen;
        if (needed > cols) {
          lineCount++;
          if (lineCount >= rows) {
            pages.push_back(wordStart);
            lineCount = 0;
          }
          lineLen = wordLen;
        } else {
          lineLen = needed;
        }
        wordLen = 0;
      }
      if (c == '\n') {
        lineCount++;
        lineLen = 0;
        if (lineCount >= rows) {
          pages.push_back(r.pos());
          lineCount = 0;
        }
      }
    } else {
      if (wordLen == 0) wordStart = pos;
      wordLen++;
      if (wordLen >= cols) {
        // A word as wide as a line is broken onto a line of its own. Like
        // pageLines(), first finish any line in progress (that's a line too).
        if (lineLen > 0) {
          lineCount++;
          lineLen = 0;
          if (lineCount >= rows) { pages.push_back(wordStart); lineCount = 0; }
        }
        lineCount++;
        if (lineCount >= rows) {
          pages.push_back(r.pos());
          lineCount = 0;
        }
        lineLen = 0;
        wordLen = 0;
      }
    }
  }

  // A page that starts exactly at the end of the file would be blank
  while (pages.size() > 1 && pages.back() >= r.size()) pages.pop_back();
}

std::vector<String> pageLines(Storage::ByteReader& r, int cols, int rows, int lh) {
  if (cols < 1) cols = 1;
  if (rows < 1) rows = 1;
  std::vector<String> lines;
  String line, word;
  bool afterMarker = false;

  auto full = [&]() { return (int)lines.size() >= rows; };

  while (!full()) {
    const int ch = r.read();
    if (ch < 0) {
      // End of file: flush what's left
      if (word.length()) {
        if (!line.length())                                          line = word;
        else if ((int)(line.length() + 1 + word.length()) <= cols) { line += ' '; line += word; }
        else if (!full())                                            { lines.push_back(line); line = word; }
      }
      if (line.length() && !full()) lines.push_back(line);
      break;
    }
    const char c = (char)ch;
    if (c == '\r') continue;
    if (afterMarker) {
      afterMarker = false;
      if (c == '\n') continue;
    }

    if (c == '\x02') {
      String content;
      int m;
      while ((m = r.read()) >= 0 && m != '\x02') if (content.length() < 80) content += (char)m;
      afterMarker = true;
      const int need = imageRows(content, rows, lh);
      if (need == 0) continue;                               // images off
      // finish the word and line in progress, exactly as paginate() does
      if (word.length()) {
        if (!line.length())                                          line = word;
        else if ((int)(line.length() + 1 + word.length()) <= cols) { line += ' '; line += word; }
        else                                                         { lines.push_back(line); line = full() ? String() : word; }
        word = "";
      }
      if (line.length()) { if (!full()) lines.push_back(line); line = ""; }
      if (full()) break;
      if (!lines.empty() && (int)lines.size() + need > rows) break;   // it goes on the next page
      lines.push_back(String("\x02") + content);             // the image, then its empty rows
      for (int i = 1; i < need && !full(); i++) lines.push_back(String());
      continue;
    }

    if (c == '\x01') {
      // A chapter starts on its own page: stop here if this page has text
      if (lines.size() || line.length() || word.length()) {
        if (word.length()) {
          if (!line.length())                                          line = word;
          else if ((int)(line.length() + 1 + word.length()) <= cols) { line += ' '; line += word; }
          else                                                         { lines.push_back(line); line = full() ? String() : word; }
        }
        if (line.length() && !full()) lines.push_back(line);
        break;
      }
      int m;                                  // at the top of the page: skip the marker
      while ((m = r.read()) >= 0 && m != '\x01') {}
      afterMarker = true;
      continue;
    }

    if (c == ' ' || c == '\t' || c == '\n') {
      if (word.length()) {
        if (!line.length()) {
          line = word;
        } else if ((int)(line.length() + 1 + word.length()) <= cols) {
          line += ' ';
          line += word;
        } else {
          lines.push_back(line);
          if (full()) break;
          line = word;
        }
        word = "";
      }
      if (c == '\n') {
        lines.push_back(line);
        line = "";
        if (full()) break;
      }
    } else {
      word += c;
      if ((int)word.length() >= cols) {      // hard break a word longer than the line
        if (line.length()) {
          lines.push_back(line);
          line = "";
          if (full()) break;
        }
        lines.push_back(word);
        word = "";
        if (full()) break;
      }
    }
  }
  return lines;
}

}
