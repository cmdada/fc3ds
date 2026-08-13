#include "ctrosk.h"
#include "ctrosk_gfx.h"

#include <string.h>

/* --------------------------------------------------------------- defaults - */

#define K_SHF CTR_OSK_KEY_SHIFT
#define K_SYM CTR_OSK_KEY_SYM
#define K_ABC CTR_OSK_KEY_ABC
#define K_SY2 CTR_OSK_KEY_SYM2
#define K_CTL CTR_OSK_KEY_CTRL
#define K_TAB CTR_OSK_KEY_TAB
#define K_ESC CTR_OSK_KEY_ESC
#define K_ENT CTR_OSK_KEY_ENTER
#define K_BSP CTR_OSK_KEY_BKSP
#define K_SPC CTR_OSK_KEY_SPACE

/* A plain key, one with an explicit width in half-key units, and a spacer. */
#define K(C)      {(C), 2}
#define KW(C, N)  {(C), (N)}
#define KGAP(N)   {CTR_OSK_KEY_NONE, (N)}

/* The letter layers are laid out the way a phone keyboard is: ten across the
   top, nine on the home row inset by half a key on each side, then seven
   between a wide shift and a wide backspace, and a bottom row of layer
   toggle, comma, space, full stop and enter. The half-key stagger on the home
   row is doing more work than it looks like -- a keyboard whose rows all
   start flush is the single clearest tell that it was drawn on a grid.

   The 3DS bottom screen is landscape, which is the shape that gets a number
   row on a real phone keyboard too, so that stays.

   What does not fit is this library's reason for existing: Tab, Esc and Ctrl.
   Tab and Ctrl keep slots on the letter layers, where a phone would put the
   comma and the language switcher -- neither tab-completion nor Ctrl-C can be
   two taps away in a terminal. The comma takes Tab's old place on the symbol
   layers, and Esc stays there.

   Each row sums to CTR_OSK_ROW_UNITS. */
const CtrOskLayout ctrOskLayoutQwerty = {{
  [CTR_OSK_LAYER_LOWER] = {{
    {K('1'),K('2'),K('3'),K('4'),K('5'),K('6'),K('7'),K('8'),K('9'),K('0')},
    {K('q'),K('w'),K('e'),K('r'),K('t'),K('y'),K('u'),K('i'),K('o'),K('p')},
    {KGAP(1),K('a'),K('s'),K('d'),K('f'),K('g'),K('h'),K('j'),K('k'),K('l'),
     KGAP(1)},
    {KW(K_SHF,3),K('z'),K('x'),K('c'),K('v'),K('b'),K('n'),K('m'),KW(K_BSP,3)},
    {KW(K_SYM,3),K(K_TAB),K(K_CTL),KW(K_SPC,8),K('.'),KW(K_ENT,3)},
  }},
  [CTR_OSK_LAYER_UPPER] = {{
    {K('!'),K('@'),K('#'),K('$'),K('%'),K('^'),K('&'),K('*'),K('('),K(')')},
    {K('Q'),K('W'),K('E'),K('R'),K('T'),K('Y'),K('U'),K('I'),K('O'),K('P')},
    {KGAP(1),K('A'),K('S'),K('D'),K('F'),K('G'),K('H'),K('J'),K('K'),K('L'),
     KGAP(1)},
    {KW(K_SHF,3),K('Z'),K('X'),K('C'),K('V'),K('B'),K('N'),K('M'),KW(K_BSP,3)},
    {KW(K_SYM,3),K(K_TAB),K(K_CTL),KW(K_SPC,8),K(':'),KW(K_ENT,3)},
  }},
  /* The symbol layers keep the flush ten-across grid -- they are a character
     picker, not a typing surface, and the stagger would only cost a key. */
  [CTR_OSK_LAYER_SYM1] = {{
    {K('1'),K('2'),K('3'),K('4'),K('5'),K('6'),K('7'),K('8'),K('9'),K('0')},
    {K('+'),K('-'),K('*'),K('/'),K('='),K('|'),K('\\'),K('`'),K('~'),K('_')},
    {K('@'),K('#'),K('$'),K('%'),K('^'),K('&'),K('('),K(')'),K('['),K(']')},
    {KW(K_SY2,3),K('{'),K('}'),K('<'),K('>'),K('?'),K('\''),K(':'),KW(K_BSP,3)},
    {KW(K_ABC,3),K(','),K(K_ESC),K(';'),KW(K_SPC,4),K('"'),K(K_CTL),
     KW(K_ENT,3)},
  }},
  [CTR_OSK_LAYER_SYM2] = {{
    {K('`'),K('~'),K('|'),K('\\'),K('_'),K('-'),K('+'),K('='),K('.'),K(',')},
    {K('['),K(']'),K('{'),K('}'),K('<'),K('>'),K('('),K(')'),K('/'),K('?')},
    {K('!'),K('@'),K('#'),K('$'),K('%'),K('^'),K('&'),K('*'),K('\''),K('"')},
    /* Paging back to the first symbol page, mirroring the `#+=` that got you
       here -- an `abc` in both this row and the one below it wasted a key. */
    {KW(K_SYM,3),K(':'),K(';'),K('-'),K('_'),K('+'),K('='),K('\\'),KW(K_BSP,3)},
    {KW(K_ABC,3),K(','),K(K_ESC),K('|'),KW(K_SPC,4),K('~'),K(K_CTL),
     KW(K_ENT,3)},
  }},
}};

/* 20 units of 16px is 320, so a row starting at x=2 with a 4px horizontal gap
   ends 2px from the right edge: even margins on both sides, and a normal
   2-unit key comes out 28px wide. Five 34px rows 6px apart end 6px clear of
   the bottom, which is one vertical gap. Even margins matter more than they
   sound like they should -- an odd pixel on one side is exactly what makes a
   grid of keys look hand-placed. */
const CtrOskGeometry ctrOskGeometryDefault = {
  .x = 2, .y = 40, .unit_w = 16, .key_h = 34, .gap_x = 4, .gap_y = 6
};

#define RGB(R, G, B) {(R), (G), (B)}

/* Three tonal tiers, evenly spaced: background, function keys, letter keys.
   Spacing them evenly matters on the real panel, which is dim and sits under
   a resistive overlay -- tiers that look distinct on a monitor collapse into
   each other on hardware. */
const CtrOskTheme ctrOskThemeDark = {
  .bg         = RGB(26, 27, 31),
  .status_bar = RGB(26, 27, 31),

  .key        = RGB(60, 64, 71),
  .mod        = RGB(43, 46, 52),
  /* Material's dark-surface blue, not the light-theme one: the vivid blue
     buzzes against a near-black background. */
  .accent     = RGB(138, 180, 248),
  /* The same blue as a tinted container instead of a fill. Enter earns a
     solid accent -- that is what a phone keyboard does -- but a latched shift
     lit up just as brightly gives you two glowing slabs competing for the
     same glance, so it gets the quiet version. */
  .accent_dim = RGB(28, 46, 78),
  .warn       = RGB(242, 179, 61),
  .warn_dim   = RGB(74, 54, 16),

  .text       = RGB(232, 234, 237),
  .text_dim   = RGB(126, 133, 146),
  .text_on    = RGB(20, 22, 28),
  .title      = RGB(232, 234, 237),
};

/* ---------------------------------------------------------------- helpers - */

/* The theme speaks in CtrOskColor and the renderer in OskRGB; they are the
   same three bytes, but the renderer is not part of the public API. */
static inline OskRGB rgb(CtrOskColor c) { return oskRGB(c.r, c.g, c.b); }

/* Generous rounding relative to a 28x34 key -- the single strongest cue that
   this is a current keyboard rather than a 2005 one. */
#define KEY_RADIUS 7.0f

/* Press feedback. A held key moves away from wherever it started rather than
   towards a fixed colour: adding a constant to the dark function keys pushed
   them straight past the letter keys, so holding shift briefly promoted it to
   a brighter tier than the letters beside it. Dark faces lighten and bright
   ones darken, by amounts picked so that both moves are equally visible and
   neither crosses into another tier. */
#define PRESS_LIFT 20   /* dark faces, up   */
#define PRESS_SINK 40   /* bright faces, down */
#define PRESS_PIVOT 110 /* the luma either side of which the move flips */

/* Proportional, into the headroom that is left. A flat addition clips one
   channel before the others on an already-bright colour, which drags the
   accent blue and the ctrl amber towards white as they are pressed. */
static CtrOskColor lighten(CtrOskColor c, int n) {
  CtrOskColor o = {(u8)(c.r + (255 - c.r) * n / 255),
                   (u8)(c.g + (255 - c.g) * n / 255),
                   (u8)(c.b + (255 - c.b) * n / 255)};
  return o;
}

static CtrOskColor darken(CtrOskColor c, int n) {
  CtrOskColor o = {(u8)(c.r - c.r * n / 255),
                   (u8)(c.g - c.g * n / 255),
                   (u8)(c.b - c.b * n / 255)};
  return o;
}

static int luma(CtrOskColor c) {
  return (54 * c.r + 183 * c.g + 19 * c.b) >> 8;
}

/* How a key looks while it is held. */
static CtrOskColor pressed(CtrOskColor c) {
  return luma(c) > PRESS_PIVOT ? darken(c, PRESS_SINK)
                               : lighten(c, PRESS_LIFT);
}

bool ctrOskKeyIsModifier(int key) {
  unsigned char u = (unsigned char)key;
  return u == CTR_OSK_KEY_SHIFT || u == CTR_OSK_KEY_SYM ||
         u == CTR_OSK_KEY_ABC   || u == CTR_OSK_KEY_SYM2 ||
         u == CTR_OSK_KEY_CTRL;
}

const char *ctrOskKeyLabel(int key) {
  switch ((unsigned char)key) {
    case CTR_OSK_KEY_SHIFT: return "SHF";
    case CTR_OSK_KEY_SYM:   return "?#1";
    case CTR_OSK_KEY_ABC:   return "ABC";
    case CTR_OSK_KEY_SYM2:  return "#+=";
    case CTR_OSK_KEY_CTRL:  return "CTL";
    case CTR_OSK_KEY_TAB:   return "TAB";
    case CTR_OSK_KEY_ESC:   return "ESC";
    case CTR_OSK_KEY_ENTER: return "ENT";
    case CTR_OSK_KEY_BKSP:  return "DEL";
    default: return NULL;
  }
}

static const CtrOskLayerMap *active_map(const CtrOsk *osk) {
  int l = osk->layer;
  if (l < 0 || l >= CTR_OSK_LAYER_COUNT) l = CTR_OSK_LAYER_LOWER;
  return &osk->layout->layer[l];
}

/* Screen position of a key `units` wide sitting at half-unit offset `u`. */
static int unit_x(const CtrOsk *osk, int u) {
  return osk->geom.x + u * osk->geom.unit_w;
}
static int unit_w(const CtrOsk *osk, int units) {
  return units * osk->geom.unit_w - osk->geom.gap_x;
}
static int key_y(const CtrOsk *osk, int r) {
  return osk->geom.y + r * (osk->geom.key_h + osk->geom.gap_y);
}

static int row_len(const CtrOskLayerMap *map, int r) {
  int n = 0;
  while (n < CTR_OSK_MAX_KEYS && map->keys[r][n].units) n++;
  return n;
}

/* Half-unit offset of key `k` within its row. */
static int key_offset(const CtrOskLayerMap *map, int r, int k) {
  int u = 0;
  for (int i = 0; i < k; i++) u += map->keys[r][i].units;
  return u;
}

static bool is_spacer(const CtrOskLayerMap *map, int r, int k) {
  return map->keys[r][k].key == CTR_OSK_KEY_NONE;
}

/* Which key in row `r` covers half-unit `u`, or the last one if `u` runs off
   the end. Rows always sum to CTR_OSK_ROW_UNITS, so that only happens on the
   half-gap of slack past the right edge.

   Landing in a spacer walks outwards to the nearest real key, so the indent
   either side of the home row still types the letter next to it instead of
   swallowing the touch. */
static int key_at_unit(const CtrOskLayerMap *map, int r, int u) {
  int n = row_len(map, r);
  if (n <= 0) return 0;

  int acc = 0, k = n - 1;
  for (int i = 0; i < n; i++) {
    acc += map->keys[r][i].units;
    if (u < acc) { k = i; break; }
  }
  if (!is_spacer(map, r, k)) return k;

  for (int d = 1; d < n; d++) {
    if (k - d >= 0 && !is_spacer(map, r, k - d)) return k - d;
    if (k + d < n  && !is_spacer(map, r, k + d)) return k + d;
  }
  return k;
}

/* ---------------------------------------------------------------- drawing - */

/* What goes on a key face: either a short word or a single picture glyph. */
typedef struct {
  const char *word;
  int         icon;
} KeyFace;

static KeyFace face_word(const char *w) { KeyFace f = {w, 0}; return f; }
static KeyFace face_icon(int cp) { KeyFace f = {NULL, cp}; return f; }

/* The marks that are mostly whitespace: a dot, a tick or a dash whose ink is
   a small fraction of any letter's. */
static bool is_light_mark(char c) {
  return c && strchr(".,'\"`-_", c) != NULL;
}

/* How one key should look: face, lettering, and which font carries it. */
typedef struct {
  KeyFace         face;
  const OskFont  *font;
  CtrOskColor     fill;
  CtrOskColor     text;
} KeyStyle;

/* Icons come off the large face -- it is the only one that carries them, and
   they need the same visual weight as the letters they sit beside. */
static KeyStyle style_icon(const CtrOskTheme *t, int cp) {
  KeyStyle s = {face_icon(cp), &oskFontBig, t->mod, t->text};
  return s;
}

/* Everything the keyboard draws goes through here, so the function row is no
   longer laid out by hand: with widths in the keymap it is just another row.
   `buf` holds the single character of a printable key, which the caller keeps
   alive for the duration of the draw. */
static KeyStyle key_style(const CtrOsk *osk, char key, char *buf) {
  const CtrOskTheme *t = osk->theme;
  KeyStyle s = {face_word(NULL), &oskFontMid, t->mod, t->text};

  switch ((unsigned char)key) {
    case CTR_OSK_KEY_SHIFT:
      s = style_icon(t, OSK_ICON_SHIFT);
      /* Latched shift picks up the accent hue as a tint, the way a phone
         keyboard shows that caps are on -- but quietly, because enter is
         already wearing the solid version of the same colour. */
      if (osk->layer == CTR_OSK_LAYER_UPPER) {
        s.fill = t->accent_dim; s.text = t->accent;
      }
      return s;
    case CTR_OSK_KEY_CTRL:
      s.face = face_word("ctrl");
      if (osk->ctrl) { s.fill = t->warn_dim; s.text = t->warn; }
      return s;
    case CTR_OSK_KEY_ENTER:
      s = style_icon(t, OSK_ICON_ENTER);
      s.fill = t->accent; s.text = t->text_on;
      return s;
    /* The layer toggles say where they will take you, not where you are. */
    case CTR_OSK_KEY_SYM:  s.face = face_word("?#1"); return s;
    case CTR_OSK_KEY_SYM2: s.face = face_word("#+="); return s;
    case CTR_OSK_KEY_ABC:  s.face = face_word("abc"); return s;
    case CTR_OSK_KEY_TAB:  return style_icon(t, OSK_ICON_TAB);
    case CTR_OSK_KEY_BKSP: return style_icon(t, OSK_ICON_BKSP);
    case CTR_OSK_KEY_ESC:  s.face = face_word("esc"); return s;
    case CTR_OSK_KEY_SPACE:
      /* Space types a character, so it belongs to the light tier with the
         comma and full stop either side of it, not to the dark function
         tier. It carries no label: a wide blank key is unmistakable, and the
         word is a dated cue. */
      s.fill = t->key;
      return s;
    default:
      buf[0] = key; buf[1] = 0;
      s.face = face_word(buf);
      /* A full stop carries about a ninth of the ink of the lightest letter,
         so at the cap size it reads as a speck rather than as a character --
         and half of each symbol layer is made of marks like it. Setting just
         those from the next face up puts them back in the same weight range
         as their neighbours, and costs nothing: the larger face is already
         resident for the preview bubble. */
      s.font = is_light_mark(key) ? &oskFontHuge : &oskFontBig;
      s.fill = t->key;
      s.text = t->text;
      return s;
  }
}

static bool key_pressed(const CtrOsk *osk, int r, int k) {
  return osk->press_r == r && osk->press_k == k;
}

/* One key, `units` wide, at half-unit offset `u` in row `r`. Pressing shifts
   the face within its own tier rather than embossing it: the feedback has to
   survive being under a fingertip, and a change of tone is visible around the
   edge of one where a bevel is not. */
static void draw_key(const CtrOsk *osk, int r, int k, int u, int units,
                     KeyStyle s) {
  int x = unit_x(osk, u);
  int y = key_y(osk, r);
  int w = unit_w(osk, units);
  int h = osk->geom.key_h;

  if (key_pressed(osk, r, k)) s.fill = pressed(s.fill);

  oskRoundRect(x, y, w, h, KEY_RADIUS, rgb(s.fill));

  if (s.face.icon)
    oskGlyphBox(s.font, s.face.icon, x, y, w, h, rgb(s.text), 255);
  else if (s.face.word)
    oskTextBox(s.font, x, y, w, h, s.face.word, rgb(s.text), 255);
}

/* The bubble that pops above a held character key.

   This is the one piece of the Gboard vocabulary that is not decoration
   here: the 3DS is driven with a stylus or a thumb, both of which sit on top
   of the key being pressed, so the pressed-face highlight is the one piece of
   feedback the user cannot see. The bubble puts the character somewhere not
   underneath their hand. Modifier and word keys get none, as on a phone --
   only keys that actually deposit a character.

   Deliberately bigger than a key on both axes, lighter, and more rounded, so
   it cannot be mistaken for the neighbouring key having changed its mind. It
   sits on a soft shadow and a one-pixel rim of background: that is what reads
   as floating above the keyboard. The separation must be bought with a
   shadow rather than by clearing a margin around the bubble -- a wide gap
   punched out of the background takes the corners off the keys either side
   with it, and a key sliced down a straight edge looks like a rendering
   fault, not like depth. A short stem to the key it belongs to keeps the
   bubble from floating free. */
#define PREVIEW_W 42
#define PREVIEW_H 44
#define PREVIEW_RADIUS 10.0f
#define PREVIEW_RIM 1
#define PREVIEW_STEM_W 14
#define PREVIEW_STEM_H 3
#define PREVIEW_LIFT 62   /* how much lighter than a key face the bubble is */
#define PREVIEW_SHADOW 110

static void draw_preview(const CtrOsk *osk, const CtrOskLayerMap *map) {
  if (osk->press_r < 0 || osk->press_k < 0) return;

  char key = map->keys[osk->press_r][osk->press_k].key;
  if (key < 33 || key > 126) return;   /* space and the modifiers opt out */

  const CtrOskTheme *t = osk->theme;
  int units = map->keys[osk->press_r][osk->press_k].units;
  int kx = unit_x(osk, key_offset(map, osk->press_r, osk->press_k));
  int kw = unit_w(osk, units);

  int ky = key_y(osk, osk->press_r);
  int w = PREVIEW_W > kw ? PREVIEW_W : kw;
  int x = kx + (kw - w) / 2;
  int y = ky - PREVIEW_H - PREVIEW_STEM_H;

  /* Only the top row has nowhere to put it: there the bubble flips to the
     underside of the key rather than covering the title. Every other row can
     have the couple of pixels of status bar it overlaps. */
  bool below = y < 2;
  if (below) y = ky + osk->geom.key_h + PREVIEW_STEM_H;

  if (x < 1) x = 1;
  if (x + w > OSK_SCREEN_W - 1) x = OSK_SCREEN_W - 1 - w;

  /* Hang the stem off the middle of the bubble, but never past the key it is
     pointing at -- once the bubble has been clamped at the edge of the screen
     those two are no longer the same place, and a stem centred on either one
     alone comes out as a lopsided lollipop. */
  int sx = x + (w - PREVIEW_STEM_W) / 2;
  if (sx < kx) sx = kx;
  if (sx + PREVIEW_STEM_W > kx + kw) sx = kx + kw - PREVIEW_STEM_W;
  int sy = below ? ky + osk->geom.key_h : y + PREVIEW_H;

  CtrOskColor face = lighten(t->key, PREVIEW_LIFT);

  oskRoundRectA(x + 1, y + 3, w, PREVIEW_H, PREVIEW_RADIUS,
                oskRGB(0, 0, 0), PREVIEW_SHADOW);
  oskRoundRect(x - PREVIEW_RIM, y - PREVIEW_RIM,
               w + PREVIEW_RIM * 2, PREVIEW_H + PREVIEW_RIM * 2,
               PREVIEW_RADIUS + PREVIEW_RIM, rgb(t->bg));
  oskFill(sx, sy, PREVIEW_STEM_W, PREVIEW_STEM_H, face.r, face.g, face.b);
  oskRoundRect(x, y, w, PREVIEW_H, PREVIEW_RADIUS, rgb(face));

  char buf[2] = {key, 0};
  oskTextBox(&oskFontHuge, x, y, w, PREVIEW_H, buf, rgb(t->text), 255);
}

static void draw_status_bar(CtrOsk *osk) {
  const CtrOskTheme *t = osk->theme;
  const int bar_h = osk->geom.y;

  oskFill(0, 0, OSK_SCREEN_W, bar_h,
          t->status_bar.r, t->status_bar.g, t->status_bar.b);

  if (osk->draw_status) {
    osk->draw_status(osk, osk->user);
    return;
  }

  /* Title and subtitle, and nothing else. There used to be a pill in the
     corner naming the current layer, but the keys are already showing it
     forty times larger, and a rounded status badge is debug-HUD furniture. An
     app that wants more can install draw_status. Inset to line up with the
     key block below rather than floating at some margin of its own. */
  const int inset = osk->geom.x + 1;
  int x = inset;

  if (osk->title) {
    oskTextLeft(&oskFontSmall, x, 0, bar_h, osk->title, rgb(t->title), 255);
    x += oskTextWidth(&oskFontSmall, osk->title) + 7;
  }
  if (osk->subtitle)
    oskTextLeft(&oskFontSmall, x, 0, bar_h, osk->subtitle,
                rgb(t->text_dim), 255);
}

void ctrOskDraw(CtrOsk *osk) {
  if (!osk->dirty) return;
  osk->dirty = false;

  const CtrOskTheme *t = osk->theme;
  const CtrOskLayerMap *map = active_map(osk);

  oskFbBind();
  oskClear(t->bg.r, t->bg.g, t->bg.b);
  draw_status_bar(osk);

  for (int r = 0; r < CTR_OSK_ROWS; r++) {
    int n = row_len(map, r);
    int u = 0;
    for (int k = 0; k < n; k++) {
      char buf[2];
      if (!is_spacer(map, r, k))
        draw_key(osk, r, k, u, map->keys[r][k].units,
                 key_style(osk, map->keys[r][k].key, buf));
      u += map->keys[r][k].units;
    }
  }

  draw_preview(osk, map);
  oskFbPresent();
  gfxFlushBuffers();
}

void ctrOskInvalidate(CtrOsk *osk) { osk->dirty = true; }

/* ------------------------------------------------------------------ input - */

bool ctrOskHitTest(const CtrOsk *osk, int px, int py) {
  /* The gap around a key is part of its touch target, so the block of keys
     extends half a gap past the grid on each side. Without the left-hand
     slack the default geometry, which starts at x=2, would ignore taps on the
     first couple of pixel columns. The trailing gap is already inside x1. */
  int x0 = osk->geom.x - osk->geom.gap_x;
  int x1 = osk->geom.x + CTR_OSK_ROW_UNITS * osk->geom.unit_w;
  int y0 = osk->geom.y;
  int y1 = y0 + CTR_OSK_ROWS * (osk->geom.key_h + osk->geom.gap_y);
  return px >= x0 && px < x1 && py >= y0 && py < y1;
}

/* Turn a press into an event, updating layer/ctrl state. Returns true when it
   produced a byte. */
static bool apply_key(CtrOsk *osk, char key, CtrOskEvent *ev) {
  unsigned char u = (unsigned char)key;

  switch (u) {
    case CTR_OSK_KEY_SHIFT:
      osk->layer = (osk->layer == CTR_OSK_LAYER_UPPER) ? CTR_OSK_LAYER_LOWER
                                                       : CTR_OSK_LAYER_UPPER;
      return false;
    case CTR_OSK_KEY_SYM:  osk->layer = CTR_OSK_LAYER_SYM1;  return false;
    case CTR_OSK_KEY_SYM2: osk->layer = CTR_OSK_LAYER_SYM2;  return false;
    case CTR_OSK_KEY_ABC:  osk->layer = CTR_OSK_LAYER_LOWER; return false;
    case CTR_OSK_KEY_CTRL: osk->ctrl = !osk->ctrl;           return false;
  }

  ev->key  = u;
  ev->ctrl = false;

  switch (u) {
    case CTR_OSK_KEY_TAB:   ev->byte = '\t'; return true;
    case CTR_OSK_KEY_ESC:   ev->byte = 27;   return true;
    case CTR_OSK_KEY_ENTER: ev->byte = '\n'; return true;
    case CTR_OSK_KEY_SPACE: ev->byte = ' ';  return true;
    case CTR_OSK_KEY_BKSP:
      ev->byte = osk->backspace_as_del ? 127 : '\b';
      return true;
  }

  if (osk->ctrl) {
    /* Ctrl folds a letter down into its C0 control code; anything else passes
       through unchanged so Ctrl never silently eats a key. */
    if (key >= 'a' && key <= 'z')      ev->byte = key - 'a' + 1;
    else if (key >= 'A' && key <= 'Z') ev->byte = key - 'A' + 1;
    else                               ev->byte = (unsigned char)key;
    ev->ctrl = true;
    osk->ctrl = false;
  } else {
    ev->byte = (unsigned char)key;
    if (osk->shift_is_oneshot && osk->layer == CTR_OSK_LAYER_UPPER)
      osk->layer = CTR_OSK_LAYER_LOWER;
  }
  return true;
}

bool ctrOskUpdate(CtrOsk *osk, CtrOskEvent *ev) {
  CtrOskEvent scratch;
  if (!ev) ev = &scratch;
  ev->byte = -1;
  ev->key  = CTR_OSK_KEY_NONE;
  ev->ctrl = false;

  if (!(hidKeysHeld() & KEY_TOUCH)) {
    /* Release: drop the pressed-key highlight if there was one. */
    if (osk->touch_held || osk->press_r != -1) {
      osk->touch_held = false;
      osk->press_r = -1;
      osk->press_k = -1;
      osk->dirty = true;
    }
    return false;
  }

  /* Only the first frame of a touch counts, so holding a key does not repeat.
     The 3DS touchscreen is resistive and single-point; there is no second
     finger to track. */
  if (osk->touch_held) return false;
  osk->touch_held = true;

  touchPosition tp;
  hidTouchRead(&tp);
  if (!ctrOskHitTest(osk, tp.px, tp.py)) return false;

  int r = (tp.py - osk->geom.y) / (osk->geom.key_h + osk->geom.gap_y);
  int u = (tp.px - osk->geom.x) / osk->geom.unit_w;
  if (r < 0) r = 0; else if (r >= CTR_OSK_ROWS) r = CTR_OSK_ROWS - 1;
  if (u < 0) u = 0; else if (u >= CTR_OSK_ROW_UNITS) u = CTR_OSK_ROW_UNITS - 1;

  const CtrOskLayerMap *map = active_map(osk);
  int k = key_at_unit(map, r, u);

  osk->press_r = r;
  osk->press_k = k;
  osk->dirty = true;

  return apply_key(osk, map->keys[r][k].key, ev);
}

/* ---------------------------------------------------------------- lifecycle */

void ctrOskInit(CtrOsk *osk) {
  memset(osk, 0, sizeof(*osk));
  osk->layout   = &ctrOskLayoutQwerty;
  osk->theme    = &ctrOskThemeDark;
  osk->geom     = ctrOskGeometryDefault;
  osk->layer    = CTR_OSK_LAYER_LOWER;
  osk->press_r  = -1;
  osk->press_k  = -1;
  osk->dirty    = true;
  osk->backspace_as_del  = true;
  osk->shift_is_oneshot  = true;

  gfxSetDoubleBuffering(GFX_BOTTOM, false);
}

void ctrOskSetLayer(CtrOsk *osk, CtrOskLayer layer) {
  if ((unsigned)layer >= CTR_OSK_LAYER_COUNT) return;
  osk->layer = layer;
  osk->ctrl = false;
  osk->dirty = true;
}

void ctrOskSetCtrl(CtrOsk *osk, bool armed) {
  osk->ctrl = armed;
  osk->dirty = true;
}
