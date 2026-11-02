#include "picture.h"
#include "config.h"
#include <SD.h>
#include <vector>
#include <algorithm>
#include <math.h>
#include <esp_heap_caps.h>

#if EPUB_PICTURES
#include <JPEGDEC.h>
#include <PNGdec.h>
#endif

namespace Picture {

#if EPUB_PICTURES

bool supported() { return true; }

namespace {

uint8_t* bigBuf(size_t n) {
  uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : (uint8_t*)malloc(n);
}

// A grey picture, 0 = black .. 255 = white
struct Grey {
  int w = 0, h = 0;
  uint8_t* px = nullptr;
  ~Grey() { free(px); }
  bool alloc(int W, int H) {
    w = W; h = H;
    px = bigBuf((size_t)w * h);
    if (px) memset(px, 255, (size_t)w * h);
    return px != nullptr;
  }
};

// ---------------------------------------------------------------------
//  JPEG: decoded straight to 8-bit grey, at 1/2, 1/4 or 1/8 size when the
//  picture is that much bigger than it will be shown
// ---------------------------------------------------------------------
int jpegDraw(JPEGDRAW* d) {
  Grey* g = (Grey*)d->pUser;
  const uint8_t* src = (const uint8_t*)d->pPixels;
  for (int y = 0; y < d->iHeight; y++) {
    const int yy = d->y + y;
    if (yy >= g->h) break;
    for (int x = 0; x < d->iWidthUsed; x++) {
      const int xx = d->x + x;
      if (xx < g->w) g->px[yy * g->w + xx] = src[y * d->iWidth + x];
    }
  }
  return 1;
}

bool decodeJpeg(const uint8_t* data, size_t len, int needW, int needH, int& w0, int& h0, Grey& g, String& why) {
  JPEGDEC* jpeg = new JPEGDEC;
  if (!jpeg) { why = "out of memory"; return false; }
  bool ok = false;
  if (jpeg->openRAM((uint8_t*)data, len, jpegDraw)) {
    jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
    if (jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE) {    // JPEGDEC only reads baseline JPEGs properly
      jpeg->close();
      delete jpeg;
      why = "progressive JPEG (not supported)";
      return false;
    }
    w0 = jpeg->getWidth();
    h0 = jpeg->getHeight();
    int scale = 1;
    for (int s : { 8, 4, 2 }) if (w0 / s >= needW && h0 / s >= needH) { scale = s; break; }
    if ((int64_t)w0 * h0 / ((int64_t)scale * scale) > 12000000) why = "too big";
    else if (!g.alloc((w0 + scale - 1) / scale, (h0 + scale - 1) / scale)) why = "out of memory";
    else {
      jpeg->setUserPointer(&g);
      const int opt = scale == 8 ? JPEG_SCALE_EIGHTH : scale == 4 ? JPEG_SCALE_QUARTER : scale == 2 ? JPEG_SCALE_HALF : 0;
      ok = jpeg->decode(0, 0, opt) == 1;
      if (!ok) why = "JPEG not readable (progressive?)";
    }
    jpeg->close();
  } else {
    why = "JPEG not readable";
  }
  delete jpeg;
  return ok;
}

// ---------------------------------------------------------------------
//  PNG: line by line, averaged down by a whole number as it comes in
//  (transparent parts on white)
// ---------------------------------------------------------------------
struct PngSink {
  PNG* png;
  Grey* g;
  int f;                      // source pixels per output pixel, each way
  std::vector<uint32_t> sum;
  std::vector<uint16_t> cnt;
  std::vector<uint16_t> line;
  int lastRow;
};

void pngFlush(PngSink& s, int gy) {
  if (gy >= s.g->h) return;
  for (int x = 0; x < s.g->w; x++) s.g->px[gy * s.g->w + x] = s.cnt[x] ? s.sum[x] / s.cnt[x] : 255;
  std::fill(s.sum.begin(), s.sum.end(), 0);
  std::fill(s.cnt.begin(), s.cnt.end(), 0);
}

int pngDraw(PNGDRAW* d) {
  PngSink& s = *(PngSink*)d->pUser;
  s.png->getLineAsRGB565(d, s.line.data(), PNG_RGB565_LITTLE_ENDIAN, 0x00FFFFFF);
  for (int x = 0; x < d->iWidth; x++) {
    const uint16_t c = s.line[x];
    const int r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;      // to 0..255 (white stays 255)
    const int r = (r5 << 3) | (r5 >> 2), gg = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
    const int gx = x / s.f;
    if (gx < s.g->w) { s.sum[gx] += (r * 77 + gg * 150 + b * 29) >> 8; s.cnt[gx]++; }
  }
  if (d->y % s.f == s.f - 1 || d->y == s.lastRow) pngFlush(s, d->y / s.f);
  return 1;
}

bool decodePng(const uint8_t* data, size_t len, int needW, int needH, int& w0, int& h0, Grey& g, String& why) {
  PNG* png = new PNG;
  if (!png) { why = "out of memory"; return false; }
  bool ok = false;
  if (png->openRAM((uint8_t*)data, len, pngDraw) == PNG_SUCCESS) {
    w0 = png->getWidth();
    h0 = png->getHeight();
    const int f = std::max(1, std::min(w0 / std::max(1, needW), h0 / std::max(1, needH)));
    if ((int64_t)w0 * h0 > 40000000) why = "too big";
    else if (!g.alloc((w0 + f - 1) / f, (h0 + f - 1) / f)) why = "out of memory";
    else {
      PngSink s{ png, &g, f, std::vector<uint32_t>(g.w), std::vector<uint16_t>(g.w), std::vector<uint16_t>(w0), h0 - 1 };
      ok = png->decode(&s, 0) == PNG_SUCCESS;
      if (!ok) why = "PNG not readable";
    }
    png->close();
  } else {
    why = "PNG not readable";
  }
  delete png;
  return ok;
}

// ---------------------------------------------------------------------
//  Shades: the converter's three picture styles
// ---------------------------------------------------------------------
float toLinear(float v) {
  const float c = v / 255.0f;
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

int nearest(const float* pal, int n, float v) {
  int k = 0;
  float d = 1e9f;
  for (int j = 0; j < n; j++) { const float e = fabsf(v - pal[j]); if (e < d) { d = e; k = j; } }
  return k;
}

// palette index (darkest first) -> level (0 white .. 3 black)
uint8_t levelOf(int k, int n) { return (uint8_t)lroundf((n - 1 - k) * 3.0f / (n - 1)); }

void floyd(std::vector<float>& v, int w, int h, const float* pal, int n, uint8_t* out) {
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

void atkinson(std::vector<float>& v, int w, int h, const float* pal, int n, uint8_t* out) {
  const int N = w * h;
  uint32_t hist[256] = { 0 };
  for (int i = 0; i < N; i++) hist[(int)std::min(255.0f, std::max(0.0f, v[i]))]++;
  int lo = 0, hi = 255;
  uint32_t acc = 0;
  for (int g = 0; g < 256; g++) { acc += hist[g]; if (acc >= N * 0.01f) { lo = g; break; } }
  acc = 0;
  for (int g = 255; g >= 0; g--) { acc += hist[g]; if (acc >= N * 0.01f) { hi = g; break; } }
  const float span = std::max(1, hi - lo);
  for (int i = 0; i < N; i++) {
    float t = (v[i] - lo) / span;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    v[i] = 255 * powf(t, 0.85f);
  }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const int i = y * w + x, q = nearest(pal, n, v[i]);
      out[i] = levelOf(q, n);
      const float e = (v[i] - pal[q]) / 8;
      auto add = [&](int xx, int yy) { if (xx >= 0 && xx < w && yy < h) v[yy * w + xx] += e; };
      add(x + 1, y); add(x + 2, y); add(x - 1, y + 1); add(x, y + 1); add(x + 1, y + 1); add(x, y + 2);
    }
}

// ---------------------------------------------------------------------
//  BMP, as the converter writes them: 1-bit when there's no grey, else
//  4-bit with a 4-entry grey palette (index = level)
// ---------------------------------------------------------------------
void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

bool writeBmp(const String& path, const uint8_t* lv, int w, int h) {
  bool grey = false;
  for (int i = 0; i < w * h && !grey; i++) grey = lv[i] == 1 || lv[i] == 2;
  const int bits = grey ? 4 : 1, colours = grey ? 4 : 2;
  const uint32_t stride = ((w * bits + 31) >> 5) << 2, off = 14 + 40 + colours * 4, size = off + stride * h;
  uint8_t head[14 + 40 + 16] = { 0 };
  head[0] = 'B'; head[1] = 'M';
  put32(head + 2, size); put32(head + 10, off); put32(head + 14, 40);
  put32(head + 18, w); put32(head + 22, h); put16(head + 26, 1); put16(head + 28, bits);
  put32(head + 34, stride * h); put32(head + 46, colours); put32(head + 50, colours);
  if (grey) {
    const uint8_t v[4] = { 255, 170, 85, 0 };
    for (int i = 0; i < 4; i++) head[54 + i * 4] = head[55 + i * 4] = head[56 + i * 4] = v[i];
  } else {
    head[58] = head[59] = head[60] = 255;                    // 0 = black (ink), 1 = white
  }
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(head, off) == off;
  std::vector<uint8_t> row(stride);
  for (int y = h - 1; ok && y >= 0; y--) {                   // bottom row first
    std::fill(row.begin(), row.end(), 0);
    for (int x = 0; x < w; x++) {
      const uint8_t v = lv[y * w + x] & 3;
      if (grey) row[x >> 1] |= (x & 1) ? v : v << 4;
      else if (v < 2) row[x >> 3] |= 0x80 >> (x & 7);
    }
    ok = f.write(row.data(), stride) == stride;
  }
  f.close();
  return ok;
}

}  // namespace

bool convert(const uint8_t* data, size_t len, int maxW, int maxH, const Style& st,
             const String& bmpPath, int& outW, int& outH, String& why) {
  if (len < 8) { why = "empty"; return false; }
  const bool isJpeg = data[0] == 0xFF && data[1] == 0xD8;
  const bool isPng = data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
  if (!isJpeg && !isPng) { why = "not JPEG or PNG"; return false; }

  // Decode at least as big as it will be shown (either way round)
  Grey g;
  int w0 = 0, h0 = 0;
  const int need = std::min(maxW, maxH);
  const bool ok = isJpeg ? decodeJpeg(data, len, need, need, w0, h0, g, why)
                         : decodePng(data, len, need, need, w0, h0, g, why);
  if (!ok) return false;
  if (w0 < 24 && h0 < 24) { why = "a tiny decoration"; return false; }

  // Fit, as the converter does: never enlarged; tall pictures may be turned
  const float s = std::min({ 1.0f, (float)maxW / w0, (float)maxH / h0 });
  const float sTurned = std::min({ 1.0f, (float)maxW / h0, (float)maxH / w0 });
  const bool turn = st.rotate && h0 > w0 && s < 1 && sTurned > s * 1.2f;
  const float k = turn ? sTurned : s;
  const int sw = std::max(1, (int)lroundf(w0 * k)), sh = std::max(1, (int)lroundf(h0 * k));
  const int w = turn ? sh : sw, h = turn ? sw : sh;
  if (w < 8 || h < 4) { why = "a tiny decoration"; return false; }

  // Area-average the decoded picture down to sw x sh (upright), then place it
  std::vector<float> v((size_t)w * h);
  const float fx = (float)g.w / sw, fy = (float)g.h / sh;
  int extremes = 0;
  for (int y = 0; y < sh; y++) {
    const int y0 = (int)(y * fy), y1 = std::max(y0 + 1, std::min(g.h, (int)((y + 1) * fy)));
    for (int x = 0; x < sw; x++) {
      const int x0 = (int)(x * fx), x1 = std::max(x0 + 1, std::min(g.w, (int)((x + 1) * fx)));
      uint32_t sum = 0, n = 0;
      for (int yy = y0; yy < y1 && yy < g.h; yy++)
        for (int xx = x0; xx < x1 && xx < g.w; xx++) { sum += g.px[yy * g.w + xx]; n++; }
      const float val = n ? (float)sum / n : 255;
      if (val < 64 || val > 192) extremes++;
      // turned: the picture's top ends up on the left (as the converter does)
      const int px = turn ? y : x, py = turn ? (sw - 1 - x) : y;
      v[(size_t)py * w + px] = val;
    }
  }

  std::vector<uint8_t> lv((size_t)w * h);
  if (extremes > 0.9f * w * h) {
    for (size_t i = 0; i < lv.size(); i++) lv[i] = v[i] < 128 ? 3 : 0;   // line art: crisp
  } else {
    const int n = st.shades == 2 ? 2 : 4;
    float pal[4];
    for (int i = 0; i < n; i++) pal[i] = 255.0f * i / (n - 1);
    if (st.kind == 2) {                                      // linear light
      for (auto& x : v) x = toLinear(std::min(255.0f, std::max(0.0f, x)));
      for (int i = 0; i < n; i++) pal[i] = toLinear(pal[i]);
    }
    if (st.kind == 1) atkinson(v, w, h, pal, n, lv.data());
    else              floyd(v, w, h, pal, n, lv.data());
  }
  if (!writeBmp(bmpPath, lv.data(), w, h)) { why = "can't write to the SD card"; return false; }
  outW = w;
  outH = h;
  return true;
}

#else  // EPUB_PICTURES 0

bool supported() { return false; }
bool convert(const uint8_t*, size_t, int, int, const Style&, const String&, int&, int&, String& why) {
  why = "built without pictures";
  return false;
}

#endif

}
