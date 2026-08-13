/* Anti-aliased bitmap faces, baked by tools/genfont.py. */

#ifndef CTROSK_FONT_H
#define CTROSK_FONT_H

#include <3ds.h>

/* Placement is relative to the pen position on the baseline, so a caller can
   lay out text without knowing which face it is drawing with. */
typedef struct {
  u16 cp;         /* codepoint                                              */
  u8  w, h;       /* coverage map size, 0x0 for blanks like space           */
  s8  xoff, yoff; /* top-left of the map relative to the pen, yoff is up     */
  u8  adv;        /* how far the pen moves afterwards                       */
  u8  ocx2;       /* twice the ink's centre of mass, measured from xoff      */
  u16 off;        /* start of this glyph inside `pix`                       */
} OskGlyph;

typedef struct {
  const OskGlyph *glyphs;
  int             count;
  const u8       *pix;
  u8              cap;     /* height of `H` above the baseline              */
  u8              xh;      /* height of `x` above the baseline              */
  u8              line_h;
} OskFont;

/* Huge is the key preview bubble and nothing else -- it has to read as a
   magnification of the cap, so it cannot be the same size as one. */
extern const OskFont oskFontHuge;
extern const OskFont oskFontBig;    /* key caps, and the only face with icons */
extern const OskFont oskFontMid;    /* words on the function keys             */
extern const OskFont oskFontSmall;  /* status bar                             */

/* The picture keys, drawn by tools/genfont.py rather than borrowed from a
   symbol font, and carried only by the large face. Private-use codepoints so
   they cannot collide with the ASCII the same atlas holds. */
#define OSK_ICON_SHIFT 0xE000
#define OSK_ICON_BKSP  0xE001
#define OSK_ICON_ENTER 0xE002
#define OSK_ICON_TAB   0xE003

#endif /* CTROSK_FONT_H */
