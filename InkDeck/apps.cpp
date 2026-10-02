#include "apps.h"
#include "ui.h"
#include "keyboard.h"
#include "storage.h"
#include "luahost.h"
#include "webui.h"
#include "icons.h"
#include "icons_builtin.h"
#include "clock.h"
#include "power.h"
#include "ota.h"
#include <algorithm>
#include <vector>

using namespace UI;

namespace Clipboard {
  static String clip;
  void set(const String& text) { clip = text; Serial.printf("[clip] %u chars\n", (unsigned)text.length()); }
  const String& get() { return clip; }
}

// Word-wrap a string into screen lines (long words still get split)
std::vector<String> wrapText(const String& s, int cols) {
  std::vector<String> lines(1);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\r') continue;
    if (c == '\n') { lines.emplace_back(); continue; }
    if (c == '\t') c = ' ';
    if ((int)lines.back().length() >= cols) {
      if (c == ' ') { lines.emplace_back(); continue; }        // wrap at the space, drop it
      const int sp = lines.back().lastIndexOf(' ');
      if (sp > 0) {                                           // move the partial word down
        String carry = lines.back().substring(sp + 1);
        lines.back() = lines.back().substring(0, sp);
        lines.push_back(carry);
      } else {
        lines.emplace_back();                                 // one huge word: hard split
      }
    }
    lines.back() += c;
  }
  return lines;
}

// Draw a selectable list; keeps `sel` visible by adjusting `top`
static void drawList(const std::vector<String>& items, int sel, int& top, int y0, int rows) {
  if (sel < top) top = sel;
  if (sel >= top + rows) top = sel - rows + 1;
  for (int r = 0; r < rows && top + r < (int)items.size(); r++) {
    const int y = y0 + r * LINE_H;
    screen.text(MARGIN, y + T, items[top + r].c_str(), T);
    if (top + r == sel) screen.invertRect(0, y, SCREEN_W, LINE_H);
  }
}

static String humanSize(uint32_t b) {
  char buf[12];
  if (b < 1024)             snprintf(buf, sizeof(buf), "%luB", (unsigned long)b);
  else if (b < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1fK", b / 1024.0);
  else                      snprintf(buf, sizeof(buf), "%.1fM", b / (1024.0 * 1024.0));
  return String(buf);
}

// =====================================================================
//  Native apps (forward declarations)
// =====================================================================
static void filesEnter();   static void filesKey(uint8_t k);   static void filesDraw();   static bool filesBack();
static void upEnter();      static void upKey(uint8_t k);      static void upDraw();      static bool upBack();   static void upTick();
static void keyTestEnter(); static void keyTestKey(uint8_t k); static void keyTestDraw();
static void sysEnter();  static void sysKey(uint8_t k);  static void sysDraw();  static void sysTick();  static bool sysBack();

// Built-in apps. The home screen shows these and the Lua apps together, A-Z.
static const App NATIVE_APPS[] = {
  { "Files",    "SD", filesEnter,   filesKey,   filesDraw,   filesBack, nullptr, -1, ICON_FILES,
    "Browse the SD card and read text files." },
  { "Uploader", "Up", upEnter,      upKey,      upDraw,      upBack,    upTick,  -1, ICON_UPLOADER,
    "Install apps and move files from a web browser over WiFi." },
  { "Key Test", "Kb", keyTestEnter, keyTestKey, keyTestDraw, nullptr,   nullptr, -1, ICON_KEYTEST,
    "Shows the code each keyboard key sends." },
  { "System",   "i",  sysEnter,     sysKey,     sysDraw,     sysBack,   sysTick, -1, ICON_SYSTEM,
    "Hardware, memory, storage and display details." },
};

static std::vector<LuaHost::AppInfo> luaApps;
static std::vector<App> apps;
static int NUM_APPS = 0;

// =====================================================================
//  App manager state
// =====================================================================
static int  current = -1;                 // -1 = launcher
static bool dirty = true;
static Refresh pending = Refresh::Full;
static uint32_t lastKeyMs = 0;            // for REFRESH_SETTLE_MS
static String customTitle;


// Mac OS 7 style title bar: pinstripes, the title centred on a white gap,
// a close box on the left inside apps (Esc closes), and the clock on the right.
static void drawStatusBar(const char* title, bool inApp) {
  GFXcanvas1& g = screen.g();
  const int h = STATUS_H - T;                                  // bar height above the bottom rule

  // Pinstripes: T-thick lines every 2T, inset from the top, bottom and sides
  for (int y = 2 * T; y + T <= h - T; y += 2 * T) g.fillRect(T, y, SCREEN_W - 2 * T, T, INK);

  // Clock on the right, on a white gap, with a short stripe tail after it
  const String clk = Power::sleeping() ? String("zZ") : Clock::hhmm();   // asleep: don't show a stale time
  const int cw = screen.textWidth(clk.c_str(), T);
  const int clockX = SCREEN_W - 5 * T - cw;
  g.fillRect(clockX - 2 * T, 0, cw + 4 * T, h, PAPER);
  screen.text(clockX, 2 * T, clk.c_str(), T);

  // Close box on the left while an app is open
  int leftLimit = T;
  if (inApp) {
    const int bs = h - 4 * T;                                  // box size
    const int bx = 5 * T;
    g.fillRect(bx - 2 * T, 0, bs + 4 * T, h, PAPER);
    for (int i = 0; i < T; i++) g.drawRect(bx + i, 2 * T + i, bs - 2 * i, bs - 2 * i, INK);
    leftLimit = bx + bs + 2 * T;
  }

  // Title, centred and bold (drawn twice, one pixel apart), trimmed to fit between the boxes
  const int half = std::min(SCREEN_W / 2 - leftLimit, clockX - 2 * T - SCREEN_W / 2) - 4 * T;
  const int maxChars = std::max(3, 2 * half / CW);
  String t = title;
  if ((int)t.length() > maxChars) t = t.substring(0, maxChars - 1) + "~";
  const int tw = screen.textWidth(t.c_str(), T) + 1;
  const int tx = (SCREEN_W - tw) / 2;
  g.fillRect(tx - 3 * T, 0, tw + 6 * T, h, PAPER);
  screen.text(tx, 2 * T, t.c_str(), T);
  screen.text(tx + 1, 2 * T, t.c_str(), T);

  g.fillRect(0, h, SCREEN_W, T, INK);                          // rule under the bar
}

// =====================================================================
//  Home screen — a Finder-style window of icons, with folders.
//  Arrows move (Down on the bottom row / Up on the top row changes page),
//  Enter opens, Esc closes a folder. Tab opens the menu:
//    Get Info (i), Move (m), Move to Folder (f), New Folder (n),
//    Rename / Delete Folder, Sort A-Z.
//  The arrangement is saved in /system/home.txt; until you change it,
//  everything is A-Z and new apps are added at the end.
// =====================================================================
static const int COLS = 4, ROWS = 2, PER_PAGE = COLS * ROWS;
static const int SB_W   = 8 * T + 1;                           // Mac scroll bar on the right
static const int GRID_Y = BODY_Y + T;
static const int CELL_W = (SCREEN_W - SB_W) / COLS;
static const int CELL_H = (SCREEN_H - GRID_Y - T) / ROWS;
static const int ICON_FIT = std::min(CELL_W - 6 * T, CELL_H - CH - 6 * T);
static const int ICON_BOX = ICON_FIT > 64 ? 64 : ICON_FIT;

struct Tile   { int app; int folder; };                        // one of the two is >= 0
struct Folder { String name; std::vector<Tile> tiles; };
static std::vector<Tile>   homeTiles;
static std::vector<Folder> folders;
static int    inFolder = -1;                                   // open folder, -1 = the home screen
static String inFolderName;                                    // survives rebuilds of the app list

static int page = 0, sel = 0;
static std::vector<Icon> appIcons;                             // parallel to apps
static Icon folderIcon;

enum class HomeUi { Grid, Info, Menu, Move, Picker, Name };
static HomeUi ui = HomeUi::Grid;
static uint64_t infoBytes = 0;
static uint32_t infoFiles = 0;

enum { A_OPEN, A_INFO, A_MOVE, A_TOFOLDER, A_NEWFOLDER, A_RENAME, A_DELFOLDER, A_SORT, A_CLOSE };
struct MenuItem { const char* label; const char* key; int action; };
static std::vector<MenuItem> menu;
static int menuSel = 0;

static std::vector<Tile> moveBackup;                           // to undo a Move with Esc
static int movePage = 0, moveSel = 0;

struct PickItem { String label; int target; };                 // target: folder index, -1 home, -2 new folder
static std::vector<PickItem> picker;
static int pickSel = 0;

static String nameText;
static int nameMode = 0;                                       // 0 new folder, 1 rename, 2 new folder for the selected app

static std::vector<Tile>& view() { return inFolder >= 0 ? folders[inFolder].tiles : homeTiles; }
static int  viewCount()          { return view().size(); }
static int  selIndex()           { return page * PER_PAGE + sel; }
static Tile& selTile()           { return view()[selIndex()]; }
static const char* tileName(const Tile& t) { return t.folder >= 0 ? folders[t.folder].name.c_str() : apps[t.app].name; }

static int pageCount()        { const int n = viewCount(); return n ? (n + PER_PAGE - 1) / PER_PAGE : 1; }
static int countOnPage(int p) { int n = viewCount() - p * PER_PAGE; return n > PER_PAGE ? PER_PAGE : (n < 0 ? 0 : n); }
static int rowsOnPage(int p)  { return (countOnPage(p) + COLS - 1) / COLS; }

static void clampSel() {
  if (page >= pageCount()) page = pageCount() - 1;
  if (page < 0) page = 0;
  if (sel >= countOnPage(page)) sel = countOnPage(page) - 1;
  if (sel < 0) sel = 0;
}
static void selectIndex(int i) { page = i / PER_PAGE; sel = i % PER_PAGE; clampSel(); }

// ---- Saved layout ------------------------------------------------------
static String appKey(int i) { return apps[i].lua >= 0 ? "lua:" + luaApps[apps[i].lua].id : String("app:") + apps[i].name; }

static void saveLayout() {
  String out;
  for (const Tile& t : homeTiles) {
    if (t.folder >= 0) {
      out += "folder:" + folders[t.folder].name + "\n";
      for (const Tile& c : folders[t.folder].tiles) out += "  " + appKey(c.app) + "\n";
    } else {
      out += appKey(t.app) + "\n";
    }
  }
  Storage::writeText(HOME_LAYOUT_FILE, out);
}

static void loadLayout() {
  homeTiles.clear();
  folders.clear();
  std::vector<bool> placed(apps.size(), false);
  auto findApp = [&](const String& key) { for (size_t i = 0; i < apps.size(); i++) if (appKey(i) == key) return (int)i; return -1; };

  String text;
  if (Storage::readText(HOME_LAYOUT_FILE, text, 16384)) {
    int cur = -1;
    int pos = 0;
    while (pos < (int)text.length()) {
      int nl = text.indexOf('\n', pos);
      if (nl < 0) nl = text.length();
      String line = text.substring(pos, nl);
      pos = nl + 1;
      const bool inner = line.startsWith("  ");
      line.trim();
      if (!line.length()) continue;
      if (line.startsWith("folder:")) {
        folders.push_back({ line.substring(7), {} });
        homeTiles.push_back({ -1, (int)folders.size() - 1 });
        cur = folders.size() - 1;
        continue;
      }
      const int a = findApp(line);
      if (a < 0 || placed[a]) continue;                      // app was removed, or listed twice
      placed[a] = true;
      if (inner && cur >= 0) folders[cur].tiles.push_back({ a, -1 });
      else { homeTiles.push_back({ a, -1 }); cur = -1; }
    }
  }
  for (size_t i = 0; i < apps.size(); i++)                     // new apps (A-Z) go at the end
    if (!placed[i]) homeTiles.push_back({ (int)i, -1 });

  inFolder = -1;
  for (size_t f = 0; f < folders.size(); f++) if (folders[f].name == inFolderName) inFolder = f;
  if (inFolder < 0) inFolderName = "";
}

// ---- Drawing ---------------------------------------------------------------
// Icon image if the app has one, otherwise its letters in a frame
static void drawAppIcon(int idx, int x, int y, int box) {
  const Icon& ic = appIcons[idx];
  if (ic.valid()) {
    Icons::draw(ic, x + (box - ic.w) / 2, y + (box - ic.h) / 2);
    return;
  }
  for (int b = 0; b < 2 * T; b++) screen.g().drawRect(x + b, y + b, box - 2 * b, box - 2 * b, INK);
  const char* glyph = apps[idx].icon;
  const int len = strlen(glyph) ? strlen(glyph) : 1;
  const int inner = box - 8 * T;
  int gs = std::min(inner / (6 * len), inner / 8);
  gs = std::max(1, std::min(gs, 3 * T));
  screen.textCentered(x + box / 2, y + (box - 8 * gs) / 2 + gs / 2, glyph, gs);
}

static void drawTileIcon(const Tile& t, int x, int y, int box) {
  if (t.folder >= 0) Icons::draw(folderIcon, x + (box - folderIcon.w) / 2, y + (box - folderIcon.h) / 2);
  else               drawAppIcon(t.app, x, y, box);
}

// Label: normal spacing if it fits, else letters squeezed a pixel closer, else trimmed.
// Returns the drawn width.
static int drawLabel(const char* name, int cx, int cw, int y) {
  // Squeeze long labels a little so neighbours keep a visible gap (and "Checklist" fits)
  String label = name;
  int adv = CW;
  if ((int)label.length() * CW > cw - 6 * T) {
    adv = CW - 1;
    const int maxChars = cw / adv;
    if ((int)label.length() > maxChars) label = label.substring(0, maxChars);
  }
  const int lw = (int)label.length() * adv;
  const int lx = cx + (cw - lw) / 2;
  if (adv == CW) screen.text(lx, y, label.c_str(), T);
  else { char one[2] = { 0, 0 }; for (size_t c = 0; c < label.length(); c++) { one[0] = label[c]; screen.text(lx + c * adv, y, one, T); } }
  return lw;
}

static void dottedRect(int x, int y, int w, int h) {           // "marching ants" outline
  GFXcanvas1& g = screen.g();
  for (int i = 0; i < w; i += 2 * T) { g.fillRect(x + i, y, T, T, INK); g.fillRect(x + i, y + h - T, T, T, INK); }
  for (int j = 0; j < h; j += 2 * T) { g.fillRect(x, y + j, T, T, INK); g.fillRect(x + w - T, y + j, T, T, INK); }
}

static void drawScrollBar() {
  GFXcanvas1& g = screen.g();
  const int sx = SCREEN_W - SB_W, top = BODY_Y, bottom = SCREEN_H;
  const int pages = pageCount();
  g.fillRect(sx, top, T, bottom - top, INK);                   // left edge
  // arrow boxes
  auto arrow = [&](int by, bool up) {
    g.fillRect(sx, up ? by + SB_W - T : by, SB_W, T, INK);
    const int cx = sx + SB_W / 2 + T / 2, t0 = by + 2 * T + 1, t1 = by + SB_W - 2 * T - 1;
    if (up) g.drawTriangle(cx, t0, sx + 2 * T + 1, t1, sx + SB_W - 2 * T, t1, INK);
    else    g.drawTriangle(cx, t1, sx + 2 * T + 1, t0, sx + SB_W - 2 * T, t0, INK);
  };
  arrow(top, true);
  arrow(bottom - SB_W, false);
  const int ty = top + SB_W, th = bottom - SB_W - ty;
  if (pages > 1) {
    for (int y = ty; y < ty + th; y++)                          // gray (dithered) track
      for (int x = sx + T; x < SCREEN_W; x++)
        if (((x / T) + (y / T)) & 1) g.drawPixel(x, y, INK);
    const int kh = SB_W;                                        // scroll box
    const int ky = ty + (th - kh) * page / (pages - 1);
    g.fillRect(sx + T, ky, SB_W - T, kh, PAPER);
    g.drawRect(sx, ky, SB_W, kh, INK);
  }
}

static void drawLauncher() {
  const int n = viewCount();

  if (n == 0) screen.textCentered((SCREEN_W - SB_W) / 2, GRID_Y + CELL_H - CH, inFolder >= 0 ? "This folder is empty" : "No apps", T);

  const int first = page * PER_PAGE;
  for (int i = 0; i < countOnPage(page); i++) {
    const Tile& t = view()[first + i];
    const int cx = (i % COLS) * CELL_W;
    const int cy = GRID_Y + (i / COLS) * CELL_H;
    const int ix = cx + (CELL_W - ICON_BOX) / 2;
    const int iy = cy + (CELL_H - (ICON_BOX + 3 * T + CH)) / 2;   // icon + label centred in the cell
    const int ly = iy + ICON_BOX + 3 * T;

    drawTileIcon(t, ix, iy, ICON_BOX);
    const int lw = drawLabel(tileName(t), cx, CELL_W, ly);

    if (i == sel) {                                           // Finder selection: icon and name
      screen.invertRect(ix, iy, ICON_BOX, ICON_BOX);
      screen.invertRect(cx + (CELL_W - lw) / 2 - T, ly - T, lw + 2 * T, CH + 2 * T);
      if (ui == HomeUi::Move) dottedRect(cx + T, cy, CELL_W - 2 * T, CELL_H - T);
    }
  }
  drawScrollBar();
}

// ---- Menus and dialogs (Mac OS 7 style) ------------------------------------
static void boxWithShadow(int x, int y, int w, int h) {
  GFXcanvas1& g = screen.g();
  g.fillRect(x + 2 * T, y + 2 * T, w, h, INK);
  g.fillRect(x, y, w, h, PAPER);
  screen.red().fillRect(x, y, w, h, PAPER);
  g.drawRect(x, y, w, h, INK);
  if (T > 1) g.drawRect(x + 1, y + 1, w - 2, h - 2, INK);
}

static void drawMenu() {
  int wChars = 0;
  for (const auto& m : menu) wChars = std::max(wChars, (int)(strlen(m.label) + strlen(m.key) + 3));
  const int w = wChars * CW + 4 * T, rowH = LINE_H + T;
  const int h = (int)menu.size() * rowH + 4 * T;
  const int x = std::min(2 * MARGIN, SCREEN_W - w - 3 * T);
  const int y = BODY_Y + T;
  boxWithShadow(x, y, w, h);
  for (size_t i = 0; i < menu.size(); i++) {
    const int ry = y + 2 * T + i * rowH;
    screen.text(x + 3 * T, ry + T + T / 2, menu[i].label, T);
    screen.text(x + w - 3 * T - screen.textWidth(menu[i].key, T), ry + T + T / 2, menu[i].key, T);
    if ((int)i == menuSel) screen.invertRect(x + T, ry, w - 2 * T, rowH);
  }
}

static void drawPicker() {
  const String title = String("Move \"") + tileName(selTile()) + "\" to:";
  const int w = SCREEN_W - 16 * T, rowH = LINE_H + T;
  const int rows = std::min((int)picker.size(), (SCREEN_H - BODY_Y - 8 * T - 2 * LINE_H) / rowH);
  const int h = LINE_H + 4 * T + rows * rowH + LINE_H + 4 * T;
  const int x = 8 * T, y = BODY_Y + std::max(2 * T, (SCREEN_H - BODY_Y - h) / 2);
  boxWithShadow(x, y, w, h);
  screen.text(x + 4 * T, y + 3 * T, title.substring(0, (w - 8 * T) / CW).c_str(), T);
  screen.text(x + 4 * T + 1, y + 3 * T, title.substring(0, (w - 8 * T) / CW).c_str(), T);
  const int top = std::max(0, pickSel - rows + 1);
  for (int r = 0; r < rows; r++) {
    const int i = top + r;
    const int ry = y + LINE_H + 5 * T + r * rowH;
    const bool folder = picker[i].target >= 0;
    screen.text(x + 6 * T, ry + T, ((folder ? "  " : "") + picker[i].label).c_str(), T);
    if (folder) {                                               // small folder glyph
      const int fx = x + 6 * T, fy = ry + 2 * T, fw = CW + CW / 2, fh = CH - 2 * T;
      screen.g().drawRect(fx, fy + T, fw, fh - T, INK);
      screen.g().fillRect(fx, fy, fw / 2, T, INK);
    }
    if (i == pickSel) screen.invertRect(x + 3 * T, ry, w - 6 * T, rowH);
  }
  screen.text(x + 4 * T, y + h - LINE_H - T, "Enter=move  Esc=cancel", T);
}

static void drawNameDialog() {
  const char* title = nameMode == 1 ? "Rename folder" : "New folder";
  const int w = SCREEN_W - 16 * T;
  const int fieldH = CH + 6 * T;
  const int h = 4 * T + LINE_H + 3 * T + fieldH + 4 * T + LINE_H + 3 * T;
  const int x = 8 * T, y = BODY_Y + (SCREEN_H - BODY_Y - h) / 2;
  boxWithShadow(x, y, w, h);
  screen.text(x + 4 * T, y + 4 * T, title, T);
  screen.text(x + 4 * T + 1, y + 4 * T, title, T);
  const int fy = y + 4 * T + LINE_H + 3 * T;
  screen.g().drawRect(x + 4 * T, fy, w - 8 * T, fieldH, INK);
  const int maxc = (w - 12 * T) / CW - 1;
  String shown = (int)nameText.length() > maxc ? nameText.substring(nameText.length() - maxc) : nameText;
  screen.text(x + 7 * T, fy + 3 * T, shown.c_str(), T);
  screen.g().fillRect(x + 7 * T + shown.length() * CW, fy + 3 * T, T + 1, CH, INK);   // cursor
  screen.text(x + 4 * T, y + h - LINE_H - T, "Enter=OK  Esc=cancel", T);
}

// ---- Info popup ------------------------------------------------------------
static String humanBytes(uint64_t b) {
  char buf[16];
  if (b < 1024)                snprintf(buf, sizeof(buf), "%u B", (unsigned)b);
  else if (b < 1024 * 1024)    snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else                         snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
  return String(buf);
}

static void openInfo() {
  if (!viewCount()) return;
  const Tile& t = selTile();
  infoBytes = 0;
  infoFiles = 0;
  if (t.folder < 0 && apps[t.app].lua >= 0)
    infoBytes = Storage::dirSize(String(APPS_DIR) + "/" + luaApps[apps[t.app].lua].id, &infoFiles);
  ui = HomeUi::Info;
}

static void drawInfo() {
  const Tile& tile = selTile();
  const bool isFolder = tile.folder >= 0;
  const App* a = isFolder ? nullptr : &apps[tile.app];
  const LuaHost::AppInfo* li = (a && a->lua >= 0) ? &luaApps[a->lua] : nullptr;

  const int m = 2 * T, sh = 2 * T;
  const int x0 = m, y0 = BODY_Y + m, x1 = SCREEN_W - m - sh, y1 = SCREEN_H - m - sh;
  boxWithShadow(x0, y0, x1 - x0, y1 - y0);

  const int pad = 3 * T;
  auto clip = [&](const String& s, int n) { return (int)s.length() > n ? s.substring(0, std::max(1, n - 1)) + "~" : s; };

  const int box = ICON_BOX;
  drawTileIcon(tile, x0 + pad, y0 + pad, box);
  const int tx = x0 + pad + box + 2 * pad;
  const int cols = (x1 - pad - tx) / CW;
  int y = y0 + pad;
  screen.text(tx, y, clip(tileName(tile), (x1 - pad - tx) / (2 * CW)).c_str(), 2 * T);
  y += 2 * CH + 2 * T;
  String sub;
  if (isFolder)  sub = "Folder, " + String((int)folders[tile.folder].tiles.size()) + " app" + (folders[tile.folder].tiles.size() == 1 ? "" : "s");
  else if (li)   sub = li->version.length() ? "Version " + li->version : String("No version set");
  else           sub = "Built in, firmware " FW_VERSION;
  screen.text(tx, y, clip(sub, cols).c_str(), T);
  y += LINE_H;
  if (li && li->author.length()) { screen.text(tx, y, clip("By " + li->author, cols).c_str(), T); y += LINE_H; }

  y = std::max(y, y0 + pad + box) + 2 * T;
  const int fullCols = (x1 - x0 - 2 * pad) / CW;
  const int hintY = y1 - pad - CH;
  auto rowsLeft = [&]() { return (hintY + T - y) / LINE_H; };
  auto row = [&](const String& s) { screen.text(x0 + pad, y, clip(s, fullCols).c_str(), T); y += LINE_H; };

  std::vector<String> details;
  String about;
  if (isFolder) {
    String names;
    for (const Tile& c : folders[tile.folder].tiles) names += (names.length() ? ", " : "") + String(apps[c.app].name);
    about = names.length() ? names : String("Empty. Use Move to Folder to add apps.");
  } else {
    if (li) {
      details.push_back(String(APPS_DIR) + "/" + li->id + "  " + humanBytes(infoBytes) + ", " +
                        String(infoFiles) + (infoFiles == 1 ? " file" : " files"));
      if (li->tabLabel.length()) details.push_back("Web tab: " + li->tabLabel);
    }
    about = li ? li->description : String(a->about ? a->about : "");
  }
  if (about.length()) {
    std::vector<String> lines = wrapText(about, fullCols);
    const int room = std::max(1, rowsLeft() - (int)details.size());
    if ((int)lines.size() > room) { lines.resize(room); lines.back() = clip(lines.back() + "~", fullCols); }
    for (const auto& l : lines) row(l);
  }
  for (const auto& d : details) if (rowsLeft() > 0) row(d);
  screen.text(x0 + pad, hintY, "Enter = open   Esc = close", T);
}

// Rebuild the home screen (native apps + whatever is in /apps right now)
static void buildAppList() {
  luaApps = LuaHost::readManifests();
  apps.clear();
  appIcons.clear();
  for (const App& a : NATIVE_APPS) apps.push_back(a);
  for (size_t i = 0; i < luaApps.size(); i++)
    apps.push_back({ luaApps[i].name.c_str(), luaApps[i].icon.c_str(),
                     nullptr, nullptr, nullptr, nullptr, nullptr, (int)i, nullptr, nullptr });
  std::stable_sort(apps.begin(), apps.end(), [](const App& a, const App& b) {
    return strcasecmp(a.name, b.name) < 0;
  });
  NUM_APPS = apps.size();

  // Icons: built-in bitmaps, or icon.bmp from the app's folder
  appIcons.resize(apps.size());
  for (size_t i = 0; i < apps.size(); i++) {
    if (apps[i].iconImg) {
      Icons::fromPacked(apps[i].iconImg, ICON_SRC, ICON_SRC, ICON_BOX, appIcons[i]);
    } else if (apps[i].lua >= 0 && luaApps[apps[i].lua].image.length()) {
      const String p = String(APPS_DIR) + "/" + luaApps[apps[i].lua].id + "/" + luaApps[apps[i].lua].image;
      if (Storage::exists(p) && !Icons::loadBmp(p, ICON_BOX, appIcons[i]))
        Serial.printf("[app] %s: can't use %s (needs an uncompressed BMP)\n", apps[i].name, p.c_str());
    }
  }
  if (!folderIcon.valid()) Icons::fromPacked(ICON_FOLDER, ICON_SRC, ICON_SRC, ICON_BOX, folderIcon);

  loadLayout();
  clampSel();
  Serial.printf("[app] %u Lua apps found\n", (unsigned)luaApps.size());
}

static void openApp(int idx) {
  current = idx;
  customTitle = "";
  const App& a = apps[idx];
  Serial.printf("[app] open %s\n", a.name);
  if (a.lua >= 0) {
    LuaHost::open(luaApps[a.lua]);
    if (LuaHost::exitRequested()) { AppMgr::goHome(); return; }
  } else if (a.onEnter) {
    a.onEnter();
  }
  AppMgr::requestRedraw(Refresh::Full);      // app switch = natural moment for a clean refresh
}

static void openTile() {
  if (!viewCount()) return;
  const Tile t = selTile();
  if (t.folder >= 0) {
    inFolder = t.folder;
    inFolderName = folders[t.folder].name;
    page = sel = 0;
    AppMgr::requestRedraw(Refresh::Full);
  } else {
    openApp(t.app);
  }
}

static void closeFolder() {
  const String name = inFolderName;
  inFolder = -1;
  inFolderName = "";
  for (size_t i = 0; i < homeTiles.size(); i++)                // select the folder we came out of
    if (homeTiles[i].folder >= 0 && folders[homeTiles[i].folder].name == name) selectIndex(i);
  AppMgr::requestRedraw(Refresh::Full);
}

static void buildMenu() {
  menu.clear();
  if (viewCount()) {
    const bool isFolder = selTile().folder >= 0;
    menu.push_back({ "Open", "Enter", A_OPEN });
    menu.push_back({ "Get Info", "I", A_INFO });
    if (viewCount() > 1) menu.push_back({ "Move", "M", A_MOVE });
    if (!isFolder) menu.push_back({ "Move to Folder...", "F", A_TOFOLDER });
    if (isFolder) { menu.push_back({ "Rename Folder...", "R", A_RENAME }); menu.push_back({ "Delete Folder", "", A_DELFOLDER }); }
  }
  if (inFolder < 0) menu.push_back({ "New Folder...", "N", A_NEWFOLDER });
  if (viewCount() > 1) menu.push_back({ "Sort A-Z", "", A_SORT });
  if (inFolder >= 0) menu.push_back({ "Close Folder", "Esc", A_CLOSE });
  menuSel = 0;
}

static String uniqueFolderName(String name, int except) {
  name.trim();
  if (!name.length()) name = "untitled folder";
  String base = name, candidate = name;
  for (int n = 2; ; n++) {
    bool clash = false;
    for (size_t f = 0; f < folders.size(); f++) if ((int)f != except && folders[f].name == candidate) clash = true;
    if (!clash) return candidate;
    candidate = base + " " + String(n);
  }
}

static void openPicker() {
  if (!viewCount() || selTile().folder >= 0) return;
  picker.clear();
  if (inFolder >= 0) picker.push_back({ "Home", -1 });
  for (size_t f = 0; f < folders.size(); f++) if ((int)f != inFolder) picker.push_back({ folders[f].name, (int)f });
  picker.push_back({ "New Folder...", -2 });
  pickSel = 0;
  ui = HomeUi::Picker;
}

// Take the selected app out of the current view
static Tile removeSelected() {
  const Tile t = selTile();
  view().erase(view().begin() + selIndex());
  clampSel();
  return t;
}

static void applyPicker() {
  const int target = picker[pickSel].target;
  if (target == -2) { nameMode = 2; nameText = "untitled folder"; ui = HomeUi::Name; return; }
  const Tile t = removeSelected();
  if (target == -1) homeTiles.push_back(t);
  else folders[target].tiles.push_back(t);
  saveLayout();
  ui = HomeUi::Grid;
}

static void applyName() {
  if (nameMode == 1) {
    Folder& f = folders[selTile().folder];
    f.name = uniqueFolderName(nameText, selTile().folder);
  } else {
    folders.push_back({ uniqueFolderName(nameText, -1), {} });
    const int fi = folders.size() - 1;
    if (nameMode == 2) {                                       // new folder for the selected app
      const int at = inFolder < 0 ? selIndex() : (int)homeTiles.size();
      const Tile t = removeSelected();
      folders[fi].tiles.push_back(t);
      homeTiles.insert(homeTiles.begin() + std::min(at, (int)homeTiles.size()), { -1, fi });
      if (inFolder < 0) selectIndex(at);
    } else {                                                   // empty folder next to the selection
      const int at = viewCount() ? selIndex() + 1 : 0;
      homeTiles.insert(homeTiles.begin() + at, { -1, fi });
      selectIndex(at);
    }
  }
  saveLayout();
  ui = HomeUi::Grid;
}

static void deleteFolder() {
  const int fi = selTile().folder;
  const int at = selIndex();
  std::vector<Tile> kids = folders[fi].tiles;
  homeTiles.erase(homeTiles.begin() + at);
  homeTiles.insert(homeTiles.begin() + at, kids.begin(), kids.end());   // its apps take its place
  folders.erase(folders.begin() + fi);
  for (Tile& t : homeTiles) if (t.folder > fi) t.folder--;
  clampSel();
  saveLayout();
}

static void runAction(int action) {
  ui = HomeUi::Grid;
  switch (action) {
    case A_OPEN:      openTile(); return;
    case A_INFO:      openInfo(); break;
    case A_MOVE:      moveBackup = view(); movePage = page; moveSel = sel; ui = HomeUi::Move; break;
    case A_TOFOLDER:  openPicker(); break;
    case A_NEWFOLDER: nameMode = 0; nameText = "untitled folder"; ui = HomeUi::Name; break;
    case A_RENAME:    nameMode = 1; nameText = folders[selTile().folder].name; ui = HomeUi::Name; break;
    case A_DELFOLDER: deleteFolder(); break;
    case A_SORT:
      std::stable_sort(view().begin(), view().end(), [](const Tile& a, const Tile& b) { return strcasecmp(tileName(a), tileName(b)) < 0; });
      selectIndex(0);
      saveLayout();
      break;
    case A_CLOSE:     closeFolder(); return;
  }
  AppMgr::requestRedraw(Refresh::Partial);
}

static void launcherKey(uint8_t k) {
  const char* fn = fnKeyName(k);

  switch (ui) {
    case HomeUi::Info:
      ui = HomeUi::Grid;
      if (k == K_ENTER) { openTile(); return; }
      AppMgr::requestRedraw(Refresh::Partial);
      return;

    case HomeUi::Menu:
      if (k == K_UP)        menuSel = (menuSel + menu.size() - 1) % menu.size();
      else if (k == K_DOWN) menuSel = (menuSel + 1) % menu.size();
      else if (k == K_ENTER || k == K_RIGHT) { runAction(menu[menuSel].action); return; }
      else if (k == K_ESC || k == K_TAB || k == K_LEFT) ui = HomeUi::Grid;
      else return;
      AppMgr::requestRedraw(Refresh::Partial);
      return;

    case HomeUi::Picker:
      if (k == K_UP)        pickSel = (pickSel + picker.size() - 1) % picker.size();
      else if (k == K_DOWN) pickSel = (pickSel + 1) % picker.size();
      else if (k == K_ENTER || k == K_RIGHT) applyPicker();
      else if (k == K_ESC || k == K_LEFT) ui = HomeUi::Grid;
      else return;
      AppMgr::requestRedraw(Refresh::Partial);
      return;

    case HomeUi::Name:
      if (k == K_ENTER) applyName();
      else if (k == K_ESC) ui = HomeUi::Grid;
      else if (k == K_BKSP) { if (nameText.length()) nameText.remove(nameText.length() - 1); }
      else if (isPrintableKey(k) && k != '/' && nameText.length() < 20) nameText += (char)k;
      else return;
      AppMgr::requestRedraw(Refresh::Partial);
      return;

    case HomeUi::Move: {
      const int n = viewCount(), from = selIndex();
      int to = from;
      if (k == K_LEFT) to = from - 1;
      else if (k == K_RIGHT) to = from + 1;
      else if (k == K_UP) to = from - COLS;
      else if (k == K_DOWN) to = from + COLS;
      else if (k == K_ENTER || k == 'm' || k == 'M') { ui = HomeUi::Grid; saveLayout(); AppMgr::requestRedraw(Refresh::Partial); return; }
      else if (k == K_ESC) { view() = moveBackup; page = movePage; sel = moveSel; ui = HomeUi::Grid; AppMgr::requestRedraw(Refresh::Partial); return; }
      else return;
      to = std::max(0, std::min(n - 1, to));
      if (to == from) return;
      const Tile t = view()[from];                             // slide it into the new spot
      view().erase(view().begin() + from);
      view().insert(view().begin() + to, t);
      selectIndex(to);
      AppMgr::requestRedraw(Refresh::Partial);
      return;
    }

    case HomeUi::Grid:
      break;
  }

  const int n = countOnPage(page);
  const int pc = pageCount();
  const int row = sel / COLS, col = sel % COLS;
  const int lastRow = rowsOnPage(page) - 1;

  switch (k) {
    case K_RIGHT: if (sel < n - 1) sel++; else return; break;
    case K_LEFT:  if (sel > 0) sel--; else return; break;
    case K_DOWN:
      if (row < lastRow) sel = std::min(sel + COLS, n - 1);
      else if (pc > 1) { page = (page + 1) % pc; sel = std::min(col, countOnPage(page) - 1); }
      else return;
      break;
    case K_UP:
      if (row > 0) sel -= COLS;
      else if (pc > 1) { page = (page - 1 + pc) % pc; sel = std::min((rowsOnPage(page) - 1) * COLS + col, countOnPage(page) - 1); }
      else return;
      break;
    case K_ENTER: openTile(); return;
    case K_ESC:   if (inFolder >= 0) closeFolder(); return;
    case K_TAB:   buildMenu(); ui = HomeUi::Menu; break;
    case 'i': case 'I': case K_FN_ENTER: openInfo(); break;
    case 'm': case 'M': if (viewCount() > 1) runAction(A_MOVE); return;
    case 'f': case 'F': openPicker(); break;
    case 'n': case 'N': if (inFolder < 0) runAction(A_NEWFOLDER); return;
    case 'r': case 'R': if (viewCount() && selTile().folder >= 0) runAction(A_RENAME); return;
    default: return;
  }
  (void)fn;
  AppMgr::requestRedraw(Refresh::Partial);
}

// =====================================================================
//  AppMgr
// =====================================================================
namespace AppMgr {

void begin() {
  current = -1;
  inFolder = -1;
  buildAppList();
  requestRedraw(Refresh::Clean);             // first picture on the panel
}

void requestRedraw(Refresh mode) {
  if (!dirty || mode > pending) pending = mode;
  dirty = true;
}

void setTitle(const String& title) { customTitle = title; }
int  luaAppCount() { return luaApps.size(); }

void goHome() {
  if (current >= 0 && apps[current].lua >= 0) LuaHost::close();
  ui = HomeUi::Grid;                         // (stays in the folder the app was opened from)
  current = -1;
  customTitle = "";
  buildAppList();                            // pick up apps installed/removed meanwhile
  requestRedraw(Refresh::Full);
}

void handleKey(uint8_t k) {
  lastKeyMs = millis();
  if (current < 0) { launcherKey(k); return; }

  const App& a = apps[current];
  if (a.lua >= 0) {
    if (k == K_ESC) { if (!LuaHost::back()) { goHome(); return; } }
    else            LuaHost::key(k);
    if (LuaHost::exitRequested()) goHome();
    return;
  }

  if (k == K_ESC) {
    if (a.onBack && a.onBack()) return;      // handled inside the app
    goHome();
    return;
  }
  a.onKey(k);
}

void tick() {
  if (current >= 0) {
    const App& a = apps[current];
    if (a.lua >= 0) {
      LuaHost::tick();
      if (LuaHost::exitRequested()) goHome();
    } else if (a.onTick) {
      a.onTick();
    }
  }

  Clock::loop();
#if STATUS_CLOCK_REDRAW
  static long lastMin = -2;                       // redraw when the clock's minute changes
  const long m = Clock::valid() ? (long)(time(nullptr) / 60) : -1;
  if (m != lastMin) { lastMin = m; requestRedraw(Refresh::Partial); }
#endif
  static bool lastKb = false;
  if (kb.present() != lastKb) { lastKb = kb.present(); requestRedraw(Refresh::Partial); }

  if (!dirty) return;
  if (millis() - lastKeyMs < REFRESH_SETTLE_MS) return;   // slow panels: wait for a pause
  dirty = false;

  screen.clear();
  String moving;
  if (current < 0 && ui == HomeUi::Move && viewCount()) moving = String("Moving ") + tileName(selTile());
  const char* title = current >= 0 ? (customTitle.length() ? customTitle.c_str() : apps[current].name)
                    : moving.length() ? moving.c_str()
                    : (inFolder >= 0 ? folders[inFolder].name.c_str() : BRAND_NAME);
  drawStatusBar(title, current >= 0 || inFolder >= 0);
  if (current < 0) {
    drawLauncher();
    if (ui == HomeUi::Info)   drawInfo();
    if (ui == HomeUi::Menu)   drawMenu();
    if (ui == HomeUi::Picker) drawPicker();
    if (ui == HomeUi::Name)   drawNameDialog();
  }
  else if (apps[current].lua >= 0) LuaHost::draw();
  else                             apps[current].onDraw();
  screen.refresh(pending);
}

} // namespace AppMgr

// =====================================================================
//  Mac OS 7 style alert: caution icon, message, Cancel / OK buttons.
//  The selected button gets the thick "default" ring.
// =====================================================================
static void drawDialog(const String& title, const std::vector<String>& lines,
                       const char* cancel, const char* ok, bool okSelected) {
  GFXcanvas1& g = screen.g();
  const int x0 = 6 * T, x1 = SCREEN_W - 6 * T;
  const int pad = 5 * T;
  const int iconW = 16 * T;
  const int textX = x0 + pad + iconW + 4 * T;
  const int cols = (x1 - pad - textX) / CW;

  std::vector<String> body;
  for (const auto& l : lines) for (const auto& w : wrapText(l, cols)) body.push_back(w);
  std::vector<String> head = wrapText(title, cols);

  const int btnH = CH + 6 * T;
  const int h = pad + (int)(head.size() + body.size()) * LINE_H + 3 * T + btnH + 2 * pad;
  const int y0 = std::max(BODY_Y + 2 * T, BODY_Y + (SCREEN_H - BODY_Y - h) / 2);
  const int y1 = std::min(SCREEN_H - 2 * T, y0 + h);

  // Frame: shadow, 1px outer line, gap, thick inner border
  g.fillRect(x0 + 2 * T, y0 + 2 * T, x1 - x0, y1 - y0, INK);
  g.fillRect(x0, y0, x1 - x0, y1 - y0, PAPER);
  screen.red().fillRect(x0, y0, x1 - x0, y1 - y0, PAPER);
  g.drawRect(x0, y0, x1 - x0, y1 - y0, INK);
  for (int b = 0; b < 2 * T; b++) g.drawRect(x0 + 2 * T + b, y0 + 2 * T + b, x1 - x0 - 4 * T - 2 * b, y1 - y0 - 4 * T - 2 * b, INK);

  // Caution icon: a triangle with "!"
  const int ix = x0 + pad, iy = y0 + pad + T;
  for (int b = 0; b < T; b++)
    g.drawTriangle(ix + iconW / 2, iy + b, ix + b, iy + iconW - b, ix + iconW - b, iy + iconW - b, INK);
  g.fillRect(ix + iconW / 2 - T / 2 - (T > 1 ? 0 : 0), iy + iconW / 3, T + (T > 1 ? 0 : 1), iconW / 3, INK);
  g.fillRect(ix + iconW / 2 - T / 2, iy + iconW - 4 * T, T + (T > 1 ? 0 : 1), T + 1, INK);

  // Message
  int y = y0 + pad;
  for (const auto& l : head) { screen.text(textX, y, l.c_str(), T); screen.text(textX + 1, y, l.c_str(), T); y += LINE_H; }
  for (const auto& l : body) { screen.text(textX, y, l.c_str(), T); y += LINE_H; }

  // Buttons, right-aligned: [Cancel] [OK]
  const int bw = std::max(screen.textWidth(cancel, T), screen.textWidth(ok, T)) + 10 * T;
  const int by = y1 - pad - btnH;
  const int okX = x1 - pad - 2 * T - bw;
  const int cancelX = okX - 6 * T - bw;
  const int r = 4 * T;
  auto button = [&](int bx, const char* label, bool selected) {
    for (int b = 0; b < T; b++) g.drawRoundRect(bx + b, by + b, bw - 2 * b, btnH - 2 * b, r, INK);
    screen.textCentered(bx + bw / 2, by + 3 * T, label, T);
    if (selected) for (int b = 0; b < 2 * T; b++)
      g.drawRoundRect(bx - 3 * T + b, by - 3 * T + b, bw + 6 * T - 2 * b, btnH + 6 * T - 2 * b, r + 3 * T, INK);
  };
  button(cancelX, cancel, !okSelected);
  button(okX, ok, okSelected);
}

// =====================================================================
//  Files — browse the card, read text files, delete files and folders.
//  Right = open, Left = back, Bksp = delete (with a confirmation).
// =====================================================================
static String fPath = "/";
static std::vector<Storage::Entry> fEnts;
static int fSel = 0, fTop = 0;
static bool fViewing = false;
static std::vector<String> fLines;
static int fScroll = 0;
static String fMsg;

// Delete confirmation
static bool fConfirm = false, fDelYes = false;
static uint32_t fDelFiles = 0, fDelDirs = 0;
static uint64_t fDelBytes = 0;

static const int FILE_ROWS = (SCREEN_H - BODY_Y - 2 * T) / LINE_H - 1;   // leave the hint line
static const int VIEW_ROWS = (SCREEN_H - BODY_Y - 4 * T) / LINE_H;

static String joinPath(const String& dir, const String& name) {
  return dir == "/" ? String("/") + name : dir + "/" + name;
}

static void filesLoad(const String& select = "") {
  fEnts = Storage::list(fPath);
  fSel = 0; fTop = 0;
  for (size_t i = 0; i < fEnts.size(); i++) if (fEnts[i].name == select) fSel = i;
  AppMgr::setTitle(fPath);
}

static void filesEnter() {
  if (!Storage::mounted()) Storage::begin();
  fPath = "/";
  fViewing = fConfirm = false;
  fMsg = "";
  filesLoad();
}

static void filesOpen() {
  if (fEnts.empty()) return;
  const Storage::Entry& e = fEnts[fSel];
  const String p = joinPath(fPath, e.name);
  if (e.dir) {
    fPath = p;
    filesLoad();
  } else {
    String content;
    Storage::readText(p, content, 16000);
    fLines = wrapText(content, TEXT_COLS);
    fScroll = 0;
    fViewing = true;
    AppMgr::setTitle(e.name);
  }
  AppMgr::requestRedraw(Refresh::Full);
}

static bool filesUp() {                          // one level up; false at the top
  if (fPath == "/") return false;
  const String from = fPath.substring(fPath.lastIndexOf('/') + 1);
  const int slash = fPath.lastIndexOf('/');
  fPath = slash <= 0 ? String("/") : fPath.substring(0, slash);
  filesLoad(from);                               // keep the folder we came from selected
  AppMgr::requestRedraw(Refresh::Full);
  return true;
}

static void filesAskDelete() {
  if (fEnts.empty()) return;
  const Storage::Entry& e = fEnts[fSel];
  fDelFiles = fDelDirs = 0;
  fDelBytes = e.dir ? Storage::dirSize(joinPath(fPath, e.name), &fDelFiles, &fDelDirs) : e.size;
  fDelYes = false;                               // Cancel is the default
  fConfirm = true;
}

static void filesDoDelete() {
  const Storage::Entry e = fEnts[fSel];
  const bool ok = Storage::removeTree(joinPath(fPath, e.name));
  fMsg = ok ? "Deleted " + e.name : "Couldn't delete " + e.name;
  // A deleted system folder comes straight back, empty: the firmware relies on it
  if (fPath == "/") { Storage::mkdirs(NOTES_DIR); Storage::mkdirs(APPS_DIR); Storage::mkdirs(SYSTEM_DIR); }
  const int keep = fSel;
  filesLoad();
  fSel = std::min(keep, (int)fEnts.size() - 1);
  if (fSel < 0) fSel = 0;
}

static void filesKey(uint8_t k) {
  const char* fn = fnKeyName(k);

  if (fConfirm) {
    switch (k) {
      case K_LEFT: case K_RIGHT: case K_UP: case K_DOWN: case K_TAB: fDelYes = !fDelYes; break;
      case 'y': case 'Y': fDelYes = true; break;
      case 'n': case 'N': fDelYes = false; break;
      case K_ENTER:
        fConfirm = false;
        if (fDelYes) filesDoDelete();
        AppMgr::requestRedraw(Refresh::Full);
        return;
      default: return;
    }
    AppMgr::requestRedraw(Refresh::Partial);
    return;
  }

  if (fViewing) {
    const int maxScroll = (int)fLines.size() > VIEW_ROWS ? (int)fLines.size() - VIEW_ROWS : 0;
    if (k == K_LEFT) { fViewing = false; AppMgr::setTitle(fPath); AppMgr::requestRedraw(Refresh::Full); return; }
    if (k == K_UP) fScroll--;
    else if (k == K_DOWN) fScroll++;
    else if (k == K_RIGHT || k == ' ' || (fn && !strcmp(fn, "down"))) fScroll += VIEW_ROWS - 1;
    else if (fn && !strcmp(fn, "up")) fScroll -= VIEW_ROWS - 1;
    else return;
    if (fScroll < 0) fScroll = 0;
    if (fScroll > maxScroll) fScroll = maxScroll;
    AppMgr::requestRedraw(Refresh::Partial);
    return;
  }

  fMsg = "";
  const int count = (int)fEnts.size();
  if (k == K_UP)        { if (fSel > 0) fSel--; }
  else if (k == K_DOWN) { if (fSel < count - 1) fSel++; }
  else if (k == K_RIGHT || k == K_ENTER) { filesOpen(); return; }
  else if (k == K_LEFT) { if (!filesUp()) AppMgr::goHome(); return; }   // Left at the top leaves Files
  else if (k == K_BKSP || k == K_DEL || (fn && !strcmp(fn, "del"))) { if (count) filesAskDelete(); else return; }
  else return;
  AppMgr::requestRedraw(Refresh::Partial);
}

static bool filesBack() {
  if (fConfirm) { fConfirm = false; AppMgr::requestRedraw(Refresh::Full); return true; }   // Esc = Cancel
  if (fViewing) { fViewing = false; AppMgr::setTitle(fPath); AppMgr::requestRedraw(Refresh::Full); return true; }
  return filesUp();                                                                            // false at the top: go home
}

static String countPhrase(uint32_t n, const char* one, const char* many) {
  return String(n) + " " + (n == 1 ? one : many);
}

static void filesDrawList() {
  if (fEnts.empty()) {
    screen.text(MARGIN, BODY_Y + 4 * T, "(empty folder)", T);
  } else {
    std::vector<String> items;
    const int nameCols = TEXT_COLS - 8;
    for (const auto& e : fEnts) {
      String n = e.dir ? e.name + "/" : e.name;
      if ((int)n.length() > nameCols) n = n.substring(0, nameCols - 2) + "..";
      String right = e.dir ? String(">") : humanSize(e.size);
      while ((int)(n.length() + right.length()) < TEXT_COLS) n += ' ';
      items.push_back(n + right);
    }
    drawList(items, fSel, fTop, BODY_Y + 2 * T, FILE_ROWS - (fMsg.length() ? 1 : 0));
  }
  if (fMsg.length()) screen.text(MARGIN, SCREEN_H - 2 * LINE_H, fMsg.substring(0, TEXT_COLS).c_str(), T);
  screen.text(MARGIN, SCREEN_H - LINE_H, "Right=open Left=back Bksp=del", T);
}

static void filesDraw() {
  if (!Storage::mounted()) {
    screen.textCentered(SCREEN_W / 2, BODY_Y + 20 * T, "No SD card", 2 * T);
    screen.textCentered(SCREEN_W / 2, BODY_Y + 45 * T, "Insert FAT32 card, reopen Files", T);
    return;
  }

  if (fViewing) {
    const int y0 = BODY_Y + 3 * T;
    for (int r = 0; r < VIEW_ROWS && fScroll + r < (int)fLines.size(); r++)
      screen.text(MARGIN, y0 + r * LINE_H, fLines[fScroll + r].c_str(), T);
    if ((int)fLines.size() > VIEW_ROWS) {                  // scroll position bar
      const int barH = SCREEN_H - BODY_Y;
      const int knob = barH * VIEW_ROWS / fLines.size();
      const int pos  = (barH - knob) * fScroll / ((int)fLines.size() - VIEW_ROWS);
      screen.g().fillRect(SCREEN_W - 2 * T, BODY_Y + pos, 2 * T, knob < 4 ? 4 : knob, INK);
    }
    return;
  }

  filesDrawList();

  if (fConfirm) {
    const Storage::Entry& e = fEnts[fSel];
    const String path = joinPath(fPath, e.name);
    std::vector<String> lines;
    if (e.dir) {
      if (fDelFiles == 0 && fDelDirs == 0) {
        lines.push_back("The folder is empty.");
      } else {
        String what = countPhrase(fDelFiles, "file", "files");
        if (fDelDirs) what += " in " + countPhrase(fDelDirs, "folder", "folders");
        lines.push_back("It contains " + what + " (" + humanSize(fDelBytes) + ").");
        lines.push_back("They will all be deleted.");
      }
      if (path == APPS_DIR)   lines.push_back("This is every installed app and its data.");
      if (path == NOTES_DIR)  lines.push_back("These are all your notes.");
      if (path == SYSTEM_DIR) lines.push_back("This holds your WiFi and clock settings.");
    } else {
      lines.push_back(humanSize(e.size) + ". This can't be undone.");
    }
    drawDialog(String("Delete ") + (e.dir ? "folder " : "") + "\"" + e.name + "\"?", lines, "Cancel", "Delete", fDelYes);
  }
}

// =====================================================================
//  Key Test — shows raw codes (clones sometimes differ)
// =====================================================================
static uint8_t keyHist[8];
static int keyHistN = 0;

static const char* keyName(uint8_t k) {
  static char buf[16];
  switch (k) {
    case K_LEFT:  return "LEFT";
    case K_RIGHT: return "RIGHT";
    case K_UP:    return "UP";
    case K_DOWN:  return "DOWN";
    case K_ENTER: return "ENTER";
    case K_BKSP:  return "BKSP";
    case K_TAB:   return "TAB";
    case ' ':     return "SPACE";
  }
  if (isPrintableKey(k)) { snprintf(buf, sizeof(buf), "'%c'", k); return buf; }
  if (k == K_DEL) return "DEL";
  if (const char* fn = fnKeyName(k)) { snprintf(buf, sizeof(buf), "Fn+%s", fn); return buf; }
  return "(other)";
}

static void keyTestEnter() { keyHistN = 0; }

static void keyTestKey(uint8_t k) {
  for (int i = 7; i > 0; i--) keyHist[i] = keyHist[i - 1];
  keyHist[0] = k;
  if (keyHistN < 8) keyHistN++;
  AppMgr::requestRedraw(Refresh::Partial);
}

static void keyTestDraw() {
  const int y0 = BODY_Y + 3 * T;
  screen.text(MARGIN, y0, "Press keys. Esc = back.", T);
  if (keyHistN == 0) { screen.text(MARGIN, y0 + 15 * T, "(nothing yet)", T); return; }
  char line[32];
  for (int i = 0; i < keyHistN; i++) {
    snprintf(line, sizeof(line), "0x%02X %s", keyHist[i], keyName(keyHist[i]));
    screen.text(MARGIN + (i / 4) * (SCREEN_W / 2), y0 + 15 * T + (i % 4) * 12 * T, line, T);
  }
}

// =====================================================================
//  System — About (device info) and Settings (Clock, screen refresh).
//  Up/Down move, Right/Enter open or change, Left/Esc go back.
// =====================================================================
enum class SysScreen { Menu, About, Settings, Clock, Zones };
static SysScreen sysScr = SysScreen::Menu;
static int sysSel = 0, sysTop = 0;
static String sysMsg;

static const char* SYS_MENU[]     = { "About this device", "Settings" };
// Settings rows; the list shown depends on the panel and firmware (see settingsRows)
enum SetRow { SET_CLOCK, SET_SLEEP, SET_EVERY, SET_SWITCH, SET_STYLE, SET_CLEAN, SET_ROLLBACK };
static const char* SYS_SETTINGS[] = { "Clock", "Sleep after", "Full refresh after", "Full on app switch",
                                      "Full refresh type", "Deep clean screen", "Previous firmware" };
static std::vector<int> settingsRows() {
  std::vector<int> r = { SET_CLOCK, SET_SLEEP, SET_EVERY, SET_SWITCH };
  if (screen.hasDeepClean()) r.push_back(SET_STYLE);
  r.push_back(SET_CLEAN);
  if (Ota::canRollBack()) r.push_back(SET_ROLLBACK);
  return r;
}
static bool sysRollAsk = false, sysRollYes = false;
static const uint32_t SLEEP_CHOICES[] = { 60, 120, 300, 600, 0 };      // seconds; 0 = never
// Partial refreshes before a full one (Screen::REFRESH_NEVER = never)
static const uint16_t FULL_EVERY_CHOICES[]  = { 25, 50, 100, 200, 500, Screen::REFRESH_NEVER };
static const uint16_t FULL_SWITCH_CHOICES[] = { 0, 10, 30, 100, Screen::REFRESH_NEVER };

template <size_t N>
static uint16_t nextChoice(const uint16_t (&choices)[N], uint16_t cur) {
  size_t i = 0;
  while (i < N && choices[i] != cur) i++;
  return choices[i < N ? (i + 1) % N : 0];             // unknown value (edited file): start over
}

static String fullEveryLabel(uint16_t n) {
  return n == Screen::REFRESH_NEVER ? String("Never") : String(n) + " updates";
}
static String fullSwitchLabel(uint16_t n) {
  if (n == Screen::REFRESH_NEVER) return "Never";
  if (n == 0) return "Always";
  return "After " + String(n);
}

static String sleepLabel(uint32_t s) {
  if (!s) return "Never";
  return String((unsigned long)(s / 60)) + (s == 60 ? " minute" : " minutes");
}
static const int CLOCK_ITEMS = 3;                // format, time zone, sync now

static void sysGo(SysScreen to, int select = 0) {
  sysScr = to;
  sysSel = select;
  sysTop = 0;
  sysMsg = "";
  static const char* titles[] = { "System", "About", "Settings", "Clock", "Time zone" };
  AppMgr::setTitle(titles[(int)to]);
  AppMgr::requestRedraw(Refresh::Full);
}

static void sysEnter() { sysGo(SysScreen::Menu); }

static int sysCount() {
  switch (sysScr) {
    case SysScreen::Menu:     return 2;
    case SysScreen::Settings: return settingsRows().size();
    case SysScreen::Clock:    return CLOCK_ITEMS;
    case SysScreen::Zones:    return Clock::zoneCount();
    default:                  return 0;
  }
}

static bool sysBack() {
  if (sysRollAsk) { sysRollAsk = false; AppMgr::requestRedraw(Refresh::Partial); return true; }
  switch (sysScr) {
    case SysScreen::Menu:     return false;                    // leave the app
    case SysScreen::About:    sysGo(SysScreen::Menu, 0); break;
    case SysScreen::Settings: sysGo(SysScreen::Menu, 1); break;
    case SysScreen::Clock:    sysGo(SysScreen::Settings, 0); break;
    case SysScreen::Zones:    sysGo(SysScreen::Clock, 1); break;
  }
  return true;
}

static void sysActivate() {
  switch (sysScr) {
    case SysScreen::Menu:
      sysGo(sysSel == 0 ? SysScreen::About : SysScreen::Settings);
      break;
    case SysScreen::Settings: {
      const std::vector<int> rows = settingsRows();
      if (sysSel < 0 || sysSel >= (int)rows.size()) break;
      switch (rows[sysSel]) {
        case SET_CLOCK: sysGo(SysScreen::Clock); return;
        case SET_SLEEP: {                                      // cycle 1, 2, 5, 10 minutes, never
          const int n = sizeof(SLEEP_CHOICES) / sizeof(SLEEP_CHOICES[0]);
          int i = 0;
          while (i < n && SLEEP_CHOICES[i] != Power::timeoutSec()) i++;
          Power::setTimeoutSec(SLEEP_CHOICES[(i + 1) % n]);
          break;
        }
        case SET_EVERY:  screen.setFullEvery(nextChoice(FULL_EVERY_CHOICES, screen.fullEvery())); break;
        case SET_SWITCH: screen.setFullOnSwitch(nextChoice(FULL_SWITCH_CHOICES, screen.fullOnSwitch())); break;
        case SET_STYLE:  screen.setDeepFull(!screen.deepFull()); break;
        case SET_CLEAN:  sysMsg = "Screen cleaned"; AppMgr::requestRedraw(Refresh::Clean); return;
        case SET_ROLLBACK: sysRollAsk = true; sysRollYes = false; break;
      }
      AppMgr::requestRedraw(Refresh::Partial);
      break;
    }
    case SysScreen::Clock:
      if (sysSel == 0)      { Clock::set12h(!Clock::use12h()); AppMgr::requestRedraw(Refresh::Partial); }
      else if (sysSel == 1) { sysGo(SysScreen::Zones, std::max(0, Clock::currentZone())); }
      else                  { Clock::syncNow(); AppMgr::requestRedraw(Refresh::Partial); }
      break;
    case SysScreen::Zones:
      Clock::setTz(Clock::zonePosix(sysSel));
      sysGo(SysScreen::Clock, 1);
      break;
    default: break;
  }
}

static void sysKey(uint8_t k) {
  if (sysRollAsk) {                                            // "go back to the previous firmware?"
    if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN || k == K_TAB) sysRollYes = !sysRollYes;
    else if (k == K_ENTER) {
      sysRollAsk = false;
      if (sysRollYes) {
        if (Ota::rollBack()) { sysMsg = "Restarting..."; AppMgr::requestRedraw(Refresh::Full); AppMgr::tick(); delay(1000); ESP.restart(); }
        sysMsg = "Couldn't switch back";
      }
    } else if (k == K_ESC) sysRollAsk = false;
    else return;
    AppMgr::requestRedraw(Refresh::Partial);
    return;
  }
  if (k == K_LEFT) { if (!sysBack()) AppMgr::goHome(); return; }
  const int n = sysCount();
  if (k == K_UP && n)        sysSel = (sysSel + n - 1) % n;
  else if (k == K_DOWN && n) sysSel = (sysSel + 1) % n;
  else if (k == K_RIGHT || k == K_ENTER) { sysActivate(); return; }
  else if ((k == 't' || k == 'T') && sysScr == SysScreen::Clock) { Clock::syncNow(); }
  else return;
  AppMgr::requestRedraw(Refresh::Partial);
}

static void sysTick() {
  if (sysScr != SysScreen::Clock && sysScr != SysScreen::About) return;
  static String last;                            // keep the time and sync status current
  const String now = Clock::syncStatus() + Clock::hhmm();
  if (now != last) { last = now; AppMgr::requestRedraw(Refresh::Partial); }
}

// A menu row: label on the left, value or ">" on the right
static String menuRow(const String& label, const String& right) {
  String l = label;
  const int room = TEXT_COLS - 1 - (int)right.length();
  if ((int)l.length() > room) l = l.substring(0, room - 1) + "~";
  while ((int)(l.length() + right.length()) < TEXT_COLS) l += ' ';
  return l + right;
}

static void sysDrawAbout() {
  char l[64];
  int y = BODY_Y + 3 * T;
  auto row = [&](const char* s) {
    String t = s;
    if ((int)t.length() > TEXT_COLS) t = t.substring(0, TEXT_COLS - 1) + "~";
    screen.text(MARGIN, y, t.c_str(), T);
    y += 10 * T;
  };
  snprintf(l, sizeof(l), "Firmware %s (%s), %s", FW_VERSION, Ota::runningSlot(), ESP.getChipModel()); row(l);
  snprintf(l, sizeof(l), "Heap free: %lu KB  PSRAM: %lu KB",
           (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getPsramSize() / 1024));   row(l);
  snprintf(l, sizeof(l), "Display: %s %dx%d", screen.backendName(), SCREEN_W, SCREEN_H);         row(l);
  snprintf(l, sizeof(l), "Keyboard: %s", kb.present() ? "CardKB @0x5F" : "not found");              row(l);
  snprintf(l, sizeof(l), "SD: %s", Storage::cardInfo().c_str());                                   row(l);
  snprintf(l, sizeof(l), "Lua apps: %d installed", AppMgr::luaAppCount());                           row(l);
  snprintf(l, sizeof(l), "Refresh: %lu partial / %lu full",
           (unsigned long)screen.partialCount(), (unsigned long)screen.fullCount());               row(l);
  snprintf(l, sizeof(l), "Time: %s", Clock::dateLine().c_str());                                   row(l);
  snprintf(l, sizeof(l), "Clock: %s", Clock::rtcName() ? Clock::rtcName() : "no RTC module");       row(l);
  screen.text(MARGIN, SCREEN_H - 10 * T, "Left=back", T);
}

static void sysDrawScreen() {
  std::vector<String> items;
  int y0 = BODY_Y + 2 * T;
  switch (sysScr) {
    case SysScreen::About:
      sysDrawAbout();
      return;
    case SysScreen::Menu:
      items.push_back(menuRow(SYS_MENU[0], ">"));
      items.push_back(menuRow(SYS_MENU[1], ">"));
      break;
    case SysScreen::Settings:
      for (int id : settingsRows()) {
        String right;
        switch (id) {
          case SET_CLOCK:  right = ">"; break;
          case SET_SLEEP:  right = sleepLabel(Power::timeoutSec()); break;
          case SET_EVERY:  right = fullEveryLabel(screen.fullEvery()); break;
          case SET_SWITCH: right = fullSwitchLabel(screen.fullOnSwitch()); break;
          case SET_STYLE:  right = screen.deepFull() ? "Deep (3 s)" : "Fast (1 s)"; break;
        }
        items.push_back(menuRow(SYS_SETTINGS[id], right));
      }
      break;
    case SysScreen::Clock:
      items.push_back(menuRow("Format", Clock::use12h() ? "12-hour" : "24-hour"));
      items.push_back(menuRow("Time zone", String(Clock::zoneLabel(Clock::currentZone())) + " >"));
      items.push_back(menuRow("Sync time now", ""));
      break;
    case SysScreen::Zones:
      for (int i = 0; i < Clock::zoneCount(); i++)
        items.push_back(menuRow(Clock::zoneLabel(i), i == Clock::currentZone() ? "*" : ""));
      break;
  }

  int rows = (SCREEN_H - y0 - LINE_H) / LINE_H;
  if (sysScr == SysScreen::Clock) rows = CLOCK_ITEMS;
  drawList(items, sysSel, sysTop, y0, rows);

  int y = y0 + rows * LINE_H + 3 * T;
  if (sysScr == SysScreen::Clock) {
    screen.g().fillRect(0, y, SCREEN_W, T, INK);
    y += 3 * T;
    auto line = [&](const String& s) {
      String t = s;
      if ((int)t.length() > TEXT_COLS) t = t.substring(0, TEXT_COLS - 1) + "~";
      screen.text(MARGIN, y, t.c_str(), T);
      y += LINE_H;
    };
    line(Clock::dateLine());
    line(String("Set from ") + Clock::source() + (Clock::rtcName() ? String(", ") + Clock::rtcName() + " RTC" : String(", no RTC")));
    if (Clock::syncStatus().length()) line(Clock::syncStatus());
  }
  if (sysMsg.length()) screen.text(MARGIN, SCREEN_H - 2 * LINE_H, sysMsg.c_str(), T);
  screen.text(MARGIN, SCREEN_H - LINE_H,
              sysScr == SysScreen::Menu ? "Right=open  Esc=exit" : "Right=choose  Left=back", T);

}

static void sysDraw() {
  sysDrawScreen();
  if (sysRollAsk)
    drawDialog("Go back to the previous firmware?", { String("Now running ") + FW_VERSION + ".",
               "InkDeck restarts into the version you had before the last update." },
               "Cancel", "Go back", sysRollYes);

}

// =====================================================================
//  Uploader — WiFi + web page for installing apps and moving files.
//  WiFi is only on while this app is open.
//  Screens: network list (scan) -> password -> status (address to open).
// =====================================================================
static bool upTypingPassword = false;
static String upSsid, upPass;
static bool upShowPass = false;
static int upSel = 0, upTop = 0;

static bool upPicking() {
  const WebUI::Mode m = WebUI::mode();
  return !upTypingPassword && (m == WebUI::Mode::Scanning || m == WebUI::Mode::Idle);
}

// Network list = scan results + two fixed entries at the end
static int upListCount() { return (int)WebUI::networks().size() + 2; }

static String signalBars(int rssi) {
  if (rssi >= -55) return "||||";
  if (rssi >= -65) return "||| ";
  if (rssi >= -75) return "||  ";
  return "|   ";
}

static void upEnter() {
  if (!Storage::mounted()) Storage::begin();
  upTypingPassword = false;
  upSel = 0; upTop = 0;
  WebUI::start();
}

// ---- Firmware update (uploaded from the web page, confirmed here) ----
static bool upFwAsk = false, upFwYes = false;
static String upFwVersion, upFwStatus;
static uint32_t upFwSize = 0;
static int upFwPct = -1;                        // >= 0 while installing

static void upFwProgress(int pct) {             // redraw straight away: the main loop is busy flashing
  upFwPct = pct;
  AppMgr::requestRedraw(Refresh::Partial);
  AppMgr::tick();
}

static void upFwInstall() {
  upFwAsk = false;
  WebUI::stop();                                // nothing else should run while flashing
  String why;
  upFwProgress(0);
  if (Ota::install(UPDATE_FILE, upFwProgress, why)) {
    Storage::remove(UPDATE_FILE);
    upFwStatus = "Installed " + upFwVersion + ". Restarting...";
    upFwPct = 100;
    AppMgr::requestRedraw(Refresh::Full);
    AppMgr::tick();
    delay(1500);
    ESP.restart();
    return;                                     // (restart doesn't return)
  }
  upFwPct = -1;
  upFwStatus = "Update failed: " + why;         // the current firmware is untouched
  WebUI::clearUpdate(true);
  AppMgr::requestRedraw(Refresh::Full);
}

static void upTick() {
  if (upFwPct >= 0) return;
  WebUI::loop();
  if (!upFwAsk && WebUI::updatePending(upFwVersion, upFwSize)) {   // a new firmware arrived
    upFwAsk = true;
    upFwYes = false;                            // Cancel is the default
    upFwStatus = "";
    AppMgr::requestRedraw(Refresh::Full);
  }
  if (WebUI::changed()) {
    if (upSel >= upListCount()) upSel = 0;
    AppMgr::requestRedraw(Refresh::Partial);
  }
}

static void upKey(uint8_t k) {
  // ---- confirming a firmware update ----
  if (upFwAsk) {
    switch (k) {
      case K_LEFT: case K_RIGHT: case K_UP: case K_DOWN: case K_TAB: upFwYes = !upFwYes; break;
      case K_ENTER:
        if (upFwYes) { upFwInstall(); return; }
        upFwAsk = false;
        WebUI::clearUpdate(true);
        upFwStatus = "Update cancelled";
        AppMgr::requestRedraw(Refresh::Full);
        return;
      default: return;
    }
    AppMgr::requestRedraw(Refresh::Partial);
    return;
  }
  if (upFwPct >= 0) return;

  // ---- typing a password ----
  if (upTypingPassword) {
    if (k == K_ENTER) {
      upTypingPassword = false;
      WebUI::connectTo(upSsid, upPass);
    }
    else if (k == K_BKSP)          { if (upPass.length()) upPass.remove(upPass.length() - 1); }
    else if (k == K_TAB)           upShowPass = !upShowPass;
    else if (fnKeyName(k) && !strcmp(fnKeyName(k), "v")) {       // Fn+V: paste
      const String& c = Clipboard::get();
      for (size_t i = 0; i < c.length() && upPass.length() < 63; i++)
        if (isPrintableKey((uint8_t)c[i])) upPass += c[i];
    }
    else if (isPrintableKey(k))    { if (upPass.length() < 63) upPass += (char)k; }
    else return;
    AppMgr::requestRedraw(Refresh::Partial);
    return;
  }

  // ---- picking a network ----
  if (upPicking()) {
    if (WebUI::mode() == WebUI::Mode::Scanning) return;   // list not ready yet
    const int count = upListCount();
    const int nNets = (int)WebUI::networks().size();
    switch (k) {
      case K_UP:   if (upSel > 0) upSel--; break;
      case K_DOWN: if (upSel < count - 1) upSel++; break;
      case K_ENTER:
        if (upSel < nNets) {
          const WebUI::Network& n = WebUI::networks()[upSel];
          if (n.open) {
            WebUI::connectTo(n.ssid, "");
          } else {
            upSsid = n.ssid;
            upPass = "";
            upShowPass = false;
            upTypingPassword = true;
          }
        } else if (upSel == nNets) {
          WebUI::startScan();                 // "Scan again"
          upSel = 0; upTop = 0;
        } else {
          WebUI::startHotspot();              // "Use hotspot instead"
        }
        AppMgr::requestRedraw(Refresh::Full);
        return;
      default: return;
    }
    AppMgr::requestRedraw(Refresh::Partial);
    return;
  }

  // ---- status screen ----
  if (k == 'w' || k == 'W') {
    WebUI::startScan();
    upSel = 0; upTop = 0;
    AppMgr::requestRedraw(Refresh::Full);
  }
}

static bool upBack() {
  if (upFwAsk) {                               // Esc = Cancel
    upFwAsk = false;
    WebUI::clearUpdate(true);
    upFwStatus = "Update cancelled";
    AppMgr::requestRedraw(Refresh::Full);
    return true;
  }
  if (upTypingPassword) {                      // password -> back to the list
    upTypingPassword = false;
    AppMgr::requestRedraw(Refresh::Full);
    return true;
  }
  WebUI::stop();
  return false;                                // go home (the launcher rescans /apps)
}

static void upDrawScreen() {
  // ---- installing: a progress bar, nothing else ----
  if (upFwPct >= 0) {
    const int bw = SCREEN_W - 8 * MARGIN, bh = 10 * T;
    const int bx = 4 * MARGIN, by = BODY_Y + (SCREEN_H - BODY_Y) / 2;
    screen.textCentered(SCREEN_W / 2, by - 3 * LINE_H, upFwStatus.length() ? upFwStatus.c_str()
                        : (String("Installing ") + upFwVersion).c_str(), T);
    screen.textCentered(SCREEN_W / 2, by - 2 * LINE_H + T, "Don't turn InkDeck off", T);
    screen.g().drawRect(bx, by, bw, bh, INK);
    screen.g().fillRect(bx + 2 * T, by + 2 * T, (bw - 4 * T) * upFwPct / 100, bh - 4 * T, INK);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", upFwPct);
    screen.textCentered(SCREEN_W / 2, by + bh + LINE_H / 2, pct, T);
    return;
  }

  int y = BODY_Y + 3 * T;
  auto row = [&](const String& s) {
    String t = s;
    if ((int)t.length() > TEXT_COLS) t = t.substring(0, TEXT_COLS);
    screen.text(MARGIN, y, t.c_str(), T);
    y += LINE_H;
  };

  if (!Storage::mounted()) row("No SD card - uploads won't work");
  if (upFwStatus.length()) row(upFwStatus);

  // ---- password entry ----
  if (upTypingPassword) {
    row("Password for:");
    row(upSsid);
    y += 2 * T;
    const int boxH = LINE_H + 4 * T;
    screen.g().drawRect(MARGIN, y, SCREEN_W - 2 * MARGIN, boxH, INK);
    String shown = upShowPass ? upPass : String();
    if (!upShowPass) for (size_t i = 0; i < upPass.length(); i++) shown += '*';
    const int maxChars = (SCREEN_W - 4 * MARGIN) / CW - 1;
    if ((int)shown.length() > maxChars) shown = shown.substring(shown.length() - maxChars);
    screen.text(2 * MARGIN, y + 3 * T, shown.c_str(), T);
    screen.g().fillRect(2 * MARGIN + shown.length() * CW, y + 3 * T, 5 * T, CH, INK);   // cursor
    screen.text(MARGIN, SCREEN_H - 2 * LINE_H, upShowPass ? "Tab = hide password" : "Tab = show password", T);
    screen.text(MARGIN, SCREEN_H - LINE_H, "Enter = connect  Esc = back", T);
    return;
  }

  // ---- network list ----
  if (upPicking()) {
    if (WebUI::notice().length()) row(WebUI::notice());
    if (WebUI::mode() == WebUI::Mode::Scanning) {
      row("Scanning for WiFi networks...");
      return;
    }
    const auto& nets = WebUI::networks();
    row(nets.empty() ? "No networks found." : "Choose a WiFi network:");

    std::vector<String> items;
    const int nameCols = TEXT_COLS - 10;
    for (const auto& n : nets) {
      String name = n.ssid;
      if ((int)name.length() > nameCols) name = name.substring(0, nameCols - 2) + "..";
      String right = String(n.open ? "open " : "") + signalBars(n.rssi);
      while ((int)(name.length() + right.length()) < TEXT_COLS) name += ' ';
      items.push_back(name + right);
    }
    items.push_back("> Scan again");
    items.push_back("> Use hotspot instead");

    const int rows = (SCREEN_H - y - LINE_H) / LINE_H;
    drawList(items, upSel, upTop, y, rows);
    screen.text(MARGIN, SCREEN_H - LINE_H, "Enter = select  Esc = exit", T);
    return;
  }

  // ---- status ----
  switch (WebUI::mode()) {
    case WebUI::Mode::Connecting:
      row("Connecting to " + WebUI::networkName() + "...");
      break;
    case WebUI::Mode::Station:
      row("WiFi: " + WebUI::networkName());
      break;
    case WebUI::Mode::Hotspot:
      row("Hotspot: " + WebUI::networkName());
      row(String("Password: ") + AP_PASSWORD);
      break;
    default:
      row("WiFi off");
      break;
  }

  if (WebUI::address().length()) {
    y += T * 4;
    row("Open in a browser:");
    row("http://" + WebUI::address());
    row("http://" MDNS_NAME ".local");
    y += T * 4;
    row("Files received: " + String(WebUI::filesReceived()));
    if (WebUI::lastEvent().startsWith("Got ") || WebUI::lastEvent().startsWith("Deleted "))
      row(WebUI::lastEvent());
  }

  screen.text(MARGIN, SCREEN_H - LINE_H, "w = change WiFi  Esc = exit", T);

}

// The update dialog goes on top of whatever the Uploader is showing
// (upDrawScreen returns early from several screens)
static void upDraw() {
  upDrawScreen();
  if (upFwAsk) {
    char sz[24];
    snprintf(sz, sizeof(sz), "%.1f MB", upFwSize / (1024.0 * 1024.0));
    std::vector<String> lines = {
      String("Now running ") + FW_VERSION + ". New: " + upFwVersion + " (" + sz + ").",
      "Takes about a minute, then InkDeck restarts.",
    };
    drawDialog("Install firmware " + upFwVersion + "?", lines, "Cancel", "Install", upFwYes);
  }

}
