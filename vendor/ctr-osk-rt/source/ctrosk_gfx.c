#include "ctrosk_gfx.h"

#include <math.h>
#include <string.h>

/* Everything is composed here and copied to the screen in one go by
   oskFbPresent(). The keyboard runs with double buffering off -- it repaints
   only when something changes, and a second buffer would leave half those
   repaints on the page nobody is looking at -- so drawing straight into the
   live framebuffer would show the clear-then-rebuild as a flash on every key
   press. Compositing off-screen costs 225K and makes the update atomic. */
static u8 g_back[OSK_SCREEN_W * OSK_SCREEN_H * 3];
static u8 *g_fb = g_back;

void oskFbBind(void) {
  g_fb = g_back;
}

void oskFbPresent(void) {
  u8 *screen = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
  if (screen) memcpy(screen, g_back, sizeof(g_back));
}

void oskPixel(int x, int y, u8 r, u8 g, u8 b) {
  if (x < 0 || x >= OSK_SCREEN_W || y < 0 || y >= OSK_SCREEN_H) return;
  u32 off = (x * OSK_SCREEN_H + (OSK_SCREEN_H - 1 - y)) * 3;
  g_fb[off] = b; g_fb[off + 1] = g; g_fb[off + 2] = r;
}

void oskBlend(int x, int y, OskRGB c, int a) {
  if (a <= 0) return;
  if (a >= 255) { oskPixel(x, y, c.r, c.g, c.b); return; }
  if (x < 0 || x >= OSK_SCREEN_W || y < 0 || y >= OSK_SCREEN_H) return;

  u32 off = (x * OSK_SCREEN_H + (OSK_SCREEN_H - 1 - y)) * 3;
  int ia = 255 - a;
  g_fb[off]     = (u8)((c.b * a + g_fb[off]     * ia) / 255);
  g_fb[off + 1] = (u8)((c.g * a + g_fb[off + 1] * ia) / 255);
  g_fb[off + 2] = (u8)((c.r * a + g_fb[off + 2] * ia) / 255);
}

void oskFill(int x0, int y0, int w, int h, u8 r, u8 g, u8 b) {
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++)
      oskPixel(x, y, r, g, b);
}

void oskClear(u8 r, u8 g, u8 b) {
  oskFill(0, 0, OSK_SCREEN_W, OSK_SCREEN_H, r, g, b);
}

void oskHLine(int x, int y, int w, u8 r, u8 g, u8 b) {
  oskFill(x, y, w, 1, r, g, b);
}

/* ------------------------------------------------------- rounded rectangles */

/* Signed distance from (px, py) to a rounded box: negative inside, zero on
   the edge. Coverage falls out of it, which is what makes the corners smooth
   instead of stepped. */
static float rbox_sdf(float px, float py,
                      float cx, float cy, float hw, float hh, float rad) {
  float qx = fabsf(px - cx) - (hw - rad);
  float qy = fabsf(py - cy) - (hh - rad);
  float ax = qx > 0.0f ? qx : 0.0f;
  float ay = qy > 0.0f ? qy : 0.0f;
  float m  = qx > qy ? qx : qy;
  if (m > 0.0f) m = 0.0f;
  return sqrtf(ax * ax + ay * ay) + m - rad;
}

static int clamp255(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return (int)(v + 0.5f);
}

/* The shared inner loop: walk the pixels the shape can touch and hand each
   one's coverage to `emit`. */
#define RBOX_SCAN(X, Y, W, H, PAD, RAD, BODY)                                  \
  do {                                                                         \
    float cx = (X) + (W) * 0.5f, cy = (Y) + (H) * 0.5f;                        \
    float hw = (W) * 0.5f, hh = (H) * 0.5f;                                    \
    float rad = (RAD);                                                         \
    if (rad > hw) rad = hw;                                                    \
    if (rad > hh) rad = hh;                                                    \
    int px0 = (X) - (PAD), px1 = (X) + (W) + (PAD);                            \
    int py0 = (Y) - (PAD), py1 = (Y) + (H) + (PAD);                            \
    if (px0 < 0) px0 = 0;                                                      \
    if (py0 < 0) py0 = 0;                                                      \
    if (px1 > OSK_SCREEN_W) px1 = OSK_SCREEN_W;                                \
    if (py1 > OSK_SCREEN_H) py1 = OSK_SCREEN_H;                                \
    for (int py = py0; py < py1; py++)                                         \
      for (int pxi = px0; pxi < px1; pxi++) {                                  \
        float d = rbox_sdf(pxi + 0.5f, py + 0.5f, cx, cy, hw, hh, rad);        \
        BODY                                                                   \
      }                                                                        \
  } while (0)

void oskRoundRect(int x, int y, int w, int h, float radius, OskRGB c) {
  oskRoundRectA(x, y, w, h, radius, c, 255);
}

void oskRoundRectA(int x, int y, int w, int h, float radius,
                   OskRGB c, int alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  RBOX_SCAN(x, y, w, h, 1, radius, {
    int a = clamp255((0.5f - d) * 255.0f);
    if (a) oskBlend(pxi, py, c, a * alpha / 255);
  });
}

/* ------------------------------------------------------------------ text -- */

static const OskGlyph *find_glyph(const OskFont *f, int cp) {
  /* ASCII is the dense head of the table, so it lands in one step; the
     handful of icons after it are worth a short scan. */
  if (cp >= 32 && cp <= 126) {
    const OskGlyph *g = &f->glyphs[cp - 32];
    if (g->cp == cp) return g;
  }
  for (int i = 0; i < f->count; i++)
    if (f->glyphs[i].cp == cp) return &f->glyphs[i];
  return NULL;
}

static void draw_glyph(const OskFont *f, const OskGlyph *g,
                       int x, int baseline, OskRGB c, int alpha) {
  const u8 *src = f->pix + g->off;
  for (int gy = 0; gy < g->h; gy++)
    for (int gx = 0; gx < g->w; gx++) {
      int cov = src[gy * g->w + gx];
      if (cov) oskBlend(x + g->xoff + gx, baseline + g->yoff + gy,
                        c, cov * alpha / 255);
    }
}

int oskTextWidth(const OskFont *f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    const OskGlyph *g = find_glyph(f, (unsigned char)*s);
    if (g) w += g->adv;
  }
  return w;
}

void oskTextAt(const OskFont *f, int x, int baseline, const char *s,
               OskRGB c, int alpha) {
  for (; *s; s++) {
    const OskGlyph *g = find_glyph(f, (unsigned char)*s);
    if (!g) continue;
    if (g->w) draw_glyph(f, g, x, baseline, c, alpha);
    x += g->adv;
  }
}

/* One baseline for the whole face, placed so that the band between the
   x-height and the cap sits in the middle of the box.

   Centring each label on its own ink instead -- which is the obvious thing to
   do -- is what makes a row of keys look hand-placed: `a` has no ascender and
   `d` does, so they end up on baselines 4px apart, and at 36px tall that is
   plainly visible. Every label in a row has to share one baseline, exactly as
   it would on a real keyboard. */
static int label_baseline(const OskFont *f, int y, int h) {
  int band = ((int)f->cap + (int)f->xh + 1) / 2;
  return y + (h + band) / 2;
}

static bool is_alnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z');
}

/* Letters and digits hang from the shared baseline, because they are a set
   and the eye reads a row of them as a line of type.

   A lone punctuation mark is not part of that line -- it is a picture on a
   cap -- and leaving it on the baseline drops a comma to the floor and floats
   an apostrophe at the ceiling, which is most of the scatter across a symbol
   layer that is nothing but punctuation. Those get centred in the key.

   Three marks then get nudged back down from centre, because their meaning IS
   their height: `_` has to stay clear of `-` (the symbol layer puts them side
   by side), and `.` and `,` have to stay low enough to still read as a full
   stop and a comma. A few pixels of drop does that; dropping them all the way
   back to the baseline, which is the obvious thing to try, leaves them
   visibly sagging below everything else in the row. */
static int mark_drop(char c) {
  if (c == '_') return 3;
  if (c == '.' || c == ',') return 2;
  return 0;
}

void oskTextBox(const OskFont *f, int x, int y, int w, int h, const char *s,
                OskRGB c, int alpha) {
  int baseline = label_baseline(f, y, h);

  /* A one-character label is centred on its ink rather than its advance:
     advances carry side bearings, which are asymmetric enough on `j` and `t`
     to throw a key cap a visible pixel or two off centre. Words keep advance
     centring, which is what makes their spacing look even. */
  if (s[0] && !s[1]) {
    const OskGlyph *g = find_glyph(f, (unsigned char)s[0]);
    if (g && g->w) {
      /* Line the ink's centre of mass up with the middle of the key, not the
         middle of its bounding box: `r` and `k` are box-symmetric but
         mass-heavy on the left, and box centring leaves a whole row of caps
         leaning the same way. */
      int left = (2 * x + w - g->ocx2 + 1) / 2;
      int by = baseline;
      if (!is_alnum(s[0]))
        by = y + (h - g->h) / 2 - g->yoff + mark_drop(s[0]);
      draw_glyph(f, g, left - g->xoff, by, c, alpha);
      return;
    }
  }
  /* Round rather than truncate, or every label whose width is an odd number
     off the key's biases half a pixel to the left -- consistently, which the
     eye picks up as a lean. */
  oskTextAt(f, x + (w - oskTextWidth(f, s) + 1) / 2, baseline, s, c, alpha);
}

void oskTextLeft(const OskFont *f, int x, int y, int h, const char *s,
                 OskRGB c, int alpha) {
  oskTextAt(f, x, label_baseline(f, y, h), s, c, alpha);
}

void oskGlyphBox(const OskFont *f, int cp, int x, int y, int w, int h,
                 OskRGB c, int alpha) {
  const OskGlyph *g = find_glyph(f, cp);
  if (!g || !g->w) return;
  /* Icons are pictures, not letters: centre them in the key rather than
     hanging them from the text baseline. */
  int baseline = y + (h - g->h) / 2 - g->yoff;
  int left = (2 * x + w - g->ocx2 + 1) / 2;
  draw_glyph(f, g, left - g->xoff, baseline, c, alpha);
}
