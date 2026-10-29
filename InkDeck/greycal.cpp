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
//  brightness 0 (black) .. 255 (white); results are levels 0 white .. top
//  black (3, or 7 with the experimental 8 shades).
// ---------------------------------------------------------------------
enum Style { NATURAL, CLASSIC, LINEAR };

static float toLinear(float v) {
  const float c = v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static void palette(Style s, int n, float* pal) {      // darkest first, n evenly spaced shades
  for (int k = 0; k < n; k++) {
    const float v = 255.0f * k / (n - 1);
    pal[k] = s == LINEAR ? toLinear(v) : v;
  }
}

static int nearest(const float* pal, int n, float v) {
  int k = 0; float d = 1e9f;
  for (int j = 0; j < n; j++) { const float e = fabsf(v - pal[j]); if (e < d) { d = e; k = j; } }
  return k;
}
// Palette entry k (0 = darkest) -> the screen's level (0 = white .. top = black)
static int topLevel = 3;
static uint8_t levelOf(int k, int n) {
  return (uint8_t)(((n - 1 - k) * topLevel * 2 + (n - 1)) / (2 * (n - 1)));
}

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

static Img dither(const std::vector<float>& src, int w, int h, Style s, int shades, int top) {
  Img im; im.w = w; im.h = h; im.lv.assign(w * h, 0);
  std::vector<float> v(src);
  if (s == LINEAR) for (auto& x : v) x = toLinear(x);
  topLevel = top;
  float pal[8];
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

// One test picture's parts: two ramps and three balls with their labels
struct Set {
  bool built = false;
  Img ramps[2], balls[3];
  const char* rampNote;
  const char* ballNames[3];
};
static Set set4 = { false, {}, {}, "4 greys / black+white", { "Natural", "Classic", "Exact" } };
static Set set8 = { false, {}, {}, "8 greys / 4 greys", { "8 Nat", "4 Nat", "8 Exact" } };
static int ballD = 0;

static Set& build() {
  const bool eight = screen.grey8();
  Set& st = eight ? set8 : set4;
  if (st.built) return st;
  const int top = eight ? 7 : 3;
  const int rw = PIC_W, rh = 8 * T;
  const std::vector<float> r = ramp(rw, rh);
  ballD = std::min(PIC_W / 3 - 6 * T, 30 * T);
  const std::vector<float> b = ball(ballD);
  if (eight) {                                 // 8 against 4, both shown with the 8-shade refresh
    st.ramps[0] = dither(r, rw, rh, NATURAL, 8, top);
    st.ramps[1] = dither(r, rw, rh, NATURAL, 4, top);
    st.balls[0] = dither(b, ballD, ballD, NATURAL, 8, top);
    st.balls[1] = dither(b, ballD, ballD, NATURAL, 4, top);
    st.balls[2] = dither(b, ballD, ballD, LINEAR, 8, top);
  } else {
    st.ramps[0] = dither(r, rw, rh, NATURAL, 4, top);
    st.ramps[1] = dither(r, rw, rh, NATURAL, 2, top);
    st.balls[0] = dither(b, ballD, ballD, NATURAL, 4, top);
    st.balls[1] = dither(b, ballD, ballD, CLASSIC, 4, top);
    st.balls[2] = dither(b, ballD, ballD, LINEAR, 4, top);
  }
  st.built = true;
  return st;
}

// ---------------------------------------------------------------------
//  Settings rows
// ---------------------------------------------------------------------
// The rows depend on the shades: 4 (the real ones) or 8 (experimental)
enum Row { R_SHADES, R_LIGHT, R_DARK, R_STEP1, R_STEP2, R_BLACK, R_CLEAR, R_REPEAT, R_RESET };
static std::vector<int> rowList() {
  if (screen.grey8()) return { R_SHADES, R_LIGHT, R_DARK, R_STEP1, R_STEP2, R_BLACK, R_CLEAR, R_RESET };
  return { R_SHADES, R_LIGHT, R_DARK, R_BLACK, R_CLEAR, R_REPEAT, R_RESET };
}
static int rowAt(int sel) {
  const std::vector<int> r = rowList();
  return sel >= 0 && sel < (int)r.size() ? r[sel] : -1;
}
int rows() { return rowList().size(); }

const char* hint() { return "Up/Dn pick  Lt/Rt change  Esc back"; }

void leave() { screen.setGrey8(false); }

void change(int sel, int dir) {
  const int id = rowAt(sel);
  auto bump = [&](uint8_t& v, int step, int lo, int hi) { v = (uint8_t)constrain((int)v + dir * step, lo, hi); };
  if (id == R_SHADES) { screen.setGrey8(!screen.grey8()); return; }
  if (screen.grey8() && (id == R_LIGHT || id == R_DARK || id == R_STEP1 || id == R_STEP2)) {
    Screen::Grey8Timing t = screen.grey8Timing();
    switch (id) {
      case R_LIGHT: bump(t.light, 1, 1, 40); break;
      case R_DARK:  bump(t.dark, 1, 1, 50); break;
      case R_STEP1: bump(t.step1, 1, 1, 63); break;
      case R_STEP2: bump(t.step2, 1, 1, 63); break;
    }
    screen.setGrey8Timing(t);
    return;
  }
  Screen::GreyTiming t = screen.greyTiming();
  switch (id) {
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
  const int id = rowAt(sel);
  if (id == R_RESET) {
    if (screen.grey8()) screen.setGrey8Timing(Screen::grey8Defaults());   // its own timings only
    else                screen.setGreyTiming(Screen::greyDefaults());
  } else if (id == R_REPEAT || id == R_SHADES) change(sel, 1);
}

void draw(int sel) {
  const bool eight = screen.grey8();
  const int top = eight ? 7 : 3;
  Set& st = build();
  GFXcanvas1& g = screen.g();
  int y = BODY_Y + 3 * T;

  // The shades, white to black
  const int n = top + 1, gap = eight ? T : 2 * T;
  const int sw = (PIC_W - (n - 1) * gap) / n, sh = 12 * T;
  static const char* SHADES[4] = { "White", "Light", "Dark", "Black" };
  for (int i = 0; i < n; i++) {
    const int x = PIC_X + i * (sw + gap);
    for (int yy = 0; yy < sh; yy++) for (int xx = 0; xx < sw; xx++) screen.setGrey(x + xx, y + yy, i);
    g.drawRect(x, y, sw, sh, INK);
    const String label = eight ? String(i) : String(SHADES[i]);
    screen.textCentered(x + sw / 2, y + sh + T, label.c_str(), T);
  }
  y += sh + LINE_H + T;

  // Two ramps
  blit(st.ramps[0], PIC_X, y);
  y += st.ramps[0].h + T;
  blit(st.ramps[1], PIC_X, y);
  y += st.ramps[1].h + 2 * T;
  screen.text(PIC_X, y, st.rampNote, T);
  y += LINE_H + T;

  // Three balls
  const int col = PIC_W / 3;
  for (int i = 0; i < 3; i++) {
    const int cx = PIC_X + i * col + col / 2;
    blit(st.balls[i], cx - ballD / 2, y);
    screen.textCentered(cx, y + ballD + T, st.ballNames[i], T);
  }

  // Settings list on the right
  const Screen::GreyTiming t = screen.greyTiming();
  const Screen::GreyTiming d = Screen::greyDefaults();   // to say whether these are the defaults
  const Screen::Grey8Timing t8 = screen.grey8Timing();
  const Screen::Grey8Timing d8 = Screen::grey8Defaults();
  const int cols = (SCREEN_W - LIST_X - MARGIN) / CW;
  const std::vector<int> list = rowList();
  for (int i = 0; i < (int)list.size(); i++) {
    const char* label = "";
    String val;
    switch (list[i]) {
      case R_SHADES: label = "Shades"; val = String(n); break;
      case R_LIGHT:  label = "Light";  val = String(eight ? t8.light : t.light); break;
      case R_DARK:   label = "Dark";   val = String(eight ? t8.dark : t.dark); break;
      case R_STEP1:  label = "Step 1"; val = String(t8.step1); break;
      case R_STEP2:  label = "Step 2"; val = String(t8.step2); break;
      case R_BLACK:  label = "Black";  val = String(t.black); break;
      case R_CLEAR:  label = "Clean";  val = String(t.clear); break;
      case R_REPEAT: label = "Repeat"; val = String(t.repeat); break;
      case R_RESET:  label = "Reset";  break;
    }
    String s = label;
    while ((int)(s.length() + val.length()) < cols) s += ' ';
    s += val;
    const int ry = BODY_Y + 3 * T + i * LINE_H;
    screen.text(LIST_X, ry + T, s.c_str(), T);
    if (i == sel) screen.invertRect(LIST_X - T, ry, SCREEN_W - LIST_X - MARGIN + 2 * T, LINE_H);
  }

  // What the numbers mean, under the list (as many lines as fit)
  int ny = BODY_Y + 3 * T + list.size() * LINE_H + 2 * T;
  const bool isDefault = eight ? (t8.light == d8.light && t8.dark == d8.dark && t8.step1 == d8.step1 && t8.step2 == d8.step2)
                               : (t.light == d.light && t.dark == d.dark && t.black == d.black &&
                                  t.clear == d.clear && t.repeat == d.repeat);
  const char* status = !screen.hasGreyWaveforms() ? "(TFT: none)" : isDefault ? "(defaults)" : "(saved)";
  std::vector<const char*> notes;
  if (eight) notes = { status, "steps 5ms" };
  else       notes = { "20ms frames", "more=darker", status };
  for (const char* line : notes) {
    if (ny + LINE_H > SCREEN_H - LINE_H) break;        // keep clear of the hint line
    screen.text(LIST_X, ny, line, T);
    ny += LINE_H;
  }
}
}
