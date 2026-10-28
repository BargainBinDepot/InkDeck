#include "icons.h"
#include "storage.h"
#include "screen.h"
#include <algorithm>

namespace Icons {

// Fit a w x h mono image (1 byte per pixel, 1 = ink) into box x box.
// Downscaling uses area coverage so thin strokes survive.
static void fit(const std::vector<uint8_t>& px, int w, int h, int box, Icon& out) {
  // Small icons (like classic 32x32 pixel art) are enlarged by a whole number,
  // so every pixel stays a crisp square: 32x32 becomes 64x64 in a 64 box.
  const int k = std::min(box / w, box / h);
  if (k >= 2) {
    out.w = w * k;
    out.h = h * k;
    const int stride = (out.w + 7) / 8;
    out.bits.assign(stride * out.h, 0);
    for (int y = 0; y < out.h; y++)
      for (int x = 0; x < out.w; x++)
        if (px[(y / k) * w + (x / k)]) out.bits[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    return;
  }
  int tw = w, th = h;
  if (w > box || h > box) {
    if (w >= h) { tw = box; th = (h * box + w / 2) / w; }
    else        { th = box; tw = (w * box + h / 2) / h; }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
  }
  out.w = tw;
  out.h = th;
  const int stride = (tw + 7) / 8;
  out.bits.assign(stride * th, 0);
  for (int y = 0; y < th; y++) {
    const int y0 = y * h / th, y1 = ((y + 1) * h + th - 1) / th;
    for (int x = 0; x < tw; x++) {
      const int x0 = x * w / tw, x1 = ((x + 1) * w + tw - 1) / tw;
      int ink = 0, n = 0;
      for (int sy = y0; sy < y1 && sy < h; sy++)
        for (int sx = x0; sx < x1 && sx < w; sx++) { ink += px[sy * w + sx]; n++; }
      if (n && ink * 10 >= n * 4) out.bits[y * stride + (x >> 3)] |= 0x80 >> (x & 7);   // >= 40% ink
    }
  }
}

void fromPacked(const uint8_t* src, int w, int h, int box, Icon& out) {
  std::vector<uint8_t> px(w * h);
  const int stride = (w + 7) / 8;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      px[y * w + x] = (pgm_read_byte(&src[y * stride + (x >> 3)]) & (0x80 >> (x & 7))) ? 1 : 0;
  fit(px, w, h, box, out);
}

static uint32_t u32(const std::vector<uint8_t>& b, size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static uint16_t u16(const std::vector<uint8_t>& b, size_t o) { return b[o] | b[o + 1] << 8; }

// px: one level per pixel, 0 = white .. 3 = black. paletteGrey: the file is a
// 4/8-bit BMP whose palette has light or dark grey entries.
static bool decodeBmp(const String& path, std::vector<uint8_t>& px, int& w, int& h, bool& paletteGrey);

// Grey pictures: nearest-pixel scaling into tw x th (levels kept as they are)
static void fitGrey(const std::vector<uint8_t>& px, int w, int h, int tw, int th, Icon& out) {
  out.w = tw;
  out.h = th;
  out.grey.assign((size_t)tw * th, 0);
  const int stride = (tw + 7) / 8;
  out.bits.assign(stride * th, 0);
  for (int y = 0; y < th; y++)
    for (int x = 0; x < tw; x++) {
      const uint8_t lv = px[(y * h / th) * w + (x * w / tw)];
      out.grey[y * tw + x] = lv;
      if (lv >= 2) out.bits[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    }
}

static void toMono(std::vector<uint8_t>& px) { for (auto& v : px) v = v >= 2 ? 1 : 0; }

static bool hasMidGrey(const std::vector<uint8_t>& px) {
  for (uint8_t v : px) if (v == 1 || v == 2) return true;
  return false;
}

bool loadBmp(const String& path, int box, Icon& out, bool allowGrey) {
  out = Icon();
  std::vector<uint8_t> px;
  int w, h;
  bool pg;
  if (!decodeBmp(path, px, w, h, pg)) return false;
  if (allowGrey && pg && hasMidGrey(px)) {
    const int k = std::min(box / w, box / h);
    int tw = w, th = h;
    if (k >= 2) { tw = w * k; th = h * k; }
    else if (w > box || h > box) {
      if (w >= h) { tw = box; th = std::max(1, (h * box + w / 2) / w); }
      else        { th = box; tw = std::max(1, (w * box + h / 2) / h); }
    }
    fitGrey(px, w, h, tw, th, out);
    return true;
  }
  toMono(px);
  fit(px, w, h, box, out);
  return true;
}

bool loadBmpFit(const String& path, int boxW, int boxH, Icon& out, bool allowGrey) {
  out = Icon();
  std::vector<uint8_t> px;
  int w, h;
  bool pg;
  if (!decodeBmp(path, px, w, h, pg)) return false;
  if (allowGrey && pg && hasMidGrey(px)) {
    int tw = w, th = h;
    if (w > boxW || h > boxH) {
      const float scale = std::min((float)boxW / w, (float)boxH / h);
      tw = std::max(1, (int)(w * scale));
      th = std::max(1, (int)(h * scale));
    }
    fitGrey(px, w, h, tw, th, out);
    return true;
  }
  toMono(px);
  if (w <= boxW && h <= boxH) {                        // fits: exactly as converted
    out.w = w; out.h = h;
    const int stride = (w + 7) / 8;
    out.bits.assign(stride * h, 0);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (px[y * w + x]) out.bits[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    return true;
  }
  // Too big: scale by the limiting side. fit() takes a square box and scales
  // the longer side to it, so that box is the longer side times the scale.
  const float scale = std::min((float)boxW / w, (float)boxH / h);
  fit(px, w, h, std::max(1, (int)(std::max(w, h) * scale)), out);
  return true;
}

// Brightness 0..255 -> level 0 (white) .. 3 (black)
static inline uint8_t levelOf(int lum) {
  int lv = (255 - lum + 42) / 85;
  return (uint8_t)(lv < 0 ? 0 : lv > 3 ? 3 : lv);
}

static bool decodeBmp(const String& path, std::vector<uint8_t>& px, int& w, int& h, bool& paletteGrey) {
  paletteGrey = false;
  auto r = Storage::openReader(path);
  if (!r || r->size() < 54 || r->size() > 300 * 1024) return false;
  std::vector<uint8_t> b;
  b.reserve(r->size());
  int c;
  while ((c = r->read()) >= 0) b.push_back((uint8_t)c);

  if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
  const uint32_t dataOff = u32(b, 10);
  const uint32_t dibSize = u32(b, 14);
  w = (int32_t)u32(b, 18);
  h = (int32_t)u32(b, 22);
  const uint16_t bpp     = u16(b, 28);
  const uint32_t comp    = u32(b, 30);
  const bool topDown = h < 0;
  if (topDown) h = -h;
  if (w <= 0 || h <= 0 || w > 512 || h > 512) return false;
  if (!(comp == 0 || (comp == 3 && bpp == 32))) return false;          // no RLE
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return false;

  // Palette (for 1/4/8-bit): each entry's level
  std::vector<uint8_t> palLv;
  if (bpp <= 8) {
    uint32_t colors = u32(b, 46);
    if (!colors) colors = 1u << bpp;
    const size_t palOff = 14 + dibSize;
    for (uint32_t i = 0; i < colors && palOff + i * 4 + 2 < b.size(); i++) {
      const int lum = (b[palOff + i * 4 + 2] * 3 + b[palOff + i * 4 + 1] * 6 + b[palOff + i * 4]) / 10;
      palLv.push_back(levelOf(lum));
      if (bpp >= 4 && (palLv.back() == 1 || palLv.back() == 2)) paletteGrey = true;
    }
  }

  const size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
  if (dataOff + stride * h > b.size()) return false;

  // 32-bit files: only honour alpha if the image really uses it (many
  // exporters write alpha = 0 everywhere, which would hide the whole icon)
  bool useAlpha = false;
  if (bpp == 32)
    for (int y = 0; y < h && !useAlpha; y++)
      for (int x = 0; x < w; x++)
        if (b[dataOff + stride * y + x * 4 + 3] != 0) { useAlpha = true; break; }
  px.assign(w * h, 0);
  for (int y = 0; y < h; y++) {
    const uint8_t* row = &b[dataOff + stride * (topDown ? y : h - 1 - y)];
    for (int x = 0; x < w; x++) {
      uint8_t lv;
      if (bpp == 1 || bpp == 4 || bpp == 8) {
        int idx;
        if (bpp == 1)      idx = (row[x >> 3] >> (7 - (x & 7))) & 1;
        else if (bpp == 4) idx = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xF;
        else               idx = row[x];
        lv = idx < (int)palLv.size() ? palLv[idx] : 0;
      } else {
        const uint8_t* p = row + x * (bpp / 8);
        const bool transparent = useAlpha && p[3] < 128;
        lv = transparent ? 0 : ((p[2] * 3 + p[1] * 6 + p[0]) / 10 < 128 ? 3 : 0);
      }
      px[y * w + x] = lv;
    }
  }
  return true;
}

void draw(const Icon& icon, int x, int y) {
  if (!icon.grey.empty()) {                          // grey picture: every pixel, with its shade
    for (int j = 0; j < icon.h; j++)
      for (int i = 0; i < icon.w; i++) screen.setGrey(x + i, y + j, icon.grey[j * icon.w + i]);
    return;
  }
  const int stride = (icon.w + 7) / 8;
  for (int j = 0; j < icon.h; j++)
    for (int i = 0; i < icon.w; i++)
      if (icon.bits[j * stride + (i >> 3)] & (0x80 >> (i & 7))) screen.g().drawPixel(x + i, y + j, INK);
}

}
