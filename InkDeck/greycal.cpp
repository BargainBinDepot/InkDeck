#include "greycal.h"
#include "screen.h"
#include "ui.h"
#include <vector>
#include <math.h>
#include <algorithm>

using namespace UI;

namespace GreyCal {

// ---------------------------------------------------------------------
//  Dithering, ported from the eReader converter (web.html). Values are
//  brightness 0 (black) .. 255 (white); results are levels 0 white .. 3 black.
// ---------------------------------------------------------------------
enum Style { NATURAL, CLASSIC, LINEAR };

static float toLinear(float v) {
  const float c = v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static void palette(Style s, int n, float* pal) {      // darkest first
  if (s == LINEAR) {
    if (n == 4) { pal[0] = 0; pal[1] = toLinear(85); pal[2] = toLinear(170); pal[3] = 1; }
    else        { pal[0] = 0; pal[1] = 1; }
  } else {
    if (n == 4) { pal[0] = 0; pal[1] = 85; pal[2] = 170; pal[3] = 255; }
    else        { pal[0] = 0; pal[1] = 255; }
  }
}

static int nearest(const float* pal, int n, float v) {
  int k = 0; float d = 1e9f;
  for (int j = 0; j < n; j++) { const float e = fabsf(v - pal[j]); if (e < d) { d = e; k = j; } }
  return k;
}
static uint8_t levelOf(int k, int n) { return (uint8_t)((n - 1 - k) * 3 / (n - 1)); }

// Floyd-Steinberg, serpentine (natural, and linear in linear light)
static void floyd(std::vector<float>& v, int w, int h, const float* pal, int n, std::vector<uint8_t>& out) {
  for (int y = 0; y < h; y++) {
    const bool ltr = (y & 1) == 0;
    const int dir = ltr ? 1 : -1;
    for (int k = 0; k < w; k++) {
      const int x = ltr ? k : w - 1 - k, i = y * w + x;
      const int q = nearest(pal, n, v[i]);
      out[i] = levelOf(q, n);
      const float e = v[i] - pal[q];
      if (x + dir >= 0 && x + dir < w) v[i + dir] += e * 7 / 16;
      if (y + 1 < h) {
        if (x - dir >= 0 && x - dir < w) v[i + w - dir] += e * 3 / 16;
        v[i + w] += e * 5 / 16;
        if (x + dir >= 0 && x + dir < w) v[i + w + dir] += e * 1 / 16;
      }
    }
  }
}

// Atkinson with the contrast stretched and mid-tones lightened (classic)
static void atkinson(std::vector<float>& v, int w, int h, const float* pal, int n, std::vector<uint8_t>& out) {
  const int N = w * h;
  uint32_t hist[256] = { 0 };
  for (int i = 0; i < N; i++) hist[(int)constrain(v[i], 0.0f, 255.0f)]++;
  int lo = 0, hi = 255; uint32_t acc = 0;
  for (int g = 0; g < 256; g++) { acc += hist[g]; if (acc >= N * 0.01f) { lo = g; break; } }
  acc = 0;
  for (int g = 255; g >= 0; g--) { acc += hist[g]; if (acc >= N * 0.01f) { hi = g; break; } }
  const float span = std::max(1, hi - lo);
  for (int i = 0; i < N; i++) { float t = (v[i] - lo) / span; t = t < 0 ? 0 : t > 1 ? 1 : t; v[i] = 255 * powf(t, 0.85f); }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const int i = y * w + x, q = nearest(pal, n, v[i]);
      out[i] = levelOf(q, n);
      const float e = (v[i] - pal[q]) / 8;
      auto add = [&](int xx, int yy) { if (xx >= 0 && xx < w && yy < h) v[yy * w + xx] += e; };
      add(x + 1, y); add(x + 2, y); add(x - 1, y + 1); add(x, y + 1); add(x + 1, y + 1); add(x, y + 2);
    }
}

struct Img { int w = 0, h = 0; std::vector<uint8_t> lv; };

static Img dither(const std::vector<float>& src, int w, int h, Style s, int shades) {
  Img im; im.w = w; im.h = h; im.lv.assign(w * h, 0);
  std::vector<float> v(src);
  if (s == LINEAR) for (auto& x : v) x = toLinear(x);
  float pal[4];
  palette(s, shades, pal);
  if (s == CLASSIC) atkinson(v, w, h, pal, shades, im.lv);
  else              floyd(v, w, h, pal, shades, im.lv);
  return im;
}

// Test sources: a left-to-right ramp, and a ball lit from the top left
static std::vector<float> ramp(int w, int h) {
  std::vector<float> v(w * h);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) v[y * w + x] = 255.0f * x / (w - 1);
  return v;
}
static std::vector<float> ball(int d) {
  std::vector<float> v(d * d, 255.0f);
  const float r = d / 2.0f;
  for (int y = 0; y < d; y++)
    for (int x = 0; x < d; x++) {
      const float dx = (x + 0.5f - r) / r, dy = (y + 0.5f - r) / r, d2 = dx * dx + dy * dy;
      if (d2 > 1) continue;
      const float lam = std::max(0.0f, -0.45f * dx - 0.55f * dy + 0.70f * sqrtf(1 - d2));
      v[y * d + x] = std::min(255.0f, 20 + 235 * lam);
    }
  return v;
}

static void blit(const Img& im, int x0, int y0) {
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) screen.setGrey(x0 + x, y0 + y, im.lv[y * im.w + x]);
}

// ---------------------------------------------------------------------
//  Layout (3.7": 416 x 240). Picture on the left, settings on the right.
// ---------------------------------------------------------------------
static const int PIC_X = MARGIN, PIC_W = SCREEN_W * 62 / 100;    // ~258 px of picture
static const int LIST_X = PIC_X + PIC_W + 4 * T;

static bool built = false;
static Img rampGrey, rampBW, balls[3];
static int ballD = 0;
static const char* STYLE_NAMES[3] = { "Natural", "Classic", "Exact" };

static void build() {
  if (built) return;
  const int rw = PIC_W, rh = 8 * T;
  const std::vector<float> r = ramp(rw, rh);
  rampGrey = dither(r, rw, rh, NATURAL, 4);
  rampBW   = dither(r, rw, rh, NATURAL, 2);
  ballD = std::min(PIC_W / 3 - 6 * T, 30 * T);
  const std::vector<float> b = ball(ballD);
  balls[0] = dither(b, ballD, ballD, NATURAL, 4);
  balls[1] = dither(b, ballD, ballD, CLASSIC, 4);
  balls[2] = dither(b, ballD, ballD, LINEAR, 4);
  built = true;
}

// ---------------------------------------------------------------------
//  Settings rows
// ---------------------------------------------------------------------
enum Row { R_LIGHT, R_DARK, R_BLACK, R_CLEAR, R_REPEAT, R_RESET, R_COUNT };
int rows() { return R_COUNT; }

const char* hint() { return "Up/Dn pick  Lt/Rt change  Esc back"; }

void change(int sel, int dir) {
  Screen::GreyTiming t = screen.greyTiming();
  auto bump = [&](uint8_t& v, int step, int lo, int hi) { v = (uint8_t)constrain((int)v + dir * step, lo, hi); };
  switch (sel) {
    case R_LIGHT:  bump(t.light, 1, 1, 40); break;
    case R_DARK:   bump(t.dark, 1, 1, 50); break;
    case R_BLACK:  bump(t.black, 1, 1, 63); break;
    case R_CLEAR:  bump(t.clear, 2, 4, 63); break;
    case R_REPEAT: t.repeat = t.repeat ? 0 : 1; break;
    default: return;
  }
  screen.setGreyTiming(t);
}

void activate(int sel) {
  if (sel == R_RESET) screen.setGreyTiming(Screen::greyDefaults());
  else if (sel == R_REPEAT) change(sel, 1);
}

void draw(int sel) {
  build();
  GFXcanvas1& g = screen.g();
  int y = BODY_Y + 3 * T;

  // The four shades
  const int sw = (PIC_W - 3 * 2 * T) / 4, sh = 12 * T;
  static const char* SHADES[4] = { "White", "Light", "Dark", "Black" };
  for (int i = 0; i < 4; i++) {
    const int x = PIC_X + i * (sw + 2 * T);
    for (int yy = 0; yy < sh; yy++) for (int xx = 0; xx < sw; xx++) screen.setGrey(x + xx, y + yy, i);
    g.drawRect(x, y, sw, sh, INK);
    screen.textCentered(x + sw / 2, y + sh + T, SHADES[i], T);
  }
  y += sh + LINE_H + T;

  // Ramp: 4 greys, then black and white
  blit(rampGrey, PIC_X, y);
  y += rampGrey.h + T;
  blit(rampBW, PIC_X, y);
  y += rampBW.h + 2 * T;
  screen.text(PIC_X, y, "4 greys / black+white", T);
  y += LINE_H + T;

  // One ball per picture style
  const int col = PIC_W / 3;
  for (int i = 0; i < 3; i++) {
    const int cx = PIC_X + i * col + col / 2;
    blit(balls[i], cx - ballD / 2, y);
    screen.textCentered(cx, y + ballD + T, STYLE_NAMES[i], T);
  }

  // Settings list on the right
  const Screen::GreyTiming t = screen.greyTiming();
  const Screen::GreyTiming d = Screen::greyDefaults();   // to say whether these are the defaults
  const int cols = (SCREEN_W - LIST_X - MARGIN) / CW;
  auto row = [&](int i, const char* label, const String& val) {
    String s = label;
    while ((int)(s.length() + val.length()) < cols) s += ' ';
    s += val;
    const int ry = BODY_Y + 3 * T + i * LINE_H;
    screen.text(LIST_X, ry + T, s.c_str(), T);
    if (i == sel) screen.invertRect(LIST_X - T, ry, SCREEN_W - LIST_X - MARGIN + 2 * T, LINE_H);
  };
  row(R_LIGHT,  "Light",  String(t.light));
  row(R_DARK,   "Dark",   String(t.dark));
  row(R_BLACK,  "Black",  String(t.black));
  row(R_CLEAR,  "Clean",  String(t.clear));
  row(R_REPEAT, "Repeat", String(t.repeat));
  row(R_RESET,  "Reset",  "");

  // What the numbers mean, under the list
  int ny = BODY_Y + 3 * T + R_COUNT * LINE_H + 3 * T;
  const bool isDefault = t.light == d.light && t.dark == d.dark && t.black == d.black &&
                         t.clear == d.clear && t.repeat == d.repeat;
  const char* notes[] = { "20ms frames", "more=darker",
                          !screen.hasGreyWaveforms() ? "(TFT: none)" : isDefault ? "(defaults)" : "(saved)" };
  for (const char* n : notes) { screen.text(LIST_X, ny, n, T); ny += LINE_H; }
}

}
