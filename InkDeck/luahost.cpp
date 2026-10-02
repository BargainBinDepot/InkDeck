#include "luahost.h"
#include "config.h"
#include "ui.h"
#include "screen.h"
#include "storage.h"
#include "keyboard.h"
#include "apps.h"
#include "textengine.h"
#include "power.h"
#include "icons.h"
#include <map>
#include "clock.h"
#include "battery.h"
#include <time.h>
#include <algorithm>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoOblique9pt7b.h>
#include <Fonts/FreeMonoOblique12pt7b.h>
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
#include <Fonts/FreeMonoBoldOblique12pt7b.h>

extern "C" {
#include "src/lua/lua.h"
#include "src/lua/lauxlib.h"
#include "src/lua/lualib.h"
}

using namespace UI;

namespace LuaHost {

static lua_State* L = nullptr;
static String appDir, appName, errMsg;
static bool wantExit = false, hasTick = false;
static size_t used = 0;
static uint32_t callStart = 0, lastTick = 0;

// =====================================================================
//  Manifests
// =====================================================================
std::vector<AppInfo> readManifests() {
  std::vector<AppInfo> out;
  for (const auto& e : Storage::list(APPS_DIR)) {
    if (!e.dir) continue;
    AppInfo a;
    a.id = e.name;
    a.name = e.name;
    a.entry = "main.lua";
    a.image = "icon.bmp";

    String ini;
    if (Storage::readText(String(APPS_DIR) + "/" + e.name + "/app.ini", ini, 4096)) {
      int pos = 0;
      while (pos < (int)ini.length()) {
        int nl = ini.indexOf('\n', pos);
        if (nl < 0) nl = ini.length();
        String line = ini.substring(pos, nl);
        pos = nl + 1;
        line.trim();
        if (!line.length() || line[0] == '#' || line[0] == ';') continue;
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String k = line.substring(0, eq); k.trim(); k.toLowerCase();
        String v = line.substring(eq + 1); v.trim();
        if      (k == "name")       a.name = v;
        else if (k == "icon")       a.icon = v;
        else if (k == "entry")      a.entry = v;
        else if (k == "tab.label")  a.tabLabel = v;
        else if (k == "tab.dir")    a.tabDir = v;
        else if (k == "tab.accept") a.tabAccept = v;
        else if (k == "tab.page")   a.tabPage = v;
        else if (k == "tab.editor") { v.toLowerCase(); a.tabEditor = (v == "markdown" || v == "md") ? "markdown" : "text"; }
        else if (k == "image")      a.image = v;
        else if (k == "version")    a.version = v;
        else if (k == "author")     a.author = v;
        else if (k == "description") a.description = v;
      }
    }

    if (a.entry.indexOf("..") >= 0) continue;
    if (!Storage::exists(String(APPS_DIR) + "/" + e.name + "/" + a.entry)) continue;   // not a runnable app
    if (!a.icon.length()) a.icon = a.name.substring(0, 2);
    if (a.icon.length() > 3) a.icon = a.icon.substring(0, 3);
    if (a.name.length() > 16) a.name = a.name.substring(0, 16);
    while (a.tabDir.startsWith("/")) a.tabDir = a.tabDir.substring(1);
    if (a.tabDir.indexOf("..") >= 0) a.tabDir = "";
    while (a.image.startsWith("/")) a.image = a.image.substring(1);
    if (a.image.indexOf("..") >= 0) a.image = "";
    while (a.tabPage.startsWith("/")) a.tabPage = a.tabPage.substring(1);
    if (a.tabPage.indexOf("..") >= 0) a.tabPage = "";
    if (a.tabLabel.length() && !a.tabDir.length()) a.tabDir = "files";
    out.push_back(a);
  }
  std::sort(out.begin(), out.end(), [](const AppInfo& x, const AppInfo& y) {
    return strcasecmp(x.name.c_str(), y.name.c_str()) < 0;
  });
  return out;
}

// =====================================================================
//  Memory cap + runaway-script guard
// =====================================================================
static void* luaAlloc(void*, void* ptr, size_t osize, size_t nsize) {
  const size_t old = ptr ? osize : 0;          // when ptr is NULL, osize is a type tag, not a size
  if (nsize == 0) {
    free(ptr);
    used -= old;
    return nullptr;
  }
  if (used - old + nsize > (size_t)LUA_MEM_LIMIT_KB * 1024) return nullptr;   // Lua reports "not enough memory"
  void* p = psramFound() ? ps_realloc(ptr, nsize) : realloc(ptr, nsize);
  if (!p && psramFound()) p = realloc(ptr, nsize);
  if (p) used = used - old + nsize;
  return p;
}

static void timeoutHook(lua_State* Ls, lua_Debug*) {
  if (millis() - callStart > LUA_TIMEOUT_MS)
    luaL_error(Ls, "took longer than %d ms (endless loop?)", LUA_TIMEOUT_MS);
}

// Call the function + args already on the stack. On error, remember it and show it.
static bool call(int nargs, int nres) {
  callStart = millis();
  if (lua_pcall(L, nargs, nres, 0) != LUA_OK) {
    const char* m = lua_tostring(L, -1);
    errMsg = m ? m : "unknown error";
    lua_pop(L, 1);
    Serial.printf("[lua] %s error: %s\n", appName.c_str(), errMsg.c_str());
    AppMgr::requestRedraw(Refresh::Full);
    return false;
  }
  return true;
}

static bool pushGlobalFn(const char* name) {
  lua_getglobal(L, name);
  if (lua_isfunction(L, -1)) return true;
  lua_pop(L, 1);
  return false;
}

// =====================================================================
//  API: screen
// =====================================================================
static int num(lua_State* Ls, int i)              { return (int)luaL_checknumber(Ls, i); }
static int optNum(lua_State* Ls, int i, int def)  { return (int)luaL_optnumber(Ls, i, def); }

// ---- Fonts: "system" (built-in 6x8, scaled) or monospaced FreeMono in several sizes
struct FontDef { const char* name; const GFXfont* font; };
static const FontDef FONTS[] = {
  { "mono9",    &FreeMono9pt7b },          { "mono12",    &FreeMono12pt7b },          { "mono18",  &FreeMono18pt7b },
  { "mono9b",   &FreeMonoBold9pt7b },      { "mono12b",   &FreeMonoBold12pt7b },      { "mono18b", &FreeMonoBold18pt7b },
  { "mono9i",   &FreeMonoOblique9pt7b },   { "mono12i",   &FreeMonoOblique12pt7b },
  { "mono9bi",  &FreeMonoBoldOblique9pt7b }, { "mono12bi", &FreeMonoBoldOblique12pt7b },
};
static const int FONT_COUNT = sizeof(FONTS) / sizeof(FONTS[0]);
static const GFXfont* curFont = nullptr;     // nullptr = system font
static int fontAscent = 0, fontLH = LINE_H, fontCW = CW;

// Metrics are measured once per font, then reused (apps switch fonts a lot)
struct FontMetrics { bool ready = false; int ascent = 0, lh = 0, cw = 0; };
static FontMetrics metrics[FONT_COUNT];

static void selectFont(const char* name) {
  curFont = nullptr;
  int idx = -1;
  for (int i = 0; i < FONT_COUNT; i++) if (strcmp(FONTS[i].name, name) == 0) { idx = i; curFont = FONTS[i].font; }
  if (!curFont) { fontAscent = 0; fontLH = LINE_H; fontCW = CW; return; }
  FontMetrics& fm = metrics[idx];
  if (!fm.ready) {
    GFXcanvas1& c = screen.g();
    c.setFont(curFont);
    c.setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    c.getTextBounds("Mgjy|(", 0, 0, &x1, &y1, &w, &h);
    c.setFont(nullptr);
    fm.ascent = -y1;                           // baseline sits this far below the top
    fm.lh = h + 2;                             // tight line pitch, like the original reader
    fm.cw = curFont->glyph['M' - curFont->first].xAdvance;   // monospaced
    fm.ready = true;
  }
  fontAscent = fm.ascent;
  fontLH = fm.lh;
  fontCW = fm.cw;
}

// Draw text with its top-left corner at x,y in the current font
static void drawText(int x, int y, const char* s, int size) {
  if (!curFont) { screen.text(x, y, s, size); return; }
  GFXcanvas1& c = screen.g();
  c.setFont(curFont);
  c.setTextSize(size);
  c.setTextColor(INK);
  c.setCursor(x, y + fontAscent * size);
  c.print(s);
  c.setFont(nullptr);
}
static int textWidth(const char* s, int size) {
  return curFont ? (int)strlen(s) * fontCW * size : screen.textWidth(s, size);
}

static int l_text(lua_State* Ls) {
  drawText(num(Ls, 1), num(Ls, 2), luaL_checkstring(Ls, 3), optNum(Ls, 4, curFont ? 1 : T));
  return 0;
}
static int l_center(lua_State* Ls) {
  const char* s = luaL_checkstring(Ls, 2);
  const int size = optNum(Ls, 3, curFont ? 1 : T);
  drawText(SCREEN_W / 2 - textWidth(s, size) / 2, num(Ls, 1), s, size);
  return 0;
}
static int l_textwidth(lua_State* Ls) {
  const char* s = luaL_checkstring(Ls, 1);
  lua_pushinteger(Ls, textWidth(s, optNum(Ls, 2, curFont ? 1 : T)));
  return 1;
}
// screen.font([name]) -> char width, line height, ascent   (no name = system font)
static int l_font(lua_State* Ls) {
  selectFont(luaL_optstring(Ls, 1, "system"));
  lua_pushinteger(Ls, fontCW);
  lua_pushinteger(Ls, fontLH);
  lua_pushinteger(Ls, curFont ? fontAscent : 7 * T);   // baseline offset from the top of a line
  return 3;
}
static int l_rect(lua_State* Ls) {
  const int x = num(Ls, 1), y = num(Ls, 2), w = num(Ls, 3), h = num(Ls, 4);
  if (lua_toboolean(Ls, 5)) screen.g().fillRect(x, y, w, h, INK);
  else                      screen.g().drawRect(x, y, w, h, INK);
  return 0;
}
static int l_line(lua_State* Ls) {
  screen.g().drawLine(num(Ls, 1), num(Ls, 2), num(Ls, 3), num(Ls, 4), INK);
  return 0;
}
static int l_circle(lua_State* Ls) {
  const int x = num(Ls, 1), y = num(Ls, 2), r = num(Ls, 3);
  if (lua_toboolean(Ls, 4)) screen.g().fillCircle(x, y, r, INK);
  else                      screen.g().drawCircle(x, y, r, INK);
  return 0;
}
static int l_pixel(lua_State* Ls) {
  const bool on = lua_isnoneornil(Ls, 3) ? true : lua_toboolean(Ls, 3);
  screen.g().drawPixel(num(Ls, 1), num(Ls, 2), on ? INK : PAPER);
  return 0;
}
static int l_invert(lua_State* Ls) {
  screen.invertRect(num(Ls, 1), num(Ls, 2), num(Ls, 3), num(Ls, 4));
  return 0;
}
static int l_wrap(lua_State* Ls) {
  size_t len;
  const char* s = luaL_checklstring(Ls, 1, &len);
  const int cols = optNum(Ls, 2, TEXT_COLS);
  String str;
  str.reserve(len);
  for (size_t i = 0; i < len; i++) str += s[i];
  std::vector<String> lines = wrapText(str, cols < 1 ? 1 : cols);
  lua_createtable(Ls, lines.size(), 0);
  for (size_t i = 0; i < lines.size(); i++) {
    lua_pushstring(Ls, lines[i].c_str());
    lua_rawseti(Ls, -2, i + 1);
  }
  return 1;
}
static int l_redraw(lua_State* Ls) {
  AppMgr::requestRedraw(lua_toboolean(Ls, 1) ? Refresh::Full : Refresh::Partial);
  return 0;
}

// ---- Images: screen.image(name, x, y [, size]) draws a BMP from the app folder.
// size = the square box it's fitted into (small images are enlarged by whole
// numbers, big ones shrunk); default is the image's own size.
// screen.image(name, x, y, maxW, maxH) fits it inside that rectangle,
// shrinking if needed but never enlarging (book pictures).
// Images are loaded once and cached while the app runs (the oldest are
// dropped past IMAGE_CACHE_MAX, so a picture-heavy book can't fill memory).
static std::map<String, Icon> imageCache;
static std::vector<String> imageOrder;
static const size_t IMAGE_CACHE_MAX = 16;

static bool appPath(const char* name, String& out);   // defined with the file API below

static int l_image(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  const int x = num(Ls, 2), y = num(Ls, 3);
  const int boxW = optNum(Ls, 4, 0);
  const int boxH = optNum(Ls, 5, 0);
  String p;
  if (!appPath(name, p)) { lua_pushboolean(Ls, 0); return 1; }
  const String key = p + "@" + String(boxW) + "x" + String(boxH);
  auto it = imageCache.find(key);
  if (it == imageCache.end()) {
    Icon ic;
    const bool ok = boxH > 0 ? Icons::loadBmpFit(p, boxW, boxH, ic)
                             : Icons::loadBmp(p, boxW > 0 ? boxW : 512, ic);
    if (!ok) { lua_pushboolean(Ls, 0); return 1; }
    if (imageOrder.size() >= IMAGE_CACHE_MAX) { imageCache.erase(imageOrder.front()); imageOrder.erase(imageOrder.begin()); }
    it = imageCache.emplace(key, ic).first;
    imageOrder.push_back(key);
  }
  Icons::draw(it->second, x, y);
  lua_pushinteger(Ls, it->second.w);
  lua_pushinteger(Ls, it->second.h);
  return 2;
}

static const luaL_Reg screenLib[] = {
  { "text", l_text }, { "center", l_center }, { "textwidth", l_textwidth },
  { "rect", l_rect }, { "line", l_line }, { "circle", l_circle }, { "pixel", l_pixel },
  { "invert", l_invert }, { "wrap", l_wrap }, { "redraw", l_redraw }, { "font", l_font },
  { "image", l_image },
  { nullptr, nullptr }
};

// =====================================================================
//  API: sys
// =====================================================================
static int l_exit(lua_State*)       { wantExit = true; return 0; }
static int l_millis(lua_State* Ls)  { lua_pushinteger(Ls, millis()); return 1; }
static int l_title(lua_State* Ls)   { AppMgr::setTitle(luaL_optstring(Ls, 1, "")); return 0; }
static int l_mem(lua_State* Ls)     { lua_pushinteger(Ls, used); return 1; }
// sys.stayawake(true/false): keep the device from sleeping (e.g. a running timer)
static int l_stayawake(lua_State* Ls) { Power::keepAwake(lua_toboolean(Ls, 1)); return 0; }

// sys.time() -> seconds since 1970 (UTC), or nil if the clock isn't set
static int l_time(lua_State* Ls) {
  if (!Clock::valid()) { lua_pushnil(Ls); return 1; }
  lua_pushinteger(Ls, (lua_Integer)time(nullptr));
  return 1;
}

// sys.date([format [, time]]) -> local time as text (strftime format, default "%Y-%m-%d %H:%M"),
// or nil if the clock isn't set
static int l_date(lua_State* Ls) {
  const char* fmt = luaL_optstring(Ls, 1, "%Y-%m-%d %H:%M");
  time_t t = lua_isnoneornil(Ls, 2) ? time(nullptr) : (time_t)luaL_checkinteger(Ls, 2);
  if (lua_isnoneornil(Ls, 2) && !Clock::valid()) { lua_pushnil(Ls); return 1; }
  struct tm l;
  localtime_r(&t, &l);
  char buf[96];
  const size_t n = strftime(buf, sizeof(buf), fmt, &l);
  lua_pushlstring(Ls, buf, n);
  return 1;
}

// sys.clipboard([text]) -> clipboard text (sets it first if text is given)
static int l_clipboard(lua_State* Ls) {
  if (lua_gettop(Ls) >= 1 && !lua_isnil(Ls, 1)) {
    size_t len;
    const char* t = luaL_checklstring(Ls, 1, &len);
    String s;
    s.reserve(len);
    for (size_t i = 0; i < len; i++) s += t[i];
    Clipboard::set(s);
  }
  const String& c = Clipboard::get();
  lua_pushlstring(Ls, c.c_str(), c.length());
  return 1;
}

// sys.random(n) -> 1..n, sys.random(a, b) -> a..b: the ESP32's hardware random
// number generator, with rejection sampling so every result is equally likely
static uint32_t hwRandomRange(uint32_t range) {
  if (range == 0) return 0;
  const uint32_t limit = UINT32_MAX - (UINT32_MAX % range);
  uint32_t r;
  do { r = esp_random(); } while (r >= limit);
  return r % range;
}
static int l_random(lua_State* Ls) {
  lua_Integer lo = 1, hi;
  if (lua_gettop(Ls) >= 2) { lo = luaL_checkinteger(Ls, 1); hi = luaL_checkinteger(Ls, 2); }
  else hi = luaL_checkinteger(Ls, 1);
  if (hi < lo) return luaL_error(Ls, "random: empty range");
  lua_pushinteger(Ls, lo + (lua_Integer)hwRandomRange((uint32_t)(hi - lo + 1)));
  return 1;
}

// sys.battery() -> bars (0..4, 0 = empty), volts, charging; nil if there's no battery meter
static int l_battery(lua_State* Ls) {
  if (!Battery::present()) { lua_pushnil(Ls); return 1; }
  lua_pushinteger(Ls, Battery::level());
  lua_pushnumber(Ls, Battery::volts());
  lua_pushboolean(Ls, Battery::charging());
  return 3;
}

static const luaL_Reg sysLib[] = {
  { "exit", l_exit }, { "millis", l_millis }, { "title", l_title }, { "mem", l_mem },
  { "clipboard", l_clipboard }, { "time", l_time }, { "date", l_date }, { "random", l_random },
  { "stayawake", l_stayawake }, { "battery", l_battery },
  { nullptr, nullptr }
};

static int l_print(lua_State* Ls) {
  String line = "[lua] ";
  const int n = lua_gettop(Ls);
  for (int i = 1; i <= n; i++) {
    if (i > 1) line += "\t";
    line += luaL_tolstring(Ls, i, nullptr);
    lua_pop(Ls, 1);
  }
  Serial.println(line);
  return 0;
}

// =====================================================================
//  API: file — sandboxed to the app's own folder
//  (Lua errors longjmp past C++ destructors, so these functions read all
//   their Lua arguments first and report problems as nil + message.)
// =====================================================================
// Paths are relative to the app's folder. The one exception is the shared
// notes folder: "/notes" and "/notes/..." are open to every app.
static bool appPath(const char* name, String& out) {
  String n = name ? name : "";
  if (n.indexOf("..") >= 0) return false;
  if (n == NOTES_DIR || n.startsWith(String(NOTES_DIR) + "/")) { out = n; return true; }
  while (n.startsWith("/")) n = n.substring(1);
  out = n.length() ? appDir + "/" + n : appDir;
  return true;
}
static int fail(lua_State* Ls, const char* why) { lua_pushnil(Ls); lua_pushstring(Ls, why); return 2; }

static int l_read(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  String p, out;
  if (!appPath(name, p)) return fail(Ls, "bad file name");
  if (!Storage::exists(p) || Storage::isDir(p)) return fail(Ls, "can't read file");
  // Never hand back part of a file: an app that saves it again would lose the rest
  const uint32_t size = Storage::fileSize(p);
  if (size > LUA_FILE_MAX) {
    char why[64];
    snprintf(why, sizeof(why), "too big to open (%lu KB, max %d)",
             (unsigned long)((size + 1023) / 1024), LUA_FILE_MAX / 1024);
    return fail(Ls, why);
  }
  if (!Storage::readText(p, out, LUA_FILE_MAX)) return fail(Ls, "can't read file");
  lua_pushlstring(Ls, out.c_str(), out.length());
  return 1;
}
static int l_write(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  size_t len;
  const char* d = luaL_checklstring(Ls, 2, &len);
  String p;
  if (!appPath(name, p)) return fail(Ls, "bad file name");
  String data;
  data.reserve(len);
  for (size_t i = 0; i < len; i++) data += d[i];
  lua_pushboolean(Ls, Storage::writeText(p, data));
  return 1;
}
static int l_exists(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  String p;
  lua_pushboolean(Ls, appPath(name, p) && Storage::exists(p));
  return 1;
}
static int l_size(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  String p;
  lua_pushinteger(Ls, appPath(name, p) ? Storage::fileSize(p) : 0);
  return 1;
}
static int l_remove(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  String p;
  if (!appPath(name, p)) return fail(Ls, "bad file name");
  if (p == appDir) return fail(Ls, "can't remove the app's own folder");
  lua_pushboolean(Ls, Storage::removeTree(p));
  return 1;
}
static int l_rename(lua_State* Ls) {
  const char* from = luaL_checkstring(Ls, 1);
  const char* to = luaL_checkstring(Ls, 2);
  String a, b;
  if (!appPath(from, a) || !appPath(to, b)) return fail(Ls, "bad file name");
  lua_pushboolean(Ls, Storage::rename(a, b));
  return 1;
}
static int l_mkdir(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  String p;
  if (!appPath(name, p)) return fail(Ls, "bad file name");
  lua_pushboolean(Ls, Storage::mkdirs(p));
  return 1;
}
static int l_list(lua_State* Ls) {
  const char* name = luaL_optstring(Ls, 1, "");
  String p;
  if (!appPath(name, p)) return fail(Ls, "bad folder name");
  Storage::mkdirs(p);                       // e.g. a web-tab folder nobody has uploaded to yet
  auto ents = Storage::list(p);
  lua_createtable(Ls, ents.size(), 0);
  for (size_t i = 0; i < ents.size(); i++) {
    lua_createtable(Ls, 0, 3);
    lua_pushstring(Ls, ents[i].name.c_str());  lua_setfield(Ls, -2, "name");
    lua_pushboolean(Ls, ents[i].dir);          lua_setfield(Ls, -2, "dir");
    const uint32_t sz = ents[i].size != Storage::SIZE_UNKNOWN ? ents[i].size : Storage::fileSize(p + "/" + ents[i].name);
    lua_pushinteger(Ls, sz);                   lua_setfield(Ls, -2, "size");
    lua_rawseti(Ls, -2, i + 1);
  }
  return 1;
}

static const luaL_Reg fileLib[] = {
  { "read", l_read }, { "write", l_write }, { "exists", l_exists }, { "size", l_size },
  { "remove", l_remove }, { "rename", l_rename }, { "mkdir", l_mkdir }, { "list", l_list },
  { nullptr, nullptr }
};

// =====================================================================
//  API: text — pagination engine for long files (see textengine.h)
// =====================================================================
// text.paginate(name, cols, rows) -> pages {offset,...}, chapters {{title=, page=, offset=},...}
static int l_paginate(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  const int cols = num(Ls, 2), rows = num(Ls, 3);
  const int lh = optNum(Ls, 4, 0);             // line height: lets images take their real space
  std::vector<uint32_t> pages;
  std::vector<TextEngine::Chapter> chaps;
  {
    String p;
    if (!appPath(name, p)) return fail(Ls, "bad file name");
    auto r = Storage::openReader(p);
    if (!r) return fail(Ls, "can't open file");
    const uint32_t t = millis();
    TextEngine::paginate(*r, cols, rows, lh, pages, chaps);
    Serial.printf("[lua] paginated %s: %u pages, %u chapters in %lu ms\n", name,
                  (unsigned)pages.size(), (unsigned)chaps.size(), (unsigned long)(millis() - t));
  }
  callStart = millis();                        // the scan doesn't count against the app's time limit
  lua_createtable(Ls, pages.size(), 0);
  for (size_t i = 0; i < pages.size(); i++) { lua_pushinteger(Ls, pages[i]); lua_rawseti(Ls, -2, i + 1); }
  lua_createtable(Ls, chaps.size(), 0);
  for (size_t i = 0; i < chaps.size(); i++) {
    // page = last page starting at or before the marker
    const size_t pg = std::upper_bound(pages.begin(), pages.end(), chaps[i].offset) - pages.begin();
    lua_createtable(Ls, 0, 3);
    lua_pushstring(Ls, chaps[i].title.c_str()); lua_setfield(Ls, -2, "title");
    lua_pushinteger(Ls, pg ? pg : 1);           lua_setfield(Ls, -2, "page");
    lua_pushinteger(Ls, chaps[i].offset);       lua_setfield(Ls, -2, "offset");
    lua_rawseti(Ls, -2, i + 1);
  }
  return 2;
}

// text.page(name, offset, cols, rows) -> { line, line, ... }
static int l_page(lua_State* Ls) {
  const char* name = luaL_checkstring(Ls, 1);
  const uint32_t offset = (uint32_t)luaL_checknumber(Ls, 2);
  const int cols = num(Ls, 3), rows = num(Ls, 4);
  const int lh = optNum(Ls, 5, 0);
  std::vector<String> lines;
  {
    String p;
    if (!appPath(name, p)) return fail(Ls, "bad file name");
    auto r = Storage::openReader(p, offset);
    if (!r) return fail(Ls, "can't open file");
    lines = TextEngine::pageLines(*r, cols, rows, lh);
  }
  lua_createtable(Ls, lines.size(), 0);
  for (size_t i = 0; i < lines.size(); i++) { lua_pushstring(Ls, lines[i].c_str()); lua_rawseti(Ls, -2, i + 1); }
  return 1;
}

static const luaL_Reg textLib[] = {
  { "paginate", l_paginate }, { "page", l_page },
  { nullptr, nullptr }
};

// =====================================================================
//  State setup
// =====================================================================
static void setIntField(const char* k, int v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); }

static void openLibs() {
  luaL_requiref(L, LUA_GNAME,       luaopen_base,      1); lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table,     1); lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string,    1); lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math,      1); lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8,      1); lua_pop(L, 1);
  luaL_requiref(L, LUA_COLIBNAME,   luaopen_coroutine, 1); lua_pop(L, 1);

  // Sandbox: no direct filesystem access outside the file API
  lua_pushnil(L); lua_setglobal(L, "dofile");
  lua_pushnil(L); lua_setglobal(L, "loadfile");
  lua_pushcfunction(L, l_print); lua_setglobal(L, "print");

  luaL_newlib(L, screenLib);
  setIntField("W", SCREEN_W);
  setIntField("H", SCREEN_H);
  setIntField("top", BODY_Y);          // first row below the status bar
  setIntField("scale", T);             // 1 on 2.9", 2 on 3.7"
  setIntField("cw", CW);               // character width at default text size
  setIntField("ch", CH);               // character height at default text size
  setIntField("lh", LINE_H);           // comfortable line pitch
  setIntField("cols", TEXT_COLS);      // characters per line with a margin
  setIntField("margin", MARGIN);
  lua_setglobal(L, "screen");

  luaL_newlib(L, sysLib);
  lua_setglobal(L, "sys");

  luaL_newlib(L, fileLib);
  lua_setglobal(L, "file");

  luaL_newlib(L, textLib);
  lua_setglobal(L, "text");

  lua_createtable(L, 0, 8);
  setIntField("LEFT", K_LEFT);   setIntField("RIGHT", K_RIGHT);
  setIntField("UP", K_UP);       setIntField("DOWN", K_DOWN);
  setIntField("ENTER", K_ENTER); setIntField("BKSP", K_BKSP);
  setIntField("TAB", K_TAB);     setIntField("ESC", K_ESC);
  setIntField("DEL", K_DEL);
  lua_setglobal(L, "keys");
}

// =====================================================================
//  Lifecycle
// =====================================================================
bool open(const AppInfo& a) {
  close();
  appDir = String(APPS_DIR) + "/" + a.id;
  appName = a.name;
  errMsg = "";
  wantExit = false;
  used = 0;
  lastTick = 0;
  selectFont("system");

  L = lua_newstate(luaAlloc, nullptr);
  if (!L) { errMsg = "not enough memory to start"; return false; }
  lua_sethook(L, timeoutHook, LUA_MASKCOUNT, 10000);
  openLibs();

  String src;
  if (!Storage::readText(appDir + "/" + a.entry, src, LUA_SOURCE_MAX)) {
    errMsg = "can't read " + a.entry;
    return false;
  }
  const String chunk = "@" + a.id + "/" + a.entry;
  if (luaL_loadbuffer(L, src.c_str(), src.length(), chunk.c_str()) != LUA_OK) {
    const char* m = lua_tostring(L, -1);
    errMsg = m ? m : "syntax error";
    lua_pop(L, 1);
    Serial.printf("[lua] %s load error: %s\n", a.name.c_str(), errMsg.c_str());
    return false;
  }
  if (!call(0, 0)) return false;               // run the top level (defines the functions)

  hasTick = pushGlobalFn("tick");
  if (hasTick) lua_pop(L, 1);
  if (pushGlobalFn("init")) call(0, 0);

  Serial.printf("[lua] started %s (%u KB)\n", a.name.c_str(), (unsigned)(used / 1024));
  return errMsg.length() == 0;
}

void close() {
  imageCache.clear();
  imageOrder.clear();
  Power::keepAwake(false);                     // an app's stay-awake request ends with the app
  if (L) {
    lua_close(L);
    L = nullptr;
    Serial.printf("[lua] closed %s\n", appName.c_str());
  }
  errMsg = "";
  hasTick = false;
  wantExit = false;
}

bool exitRequested() { return wantExit; }
size_t memUsed()     { return used; }

void key(uint8_t k) {
  if (!L || errMsg.length()) return;
  if (!pushGlobalFn("key")) return;
  lua_pushinteger(L, k);
  if (isPrintableKey(k)) { char c[2] = { (char)k, 0 }; lua_pushstring(L, c); }
  else lua_pushnil(L);
  if (const char* fn = fnKeyName(k)) lua_pushstring(L, fn);   // Fn combo: "c", "enter", "left"...
  else lua_pushnil(L);
  if (call(3, 1)) {
    const bool noRedraw = lua_isboolean(L, -1) && !lua_toboolean(L, -1);   // key() returned false
    lua_pop(L, 1);
    if (!noRedraw) AppMgr::requestRedraw(Refresh::Partial);
  }
}

bool back() {
  if (!L || errMsg.length()) return false;
  if (!pushGlobalFn("back")) return false;
  if (!call(0, 1)) return true;                 // stay so the error is visible
  const bool handled = lua_toboolean(L, -1);
  lua_pop(L, 1);
  if (handled) AppMgr::requestRedraw(Refresh::Partial);
  return handled;
}

void tick() {
  if (!L || errMsg.length() || !hasTick) return;
  if (millis() - lastTick < LUA_TICK_MS) return;
  lastTick = millis();
  if (pushGlobalFn("tick")) call(0, 0);
}

static void drawError() {
  screen.g().fillRect(0, BODY_Y, SCREEN_W, SCREEN_H - BODY_Y, PAPER);
  screen.red().fillRect(0, BODY_Y, SCREEN_W, SCREEN_H - BODY_Y, PAPER);
  int y = BODY_Y + 3 * T;
  screen.text(MARGIN, y, "App error", 2 * T);
  y += 2 * CH + 4 * T;
  for (const auto& line : wrapText(errMsg, TEXT_COLS)) {
    if (y > SCREEN_H - 2 * LINE_H) break;
    screen.text(MARGIN, y, line.c_str(), T);
    y += LINE_H;
  }
  screen.text(MARGIN, SCREEN_H - LINE_H, "Esc = exit", T);
}

void draw() {
  if (errMsg.length()) { drawError(); return; }
  if (!L) return;
  if (pushGlobalFn("draw") && !call(0, 0)) drawError();
}

}
