#include "net.h"
#include "config.h"
#include "storage.h"
#include "keyboard.h"
#include "webui.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <algorithm>

// =====================================================================
//  Net
// =====================================================================
namespace Net {

static bool ownWifi = false;           // we turned WiFi on (not the Uploader)

bool online() { return WiFi.status() == WL_CONNECTED; }
bool busy()   { return ownWifi; }

bool connect(String& err) {
  if (online()) return true;
  if (WebUI::mode() != WebUI::Mode::Off) { err = "The Uploader is using WiFi"; return false; }
  String ssid, pass;
  if (!WebUI::savedWifi(ssid, pass)) { err = "No WiFi saved yet (join one in the Uploader)"; return false; }
  Serial.printf("[net] joining %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  ownWifi = true;
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 15000) { err = String("Couldn't join ") + ssid; disconnect(); return false; }
    kb.service();                                  // keys typed meanwhile are kept
    delay(20);
  }
  Serial.printf("[net] online, %s\n", WiFi.localIP().toString().c_str());
  return true;
}

void disconnect() {
  if (!ownWifi) return;
  ownWifi = false;
  if (WebUI::mode() != WebUI::Mode::Off) return;   // the Uploader owns WiFi now
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[net] WiFi off");
}

// Where HTTPClient::writeToStream() puts the body: a capped String or a file.
// Reads the keyboard now and then, so keys typed during a download aren't lost.
class Sink : public Stream {
public:
  Sink(String* s, size_t cap) : _s(s), _cap(cap) {}
  Sink(File* f) : _f(f) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    if (_s) {
      if (_s->length() + n > _cap) { overflow = true; return 0; }
      _s->concat((const char*)buf, n);
    } else if (_f->write(buf, n) != n) {
      return 0;                                     // card full or gone
    }
    total += n;
    if (total - lastKb >= 4096) { lastKb = total; kb.service(); }
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t total = 0;
  bool overflow = false;
private:
  String* _s = nullptr;
  File* _f = nullptr;
  size_t _cap = 0;
  size_t lastKb = 0;
};

// "/cache/x.txt" or "x.txt" against the page that sent us there
static String resolve(const String& base, const String& loc) {
  if (loc.startsWith("http://") || loc.startsWith("https://")) return loc;
  const int hostEnd = base.indexOf('/', base.indexOf("//") + 2);
  const String origin = hostEnd < 0 ? base : base.substring(0, hostEnd);
  if (loc.startsWith("/")) return origin + loc;
  const int q = base.indexOf('?');
  const String path = q < 0 ? base : base.substring(0, q);
  return path.substring(0, path.lastIndexOf('/') + 1) + loc;
}

// One GET into a sink, following redirects ourselves (HTTPClient's own
// following can't change host or go from one secure connection to another);
// err says why if it fails
static long fetch(const String& url, Sink& sink, String& err) {
  if (!online()) { err = "Not online"; return -1; }
  String at = url;
  for (int hop = 0; hop < 6; hop++) {
    WiFiClientSecure client;
    client.setInsecure();               // public pages and books: no certificate store on the device
    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.setUserAgent("InkDeck/" FW_VERSION);
    const char* keep[] = { "Location" };
    if (!http.begin(client, at)) { err = "Bad address"; return -1; }
    http.collectHeaders(keep, 1);
    Serial.printf("[net] GET %s\n", at.c_str());
    const int code = http.GET();
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      const String loc = http.header("Location");
      http.end();
      if (!loc.length()) { err = String("Server said ") + code + " without a new address"; return -1; }
      at = resolve(at, loc);
      continue;
    }
    if (code != HTTP_CODE_OK) {
      err = code < 0 ? String("No answer (") + HTTPClient::errorToString(code) + ")" : String("Server said ") + code;
      http.end();
      return -1;
    }
    const int n = http.writeToStream(&sink);
    http.end();
    if (sink.overflow) { err = "Answer too big"; return -1; }
    if (n < 0) { err = String("Download broke off (") + HTTPClient::errorToString(n) + ")"; return -1; }
    Serial.printf("[net] got %u bytes\n", (unsigned)sink.total);
    return (long)sink.total;
  }
  err = "Too many redirects";
  return -1;
}

bool get(const String& url, String& out, size_t maxBytes, String& err) {
  out = "";
  Sink sink(&out, maxBytes);
  return fetch(url, sink, err) >= 0;
}

long download(const String& url, const String& path, String& err) {
  Storage::mkdirs(Storage::parentOf(path));
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) { err = "Can't write to the SD card"; return -1; }
  Sink sink(&f);
  const long n = fetch(url, sink, err);
  f.close();
  if (n < 0) Storage::remove(path);
  return n;
}

}

// =====================================================================
//  Gutenberg plain text -> eReader book
// =====================================================================
namespace Gutenberg {

// One Unicode character as plain ASCII (the same choices as the converter's
// transliterate(); the book fonts only have ASCII)
void ascii(String& s, uint32_t c) {
  if (c == '\t') { s += ' '; return; }
  if (c < 0x20) return;                                      // control codes (and our \x01 \x02 markers)
  if (c < 0x80) { s += (char)c; return; }
  static const char* LATIN1[64] = {                          // U+00C0 .. U+00FF
    "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I",
    "D","N","O","O","O","O","O","x","O","U","U","U","U","Y","Th","ss",
    "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","/","o","u","u","u","u","y","th","y" };
  if (c >= 0xC0 && c <= 0xFF) { s += LATIN1[c - 0xC0]; return; }
  switch (c) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: case 0x2039: case 0x203A: s += '\''; return;
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: case 0xAB: case 0xBB: s += '"'; return;
    case 0x2014: case 0x2015: s += "--"; return;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212: s += '-'; return;
    case 0x2026: s += "..."; return;
    case 0xA0: s += ' '; return;
    case 0x200B: case 0xFEFF: case 0xAD: case 0xFFFD: return;  // invisible
    case 0xA9: s += "(c)"; return;
    case 0xAE: s += "(R)"; return;
    case 0x2122: s += "(TM)"; return;
    case 0x2022: case 0xB7: s += '*'; return;
    case 0xBD: s += "1/2"; return;
    case 0xBC: s += "1/4"; return;
    case 0xBE: s += "3/4"; return;
    case 0xB0: s += " deg"; return;
    case 0xA3: s += 'L'; return;                             // pounds, as old books print it
    case 0xA7: s += "S."; return;
    case 0xA1: s += '!'; return;
    case 0xBF: s += '?'; return;
    case 0x152: s += "OE"; return;
    case 0x153: s += "oe"; return;
    case 0x3B1: s += "alpha"; return;
    case 0x3B2: s += "beta"; return;
    case 0x3B3: s += "gamma"; return;
    case 0x3B4: s += "delta"; return;
    case 0x3C0: s += "pi"; return;
    default: break;
  }
  if (c >= 0x2000 && c <= 0x200A) { s += ' '; return; }
  s += '?';
}

// UTF-8 text as plain ASCII
String asciiOf(const String& in) {
  String out;
  const uint8_t* p = (const uint8_t*)in.c_str();
  while (*p) {
    uint32_t c = *p++;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (more) c &= 0x3F >> more;
    while (more-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
    ascii(out, c);
  }
  return out;
}

// Reads a UTF-8 file line by line, as ASCII. lineStart = offset of the line.
class Lines {
public:
  explicit Lines(Storage::ByteReader& r) : _r(r) {}
  bool next(String& line, uint32_t& lineStart) {
    line = "";
    lineStart = _r.pos();
    int b = _r.read();
    if (b < 0) return false;
    while (b >= 0 && b != '\n') {
      uint32_t c = b;
      if (c >= 0xC0) {                                       // a multi-byte character
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        c &= 0x3F >> more;
        while (more--) { const int n = _r.read(); if (n < 0) break; c = (c << 6) | (n & 0x3F); }
      } else if (c >= 0x80) {
        c = 0xFFFD;                                          // stray byte (not UTF-8): dropped
      }
      if (c != '\r' && line.length() < 4000) ascii(line, c);
      b = _r.read();
    }
    while (line.length() && line[line.length() - 1] == ' ') line.remove(line.length() - 1);
    return true;
  }
private:
  Storage::ByteReader& _r;
};

static bool has(const String& upper, const char* what) { return upper.indexOf(what) >= 0; }

// "CHAPTER IV.", "Chapter 12: A Storm", "BOOK II", "STAVE ONE", "PREFACE", "XII."
static bool isHeading(const String& t) {
  if (!t.length() || t.length() > 60) return false;
  String u = t;
  u.toUpperCase();
  static const char* NUMBERED[] = { "CHAPTER", "BOOK", "PART", "STAVE", "ACT", "LETTER", "VOLUME", "CANTO", "SCENE" };
  for (const char* w : NUMBERED) {
    const size_t n = strlen(w);
    if (u.startsWith(w) && u.length() > n + 1 && (u[n] == ' ' || u[n] == '.')) return true;
  }
  static const char* ALONE[] = { "PREFACE", "PROLOGUE", "EPILOGUE", "INTRODUCTION", "CONCLUSION", "AFTERWORD", "FOREWORD" };
  for (const char* w : ALONE) {
    if (u == w || (u.startsWith(w) && (u[strlen(w)] == '.' || u[strlen(w)] == ' ' || u[strlen(w)] == ':'))) return true;
  }
  if (u.length() <= 8) {                                     // a Roman numeral on its own: "XII."
    for (size_t i = 0; i < u.length(); i++) if (!strchr("IVXLCDM.", u[i])) return false;
    return u[0] != '.';
  }
  return false;
}

bool convert(const String& src, const String& dst, const String& title, const String& author,
             String& err, uint32_t* chapterCount) {
  // 1. Where the book itself starts and ends (between the licence notices)
  uint32_t start = 0, end = 0xFFFFFFFF;
  {
    auto r = Storage::openReader(src);
    if (!r) { err = "Can't read the download"; return false; }
    Lines lines(*r);
    String line;
    uint32_t at;
    bool started = false;
    while (lines.next(line, at)) {
      String u = line;
      u.toUpperCase();
      if (!started && (has(u, "*** START OF") || has(u, "***START OF") || has(u, "*END*THE SMALL PRINT"))) {
        start = r->pos();
        started = true;
      } else if (started && (has(u, "*** END OF") || has(u, "***END OF") ||
                             u.startsWith("END OF THE PROJECT GUTENBERG") || u.startsWith("END OF PROJECT GUTENBERG"))) {
        end = at;
        break;
      }
    }
  }

  // 2. The text, paragraph by paragraph
  auto r = Storage::openReader(src, start);
  if (!r) { err = "Can't read the download"; return false; }
  Storage::mkdirs(Storage::parentOf(dst));
  File out = SD.open(dst.c_str(), FILE_WRITE);
  if (!out) { err = "Can't write to the SD card"; return false; }
  String buf;
  bool ok = true;
  auto emit = [&](const String& s) {
    buf += s;
    if (buf.length() >= 4096) { ok &= out.print(buf) == buf.length(); buf = ""; kb.service(); }
  };

  if (title.length()) {
    emit(asciiOf(title) + "\n");
    if (author.length()) emit(String("by ") + asciiOf(author) + "\n");
    emit("\n");
  }

  Lines lines(*r);
  String line, para;
  uint32_t at, chapters = 0;
  int blanks = 2;                    // as if after a gap: a heading on the first line counts
  bool prevShort = false, wrote = false;
  auto flush = [&]() {
    if (!para.length()) return;
    emit(para + "\n");
    para = "";
    wrote = true;
  };
  while (lines.next(line, at) && at < end) {
    if (!line.length()) { flush(); blanks++; continue; }
    int indent = 0;
    while (indent < (int)line.length() && line[indent] == ' ') indent++;
    String t = line.substring(indent);

    // Chapter heading: after a gap of two blank lines (a contents list has
    // single gaps), or after one if it isn't indented
    if (isHeading(t) && (blanks >= 2 || (blanks >= 1 && indent == 0))) {
      flush();
      if (wrote) emit("\n");
      emit(String("\x01") + t + "\x01\n" + t + "\n");
      chapters++;
      wrote = true;
      blanks = 0;
      prevShort = true;                                      // the next line starts its own paragraph
      continue;
    }

    if (!para.length()) {
      if (blanks && wrote) emit("\n");                       // one blank line between paragraphs
      para = String("    ").substring(0, std::min(indent, 4)) + t;
    } else if (indent > 0 || prevShort) {
      flush();                                               // verse, letters, lists: keep their lines
      para = String("    ").substring(0, std::min(indent, 4)) + t;
    } else {
      para += ' ';                                           // a hard-wrapped line: join it
      para += t;
    }
    if (para.length() > 3000) flush();                       // something without paragraph breaks
    prevShort = line.length() < 50;                          // Gutenberg wraps at ~70: shorter ends a line
    blanks = 0;
  }
  flush();
  if (buf.length()) ok &= out.print(buf) == buf.length();
  out.close();
  if (!ok) { Storage::remove(dst); err = "The SD card is full"; return false; }
  if (chapterCount) *chapterCount = chapters;
  Serial.printf("[gutenberg] %s: %u chapters\n", dst.c_str(), (unsigned)chapters);
  return true;
}

}
