/* Internal framebuffer primitives for ctrosk. Not part of the public API.
 *
 * The 3DS bottom screen is 320x240 in BGR8, and the framebuffer is rotated 90
 * degrees, so screen pixel (x, y) lives at offset (x * 240 + (239 - y)) * 3.
 * Everything here works in screen coordinates and hides that.
 *
 * There is no GPU involved: every edge, corner and glyph here is rasterised by
 * hand with 8-bit coverage and blended into the framebuffer, which is what
 * keeps the rounded keys and the lettering from looking like stairsteps at
 * this size.
 */

#ifndef CTROSK_GFX_H
#define CTROSK_GFX_H

#include <3ds.h>
#include <stdbool.h>

#include "ctrosk_font.h"

#define OSK_SCREEN_W 320
#define OSK_SCREEN_H 240

typedef struct { u8 r, g, b; } OskRGB;

static inline OskRGB oskRGB(u8 r, u8 g, u8 b) {
  OskRGB c = {r, g, b}; return c;
}

/* Point drawing at the off-screen buffer. Call once at the start of a redraw,
   and oskFbPresent() to push the finished frame to the screen in one copy. */
void oskFbBind(void);
void oskFbPresent(void);

void oskPixel(int x, int y, u8 r, u8 g, u8 b);
void oskFill(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void oskClear(u8 r, u8 g, u8 b);
void oskHLine(int x, int y, int w, u8 r, u8 g, u8 b);

/* Blend one pixel at `a`/255 coverage. */
void oskBlend(int x, int y, OskRGB c, int a);

/* Anti-aliased rounded rectangle, flat fill. */
void oskRoundRect(int x, int y, int w, int h, float radius, OskRGB c);

/* The same at partial opacity, for the shadow under the preview bubble. */
void oskRoundRectA(int x, int y, int w, int h, float radius,
                   OskRGB c, int alpha);

/* ------------------------------------------------------------------ text -- */

/* Advance width of an ASCII string. */
int oskTextWidth(const OskFont *f, const char *s);

/* Draw at a pen position sitting on `baseline`. */
void oskTextAt(const OskFont *f, int x, int baseline, const char *s,
               OskRGB c, int alpha);

/* Draw centred inside the box, both horizontally and on the string's own ink
   rather than its line box, so short lowercase words and full-height capitals
   both end up looking centred. */
void oskTextBox(const OskFont *f, int x, int y, int w, int h, const char *s,
                OskRGB c, int alpha);

/* Centred vertically the same way, but starting at `x` rather than centred
   across a width. */
void oskTextLeft(const OskFont *f, int x, int y, int h, const char *s,
                 OskRGB c, int alpha);

/* The same, for one glyph picked by codepoint: the picture keys are not
   reachable as ASCII. Centred on the glyph's own ink. */
void oskGlyphBox(const OskFont *f, int cp, int x, int y, int w, int h,
                 OskRGB c, int alpha);

#endif /* CTROSK_GFX_H */
