/* ctrosk - a touch on-screen keyboard for Nintendo 3DS homebrew.
 *
 * The keyboard owns the bottom screen and draws straight into its
 * framebuffer. You call ctrOskUpdate() once per frame to get key presses out
 * of it, and ctrOskDraw() to put it on screen. Nothing else is required.
 *
 *   CtrOsk osk;
 *   ctrOskInit(&osk);
 *   while (aptMainLoop()) {
 *     hidScanInput();
 *     CtrOskEvent ev;
 *     if (ctrOskUpdate(&osk, &ev)) handle_byte(ev.byte);
 *     ctrOskDraw(&osk);
 *     gfxFlushBuffers(); gspWaitForVBlank();
 *   }
 *
 * Licensed under the GNU General Public License v3.0. See LICENSE.
 */

#ifndef CTROSK_H
#define CTROSK_H

#include <3ds.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTR_OSK_ROWS 5

/* Keys are measured in half-key units, and every row is the same number of
   them across. That is what buys a wide shift and enter without the rows
   drifting out of alignment: a row is correct exactly when its widths sum to
   CTR_OSK_ROW_UNITS. A plain letter is 2 units. */
#define CTR_OSK_ROW_UNITS 20
#define CTR_OSK_MAX_KEYS  12

/* Semantic key codes. The non-printable ones live in the C0 range so a single
   `char` in a layout table can hold either one of these or plain ASCII. */
enum {
  CTR_OSK_KEY_NONE  = 0,
  CTR_OSK_KEY_SHIFT = 1,   /* toggles lower <-> upper                        */
  CTR_OSK_KEY_SYM   = 2,   /* -> sym1 layer                                  */
  CTR_OSK_KEY_ABC   = 3,   /* -> lower layer                                 */
  CTR_OSK_KEY_SYM2  = 4,   /* -> sym2 layer                                  */
  CTR_OSK_KEY_CTRL  = 5,   /* arms the Ctrl modifier for the next key        */
  CTR_OSK_KEY_TAB   = '\t',
  CTR_OSK_KEY_ESC   = 27,
  CTR_OSK_KEY_ENTER = '\n',
  CTR_OSK_KEY_BKSP  = '\b',
  CTR_OSK_KEY_SPACE = ' '
};

typedef enum {
  CTR_OSK_LAYER_LOWER = 0,
  CTR_OSK_LAYER_UPPER,
  CTR_OSK_LAYER_SYM1,
  CTR_OSK_LAYER_SYM2,
  CTR_OSK_LAYER_COUNT
} CtrOskLayer;

/* ---------------------------------------------------------------- layout -- */

/* One key: what it does, and how wide it is in half-key units.

   A `key` of CTR_OSK_KEY_NONE with a non-zero width is a spacer: nothing is
   drawn, and a touch inside it goes to the nearest real key. That is how the
   home row gets its half-key indent without leaving a dead strip down each
   side of the keyboard. */
typedef struct {
  char key;   /* ASCII, or one of the CTR_OSK_KEY_* codes */
  u8   units; /* 2 for a normal key; 0 terminates the row */
} CtrOskKey;

/* One layer is five rows of up to CTR_OSK_MAX_KEYS keys. A row ends at the
   first entry with `units` of 0, so rows may be different lengths as long as
   each one's widths add up to CTR_OSK_ROW_UNITS. */
typedef struct {
  CtrOskKey keys[CTR_OSK_ROWS][CTR_OSK_MAX_KEYS];
} CtrOskLayerMap;

typedef struct {
  CtrOskLayerMap layer[CTR_OSK_LAYER_COUNT];
} CtrOskLayout;

extern const CtrOskLayout ctrOskLayoutQwerty;

/* Where the key grid sits and how big the keys are. The status bar fills
   everything above `y`, so `y` needs to leave room for it (the default 40px
   fits the title, subtitle and layer badge).

   `unit_w` is the pitch of one half-key unit, gap included, so a row is
   CTR_OSK_ROW_UNITS * unit_w wide and an n-unit key is n * unit_w - gap_x.

   The gaps are split per axis on purpose: keys are wider than they are tall
   by a smaller factor than the rows are apart, so a single gap value leaves
   the rows looking more crowded than the columns. */
typedef struct {
  int x, y;
  int unit_w, key_h;
  int gap_x, gap_y;
} CtrOskGeometry;

extern const CtrOskGeometry ctrOskGeometryDefault;

/* ----------------------------------------------------------------- theme -- */

typedef struct { u8 r, g, b; } CtrOskColor;

/* Faces are flat fills. There are no bevels, gradients or drop shadows: at
   this pixel size they only ever read as blur, and a phone keyboard has not
   looked like that in fifteen years. Shape and contrast do the work. */
typedef struct {
  /* Surfaces. */
  CtrOskColor bg;          /* behind the keys                               */
  CtrOskColor status_bar;  /* the strip above them                          */

  /* Key faces. There is no separate pressed colour: a held key is its own
     face lightened, so both tiers move by the same amount and a pressed
     function key never briefly outshines an idle letter. */
  CtrOskColor key;         /* letters and digits                            */
  CtrOskColor mod;         /* the keys that are not letters, recessed       */
  CtrOskColor accent;      /* enter: a solid fill, and the brightest thing   */
  CtrOskColor accent_dim;  /* latched shift: the same hue as a quiet tint    */
  CtrOskColor warn, warn_dim;  /* CTL once armed, same tint/content pairing  */

  /* Text. */
  CtrOskColor text;        /* key caps and function key labels alike        */
  CtrOskColor text_dim;    /* status bar subtitle                           */
  CtrOskColor text_on;     /* over an accent or warn face                   */
  CtrOskColor title;       /* status bar title                              */
} CtrOskTheme;

extern const CtrOskTheme ctrOskThemeDark;

/* ----------------------------------------------------------------- state -- */

typedef struct CtrOsk CtrOsk;

/* What one key press produced. */
typedef struct {
  int  byte;   /* the byte to feed your app, or -1 for modifier-only presses */
  int  key;    /* CTR_OSK_KEY_* or a printable ASCII code                    */
  bool ctrl;   /* the Ctrl modifier was applied to this press                */
} CtrOskEvent;

struct CtrOsk {
  /* --- configuration: set these after ctrOskInit(), before drawing ------- */
  const CtrOskLayout *layout;
  const CtrOskTheme  *theme;
  CtrOskGeometry      geom;

  const char *title;     /* drawn top-left, may be NULL                      */
  const char *subtitle;  /* drawn next to the title, dimmed, may be NULL     */

  /* Called instead of the built-in title/subtitle/badge if non-NULL, with the
     status bar already cleared. Use it to put your own state up there. */
  void (*draw_status)(CtrOsk *osk, void *user);
  void *user;

  /* Backspace is ambiguous: terminals want DEL (127), text fields want BS
     (8). True (the default) emits 127. `key` is CTR_OSK_KEY_BKSP either way. */
  bool backspace_as_del;

  /* Whether a press on the upper layer falls back to lower afterwards, the
     way a phone keyboard does. Default true. */
  bool shift_is_oneshot;

  /* --- internal --------------------------------------------------------- */
  int  layer;
  bool ctrl;
  int  press_r, press_k;   /* row and index within it, -1 for nothing held */
  bool touch_held;
  bool dirty;
};

/* Fills `osk` with defaults: QWERTY layout, dark theme, no title. Also turns
   off double buffering on the bottom screen: the keyboard draws in place and
   never swaps, so with two buffers half its redraws would land on the one
   that isn't being scanned out. */
void ctrOskInit(CtrOsk *osk);

/* Call once per frame, after hidScanInput(). Returns true and fills `ev` when
   a key press produced something to consume; `ev` may be NULL if you only
   care about the keyboard drawing itself. Modifier presses return false. */
bool ctrOskUpdate(CtrOsk *osk, CtrOskEvent *ev);

/* Redraws the bottom screen, but only if something actually changed. Cheap to
   call every frame. */
void ctrOskDraw(CtrOsk *osk);

/* Force a full redraw on the next ctrOskDraw(). Needed after you scribble on
   the bottom screen yourself, or after changing the theme or layout. */
void ctrOskInvalidate(CtrOsk *osk);

/* Switch layers and disarm Ctrl, as if the user had pressed the toggles. */
void ctrOskSetLayer(CtrOsk *osk, CtrOskLayer layer);
void ctrOskSetCtrl(CtrOsk *osk, bool armed);

static inline int  ctrOskLayerOf(const CtrOsk *osk)  { return osk->layer; }
static inline bool ctrOskCtrlArmed(const CtrOsk *osk) { return osk->ctrl; }

/* True if (px, py) from a touchPosition falls inside the key grid. Use it to
   decide whether a touch belongs to the keyboard or to your own bottom-screen
   UI drawn alongside it. */
bool ctrOskHitTest(const CtrOsk *osk, int px, int py);

/* "SHF", "TAB", "ENT" ... for the keys that are drawn as words, NULL for the
   ones drawn as their own character. */
const char *ctrOskKeyLabel(int key);

/* True for the layer-switching and Ctrl keys, which never emit a byte. */
bool ctrOskKeyIsModifier(int key);

#ifdef __cplusplus
}
#endif

#endif /* CTROSK_H */
