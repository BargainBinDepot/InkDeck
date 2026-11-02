#include "progress.h"
#include "screen.h"
#include "ui.h"

using namespace UI;

namespace Progress {

static bool on = false;
static String title, stage, detail;
static uint32_t lastDraw = 0;
static int lastPct = -1;
static uint8_t spin = 0;

static const uint32_t EVERY_MS = 2000;      // a partial refresh takes ~0.5 s: not more often than this

void begin(const String& t) { on = true; title = t; stage = ""; detail = ""; lastDraw = 0; lastPct = -1; }
void end()   { on = false; }
bool active() { return on; }
void note(const String& d) { detail = d; }

static String mb(uint32_t b) {
  if (b < 1024 * 1024) return String((b + 512) / 1024) + " KB";
  return String(b / 1048576.0f, 1) + " MB";
}

static void draw(int pct, const String& amount) {
  GFXcanvas1& g = screen.g();
  g.fillRect(0, BODY_Y, SCREEN_W, SCREEN_H - BODY_Y, PAPER);
  const int mid = BODY_Y + (SCREEN_H - BODY_Y) / 2;
  String t = title;
  if ((int)t.length() > TEXT_COLS) t = t.substring(0, TEXT_COLS - 1) + "~";
  screen.textCentered(SCREEN_W / 2, mid - 4 * LINE_H, t.c_str(), T);
  screen.textCentered(SCREEN_W / 2, mid - 2 * LINE_H, stage.c_str(), T);

  // The bar: filled to the percentage, or a block moving along when the size isn't known
  const int bx = 6 * MARGIN, bw = SCREEN_W - 12 * MARGIN, bh = 6 * T, by = mid - LINE_H;
  for (int i = 0; i < T; i++) g.drawRect(bx + i, by + i, bw - 2 * i, bh - 2 * i, INK);
  const int inner = bw - 4 * T;
  if (pct >= 0) {
    g.fillRect(bx + 2 * T, by + 2 * T, inner * pct / 100, bh - 4 * T, INK);
  } else {
    const int blk = inner / 5;
    g.fillRect(bx + 2 * T + (spin++ % 5) * blk, by + 2 * T, blk, bh - 4 * T, INK);
  }
  String line = amount;
  if (pct >= 0) line = (line.length() ? line + "   " : String("")) + pct + "%";
  screen.textCentered(SCREEN_W / 2, by + bh + LINE_H / 2, line.c_str(), T);
  if (detail.length()) screen.textCentered(SCREEN_W / 2, by + bh + 2 * LINE_H, detail.c_str(), T);
  screen.refresh(Refresh::Partial);
  lastDraw = millis();
}

void update(const char* st, uint32_t done, uint32_t total, Unit unit) {
  if (!on) return;
  const int pct = total ? (int)std::min<uint64_t>(100, (uint64_t)done * 100 / total) : -1;
  const bool newStage = stage != st;
  // Redraw on a new stage, at the end, or every couple of seconds (if it moved)
  if (!newStage && !(pct == 100 && lastPct != 100) && (millis() - lastDraw < EVERY_MS || (pct >= 0 && pct == lastPct))) return;
  stage = st;
  lastPct = pct;
  String amount;
  if (unit == Unit::Bytes) amount = total ? mb(done) + " / " + mb(total) : mb(done);
  else if (unit == Unit::Count) amount = total ? String(done) + " / " + total : String(done);
  draw(pct, amount);
}

}
