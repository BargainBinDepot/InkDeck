#include "epub.h"
#include "net.h"
#include "storage.h"
#include "keyboard.h"
#include "progress.h"
#include "ui.h"
#include <functional>
#include <map>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <esp_heap_caps.h>

namespace Epub {

// =====================================================================
//  Inflate: raw DEFLATE, after Mark Adler's puff.c (zlib licence).
//  Whole input and output in memory: the zip says how big each file is.
// =====================================================================
namespace {

struct Inf {
  const uint8_t* in; size_t inLen, inPos;
  uint8_t* out; size_t outLen, outPos;
  uint32_t bitbuf; int bitcnt;
  bool bad;
  int bits(int need) {
    uint32_t val = bitbuf;
    while (bitcnt < need) {
      if (inPos >= inLen) { bad = true; return 0; }
      val |= (uint32_t)in[inPos++] << bitcnt;
      bitcnt += 8;
    }
    bitbuf = val >> need;
    bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
  }
};

struct Huff { int16_t count[16]; int16_t symbol[288]; };

int decode(Inf& s, const Huff& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= s.bits(1);
    if (s.bad) return -1;
    const int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

// Codes from code lengths; <0 over-subscribed, >0 incomplete, 0 complete
int construct(Huff& h, const int16_t* length, int n) {
  for (int len = 0; len < 16; len++) h.count[len] = 0;
  for (int sym = 0; sym < n; sym++) h.count[length[sym]]++;
  if (h.count[0] == n) return 0;
  int left = 1;
  for (int len = 1; len < 16; len++) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return left;
  }
  int16_t offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
  for (int sym = 0; sym < n; sym++) if (length[sym]) h.symbol[offs[length[sym]]++] = sym;
  return left;
}

bool stored(Inf& s) {
  s.bitbuf = 0;
  s.bitcnt = 0;
  if (s.inPos + 4 > s.inLen) return false;
  const unsigned len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
  const unsigned nlen = s.in[s.inPos + 2] | (s.in[s.inPos + 3] << 8);
  s.inPos += 4;
  if (len != (~nlen & 0xFFFF)) return false;
  if (s.inPos + len > s.inLen || s.outPos + len > s.outLen) return false;
  memcpy(s.out + s.outPos, s.in + s.inPos, len);
  s.inPos += len;
  s.outPos += len;
  return true;
}

const int16_t LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                            131, 163, 195, 227, 258 };
const int16_t LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const int32_t DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                            2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const int16_t DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool codes(Inf& s, const Huff& lc, const Huff& dc) {
  for (;;) {
    int sym = decode(s, lc);
    if (sym < 0) return false;
    if (sym < 256) {
      if (s.outPos >= s.outLen) return false;
      s.out[s.outPos++] = (uint8_t)sym;
    } else if (sym == 256) {
      return true;
    } else {
      sym -= 257;
      if (sym >= 29) return false;
      const int len = LBASE[sym] + s.bits(LEXT[sym]);
      const int d = decode(s, dc);
      if (d < 0 || d >= 30) return false;
      const size_t dist = DBASE[d] + s.bits(DEXT[d]);
      if (s.bad || dist > s.outPos || s.outPos + len > s.outLen) return false;
      for (int i = 0; i < len; i++) { s.out[s.outPos] = s.out[s.outPos - dist]; s.outPos++; }
    }
  }
}

bool fixedBlock(Inf& s) {
  static Huff lc, dc;
  static bool built = false;
  if (!built) {
    int16_t lengths[288];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    construct(lc, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(dc, lengths, 30);
    built = true;
  }
  return codes(s, lc, dc);
}

bool dynamicBlock(Inf& s) {
  static const uint8_t ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
  const int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
  if (s.bad || nlen > 286 || ndist > 30) return false;
  int16_t lengths[320];
  int i = 0;
  for (; i < ncode; i++) lengths[ORDER[i]] = s.bits(3);
  for (; i < 19; i++) lengths[ORDER[i]] = 0;
  Huff lc, dc;
  if (construct(lc, lengths, 19) != 0) return false;
  int index = 0;
  while (index < nlen + ndist) {
    int sym = decode(s, lc);
    if (sym < 0) return false;
    if (sym < 16) {
      lengths[index++] = sym;
    } else {
      int16_t len = 0;
      if (sym == 16) {
        if (index == 0) return false;
        len = lengths[index - 1];
        sym = 3 + s.bits(2);
      } else if (sym == 17) {
        sym = 3 + s.bits(3);
      } else {
        sym = 11 + s.bits(7);
      }
      if (s.bad || index + sym > nlen + ndist) return false;
      while (sym--) lengths[index++] = len;
    }
  }
  if (lengths[256] == 0) return false;
  int err = construct(lc, lengths, nlen);
  if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return false;
  err = construct(dc, lengths + nlen, ndist);
  if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return false;
  return codes(s, lc, dc);
}

}  // namespace

bool inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
  Inf s = { in, inLen, 0, out, outLen, 0, 0, 0, false };
  int last;
  do {
    last = s.bits(1);
    const int type = s.bits(2);
    if (s.bad) return false;
    const bool ok = type == 0 ? stored(s) : type == 1 ? fixedBlock(s) : type == 2 ? dynamicBlock(s) : false;
    if (!ok || s.bad) return false;
  } while (!last);
  return s.outPos == outLen;
}

// =====================================================================
//  Zip: the central directory, and one file at a time into memory
// =====================================================================
namespace {

uint8_t* bigBuf(size_t n) {
  uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : (uint8_t*)malloc(n);
}

uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Entry { String name; uint16_t method; uint32_t csize, usize, local; };

class Zip {
public:
  ~Zip() { if (f) f.close(); }

  bool open(const String& path, String& err) {
    f = SD.open(path.c_str(), FILE_READ);
    if (!f) { err = "Can't read the download"; return false; }
    const uint32_t size = f.size();
    const uint32_t tail = std::min<uint32_t>(size, 65557);
    std::vector<uint8_t> buf(tail);
    f.seek(size - tail);
    if (f.read(buf.data(), tail) != tail) { err = "Can't read the download"; return false; }
    int at = -1;
    for (int i = (int)tail - 22; i >= 0; i--)
      if (buf[i] == 0x50 && buf[i + 1] == 0x4B && buf[i + 2] == 0x05 && buf[i + 3] == 0x06) { at = i; break; }
    if (at < 0) { err = "Not an EPUB (no zip directory)"; return false; }
    const uint16_t count = le16(&buf[at + 10]);
    const uint32_t cdSize = le32(&buf[at + 12]), cdOff = le32(&buf[at + 16]);
    if (cdOff + cdSize > size) { err = "Damaged EPUB"; return false; }
    std::vector<uint8_t> cd(cdSize);
    f.seek(cdOff);
    if (f.read(cd.data(), cdSize) != cdSize) { err = "Can't read the download"; return false; }
    size_t p = 0;
    for (uint16_t i = 0; i < count && p + 46 <= cdSize; i++) {
      if (le32(&cd[p]) != 0x02014B50) break;
      Entry e;
      e.method = le16(&cd[p + 10]);
      e.csize = le32(&cd[p + 20]);
      e.usize = le32(&cd[p + 24]);
      const uint16_t nl = le16(&cd[p + 28]), xl = le16(&cd[p + 30]), cl = le16(&cd[p + 32]);
      e.local = le32(&cd[p + 42]);
      if (p + 46 + nl > cdSize) break;
      e.name = String((const char*)&cd[p + 46], nl);
      entries.push_back(e);
      p += 46 + nl + xl + cl;
    }
    if (entries.empty()) { err = "Empty EPUB"; return false; }
    return true;
  }

  const Entry* find(const String& name) const {
    for (const auto& e : entries) if (e.name == name) return &e;
    for (const auto& e : entries) if (e.name.equalsIgnoreCase(name)) return &e;
    return nullptr;
  }

  // The file's bytes plus a 0 at the end (freed by the caller), or nullptr
  uint8_t* read(const Entry& e, String& err) {
    if (e.usize > 8u * 1024 * 1024) { err = "A page of the book is too big"; return nullptr; }
    uint8_t hdr[30];
    f.seek(e.local);
    if (f.read(hdr, 30) != 30 || le32(hdr) != 0x04034B50) { err = "Damaged EPUB"; return nullptr; }
    f.seek(e.local + 30 + le16(&hdr[26]) + le16(&hdr[28]));
    uint8_t* out = bigBuf(e.usize + 1);
    if (!out) { err = "Out of memory"; return nullptr; }
    out[e.usize] = 0;
    if (e.method == 0) {
      if (f.read(out, e.usize) != e.usize) { free(out); err = "Can't read the download"; return nullptr; }
      return out;
    }
    if (e.method != 8) { free(out); err = "Unsupported compression"; return nullptr; }
    uint8_t* in = bigBuf(e.csize);
    if (!in) { free(out); err = "Out of memory"; return nullptr; }
    const bool ok = f.read(in, e.csize) == e.csize && inflate(in, e.csize, out, e.usize);
    free(in);
    if (!ok) { free(out); err = "Damaged EPUB (bad compressed data)"; return nullptr; }
    return out;
  }

private:
  File f;
  std::vector<Entry> entries;
};

// attribute value in one tag's text: name="..." or name='...'
String attr(const String& tag, const char* name) {
  const String key = String(name) + "=";
  int p = 0;
  while ((p = tag.indexOf(key, p)) >= 0) {
    if (p == 0 || tag[p - 1] == ' ' || tag[p - 1] == '\t' || tag[p - 1] == '\n' || tag[p - 1] == '\r') {
      const char q = tag[p + key.length()];
      if (q == '"' || q == '\'') {
        const int s = p + key.length() + 1, e = tag.indexOf(q, s);
        if (e > s) return tag.substring(s, e);
      }
    }
    p += key.length();
  }
  return "";
}

// "Text/chapter%201.xhtml#x" relative to "OEBPS/" -> "OEBPS/Text/chapter 1.xhtml"
String resolve(const String& dir, String href) {
  const int hash = href.indexOf('#');
  if (hash >= 0) href = href.substring(0, hash);
  String dec;
  for (unsigned i = 0; i < href.length(); i++) {
    if (href[i] == '%' && i + 2 < href.length()) { dec += (char)strtol(href.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
    else dec += href[i];
  }
  std::vector<String> parts;
  String path = dir + dec, part;
  for (unsigned i = 0; i <= path.length(); i++) {
    if (i == path.length() || path[i] == '/') {
      if (part == "..") { if (!parts.empty()) parts.pop_back(); }
      else if (part.length() && part != ".") parts.push_back(part);
      part = "";
    } else {
      part += path[i];
    }
  }
  String out;
  for (size_t i = 0; i < parts.size(); i++) { if (i) out += '/'; out += parts[i]; }
  return out;
}

// Letters and digits only, lower case: to compare a heading with the book's title
String key(const String& s) {
  String k;
  for (unsigned i = 0; i < s.length(); i++) if (isalnum((unsigned char)s[i])) k += (char)tolower(s[i]);
  return k;
}

String trimmed(const String& s) { String t = s; t.trim(); return t; }

// Case-insensitive strstr (for "</script" etc.)
const char* findNoCase(const char* hay, const char* needle) {
  const size_t n = strlen(needle);
  for (; *hay; hay++) if (!strncasecmp(hay, needle, n)) return hay;
  return nullptr;
}

// ---------------------------------------------------------------------
//  XHTML -> text
// ---------------------------------------------------------------------
class Html2Text {
public:
  String out;
  String titleKey;
  std::function<String(const String&)> onImage;     // picture src -> its marker line ("" = none)

  void page(const char* p) {
    while (*p) {
      if (*p == '<') { p = tag(p); continue; }
      if (*p == '&') { p = entity(p); continue; }
      uint32_t c = (uint8_t)*p++;
      if (c >= 0xC0) {                                       // UTF-8
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        c &= 0x3F >> more;
        while (more-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
      }
      text(c);
    }
    block();
  }

  void finish() { block(); flushMarker(); }

private:
  String line, marker, heads;
  bool space = false;
  int heading = 0;

  void text(uint32_t c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0) { if (line.length()) space = true; return; }
    if (space) { line += ' '; space = false; }
    Gutenberg::ascii(line, c);
  }

  void flushMarker() {
    if (!marker.length()) return;
    out += '\x01';
    out += marker;
    out += "\x01\n";
    out += heads;
    out += '\n';
    marker = "";
    heads = "";
  }

  // End of a paragraph (or, with lineBreak, of a line inside one: <br>)
  void block(bool lineBreak = false) {
    space = false;
    if (heading) { if (line.length()) space = true; return; }   // <br> inside a heading
    const String t = trimmed(line);
    line = "";
    if (!t.length()) return;
    flushMarker();
    out += t;
    out += lineBreak ? "\n" : "\n\n";
  }

  void headingEnd() {
    const String t = trimmed(line);
    line = "";
    space = false;
    heading = 0;
    if (!t.length()) return;
    if (key(t) == titleKey) { flushMarker(); out += t; out += "\n\n"; return; }   // the book's own title
    const String name = t.length() > 60 ? t.substring(0, 60) : t;
    if (marker.length()) {                                   // straight after another heading: one chapter
      if (marker.length() + 1 + name.length() <= 60) { marker += ' '; marker += name; }
      heads += t;
      heads += '\n';
    } else {
      marker = name;
      heads = t + "\n";
    }
  }

  // Skips a tag (and script/style/head contents), acting on block and heading tags
  const char* tag(const char* p) {
    if (!strncmp(p, "<!--", 4)) { const char* e = strstr(p + 4, "-->"); return e ? e + 3 : p + strlen(p); }
    if (p[1] == '!' || p[1] == '?') { const char* e = strchr(p, '>'); return e ? e + 1 : p + strlen(p); }
    const bool closing = p[1] == '/';
    const char* n = p + (closing ? 2 : 1);
    char name[12];
    int len = 0;
    while (*n && !strchr(" \t\r\n/>", *n) && len < 11) name[len++] = tolower(*n++);
    name[len] = 0;
    const char* e = n;                                       // the end of the tag, minding quotes
    char q = 0;
    while (*e && (q || *e != '>')) { if (q) { if (*e == q) q = 0; } else if (*e == '"' || *e == '\'') q = *e; e++; }
    if (*e) e++;
    if (!closing && onImage && (!strcmp(name, "img") || !strcmp(name, "image"))) {   // <img src>, SVG <image href>
      const String t = String(" ") + String(n, e - n);
      String src = attr(t, "src");
      if (!src.length()) src = attr(t, "xlink:href");
      if (!src.length()) src = attr(t, "href");
      if (src.length() && !src.startsWith("data:") && !src.startsWith("http")) {
        const String m = onImage(src);
        if (m.length()) { block(); flushMarker(); out += m; out += "\n\n"; }
      }
      return e;
    }
    if (!closing && (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "head"))) {
      char endTag[16];
      snprintf(endTag, sizeof(endTag), "</%s", name);
      const char* x = findNoCase(e, endTag);
      if (!x) return e + strlen(e);
      const char* gt = strchr(x, '>');
      return gt ? gt + 1 : x + strlen(x);
    }
    const bool h = name[0] == 'h' && name[1] >= '1' && name[1] <= '3' && !name[2];
    if (h) {
      if (closing) headingEnd();
      else { block(); heading = name[1] - '0'; }
      return e;
    }
    if (!strcmp(name, "br")) { block(true); return e; }
    static const char* BLOCKS[] = { "p", "div", "h4", "h5", "h6", "li", "tr", "blockquote", "section", "article",
                                    "hr", "ul", "ol", "table", "dd", "dt", "pre", "figure", "figcaption", "body" };
    for (const char* b : BLOCKS) if (!strcmp(name, b)) { block(); break; }
    return e;
  }

  const char* entity(const char* p) {
    const char* semi = strchr(p, ';');
    if (!semi || semi - p > 10) { text('&'); return p + 1; }
    const String ent(p + 1, semi - p - 1);
    uint32_t c = 0;
    if (ent.startsWith("#x") || ent.startsWith("#X")) c = strtoul(ent.c_str() + 2, nullptr, 16);
    else if (ent.startsWith("#")) c = strtoul(ent.c_str() + 1, nullptr, 10);
    else {
      static const struct { const char* n; uint16_t c; } NAMED[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
        { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "lsquo", 0x2018 },
        { "rsquo", 0x2019 }, { "hellip", 0x2026 }, { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 },
        { "laquo", 0xAB }, { "raquo", 0xBB }, { "shy", 0xAD }, { "pound", 0xA3 }, { "eacute", 0xE9 },
        { "egrave", 0xE8 }, { "aacute", 0xE1 }, { "agrave", 0xE0 }, { "ccedil", 0xE7 }, { "uuml", 0xFC },
        { "ouml", 0xF6 }, { "auml", 0xE4 }, { "aelig", 0xE6 }, { "oelig", 0x153 }, { "middot", 0xB7 },
      };
      for (const auto& x : NAMED) if (ent == x.n) { c = x.c; break; }
      if (!c) { text('&'); return p + 1; }                   // unknown: leave it as written
    }
    if (c) text(c);
    return semi + 1;
  }
};

}  // namespace

// =====================================================================
//  The whole book
// =====================================================================
bool convert(const String& src, const String& dst, const String& title, const String& author,
             String& err, uint32_t* chapterCount, const Picture::Style* pictures, uint32_t* pictureCount) {
  Zip zip;
  if (!zip.open(src, err)) return false;

  // container.xml -> the OPF (manifest + reading order)
  const Entry* ce = zip.find("META-INF/container.xml");
  if (!ce) { err = "Not an EPUB (no container.xml)"; return false; }
  uint8_t* raw = zip.read(*ce, err);
  if (!raw) return false;
  String opfPath = attr(String((const char*)raw), "full-path");
  free(raw);
  const Entry* oe = opfPath.length() ? zip.find(opfPath) : nullptr;
  if (!oe) { err = "Not an EPUB (no OPF)"; return false; }
  raw = zip.read(*oe, err);
  if (!raw) return false;
  const String opf((const char*)raw);
  free(raw);
  const String opfDir = opfPath.lastIndexOf('/') >= 0 ? opfPath.substring(0, opfPath.lastIndexOf('/') + 1) : "";

  struct Item { String id, href; };
  std::vector<Item> manifest;
  for (int p = 0; (p = opf.indexOf("<item ", p)) >= 0;) {
    const int e = opf.indexOf('>', p);
    if (e < 0) break;
    const String t = String(" ") + opf.substring(p + 6, e);
    const String type = attr(t, "media-type");
    if (type.indexOf("html") >= 0) manifest.push_back({ attr(t, "id"), attr(t, "href") });
    p = e;
  }
  std::vector<String> spine;
  const int s0 = opf.indexOf("<spine"), s1 = opf.indexOf("</spine>");
  for (int p = s0; p >= 0 && (p = opf.indexOf("<itemref", p)) >= 0 && (s1 < 0 || p < s1);) {
    const int e = opf.indexOf('>', p);
    if (e < 0) break;
    const String id = attr(String(" ") + opf.substring(p + 8, e), "idref");
    for (const auto& m : manifest) if (m.id == id) { spine.push_back(resolve(opfDir, m.href)); break; }
    p = e;
  }
  if (spine.empty()) { err = "The EPUB has no pages"; return false; }

  // Pictures go in <book>.img/, fitted to the reading area (as the eReader lays it out)
  Html2Text h;
  String pageDir;
  uint32_t pics = 0;
  std::map<String, String> made;                       // picture path -> its marker (a picture used twice)
  const String base = dst.endsWith(".txt") ? dst.substring(0, dst.length() - 4) : dst;
  const String imgDir = base + ".img";
  Storage::removeTree(imgDir);                          // pictures from an earlier copy of this book
  if (pictures && Picture::supported()) {
    Storage::mkdirs(imgDir);
    const int maxW = SCREEN_W - 2 * UI::MARGIN - 3 * UI::T;
    const int maxH = SCREEN_H - (UI::BODY_Y + 2 * UI::T) - UI::T;
    h.onImage = [&](const String& srcRef) -> String {
      const String path = resolve(pageDir, srcRef);
      auto it = made.find(path);
      if (it != made.end()) return it->second;
      String marker, why;
      const Entry* ie = zip.find(path);
      if (ie) {
        String rerr;
        uint8_t* data = zip.read(*ie, rerr);
        if (data) {
          char name[12];
          snprintf(name, sizeof(name), "%03u.bmp", (unsigned)(pics + 1));
          int w = 0, ht = 0;
          if (Picture::convert(data, ie->usize, maxW, maxH, *pictures, imgDir + "/" + name, w, ht, why)) {
            pics++;
            marker = String("\x02img ") + name + " " + w + " " + ht + "\x02";
            Progress::note(String(pics) + (pics == 1 ? " picture" : " pictures"));
          } else {
            Serial.printf("[epub] picture %s skipped: %s\n", path.c_str(), why.c_str());
          }
          free(data);
        }
      }
      made[path] = marker;
      kb.service();
      return marker;
    };
  }

  // Every page, in reading order
  h.titleKey = key(Gutenberg::asciiOf(title));
  h.out.reserve(512 * 1024);
  for (size_t i = 0; i < spine.size(); i++) {
    const String& path = spine[i];
    Progress::update("Converting", i + 1, spine.size());
    const Entry* pe = zip.find(path);
    if (!pe) continue;
    uint8_t* page = zip.read(*pe, err);
    if (!page) return false;
    pageDir = path.lastIndexOf('/') >= 0 ? path.substring(0, path.lastIndexOf('/') + 1) : "";
    h.page((const char*)page);
    free(page);
    kb.service();
  }
  h.finish();
  String& t = h.out;

  // Project Gutenberg's licence pages: keep what's between START and END
  int from = 0, to = t.length();
  int s = t.indexOf("*** START OF");
  if (s < 0) s = t.indexOf("***START OF");
  if (s >= 0) { const int nl = t.indexOf("\n\n", s); from = nl >= 0 ? nl + 2 : t.length(); }
  int e = t.indexOf("*** END OF", from);
  if (e < 0) e = t.indexOf("***END OF", from);
  if (e >= 0) { const int nl = t.lastIndexOf("\n\n", e); to = nl >= from ? nl + 1 : e; }
  if (to <= from) { err = "No text in the EPUB"; return false; }

  uint32_t chapters = 0;
  for (int i = from; i < to; i++) if (t[i] == '\x01') chapters++;
  chapters /= 2;

  Storage::mkdirs(Storage::parentOf(dst));
  File out = SD.open(dst.c_str(), FILE_WRITE);
  if (!out) { err = "Can't write to the SD card"; return false; }
  String head;
  if (title.length()) {
    head = Gutenberg::asciiOf(title) + "\n";
    if (author.length()) head += String("by ") + Gutenberg::asciiOf(author) + "\n";
    head += "\n";
  }
  bool ok = out.print(head) == head.length();
  for (int i = from; ok && i < to; i += 4096) {
    const int n = std::min(4096, to - i);
    ok = out.write((const uint8_t*)t.c_str() + i, n) == (size_t)n;
  }
  out.close();
  if (!ok) { Storage::remove(dst); err = "The SD card is full"; return false; }
  if (chapterCount) *chapterCount = chapters;
  if (pictureCount) *pictureCount = pics;
  Serial.printf("[epub] %s: %u pages, %u chapters, %u pictures, %u bytes\n", dst.c_str(), (unsigned)spine.size(),
                (unsigned)chapters, (unsigned)pics, (unsigned)(to - from));
  return true;
}

}
