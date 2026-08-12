#include "ui/focus.h"

#include <citro2d.h>

void fcFocusReset(FcFocus *f)
{
	f->index    = 0;
	f->activate = false;
}

static int rowStart(const FcFocus *f, int i)
{
	const uint8_t r = f->rows[i];
	while (i > 0 && f->rows[i - 1] == r)
		i--;
	return i;
}

static int stepInRow(const FcFocus *f, int i, int dir)
{
	const uint8_t r = f->rows[i];

	const int j = i + dir;
	if (j >= 0 && j < f->count && f->rows[j] == r)
		return j;

	int k = i;
	while (k - dir >= 0 && k - dir < f->count && f->rows[k - dir] == r)
		k -= dir;
	return k;
}

static int stepRow(const FcFocus *f, int i, int dir)
{
	const uint8_t r = f->rows[i];

	int j = i;
	while (j >= 0 && j < f->count && f->rows[j] == r)
		j += dir;

	if (j < 0)
		j = f->count - 1;
	else if (j >= f->count)
		j = 0;

	return rowStart(f, j);
}

void fcFocusBegin(FcFocus *f, bool buttonMode, u32 keysDown)
{
	const bool woke = buttonMode && !f->buttonMode;

	f->buttonMode = buttonMode;
	f->cursor     = 0;
	f->rowCursor  = 0;
	f->seen       = false;
	f->activate   = false;

	if (f->count > 0 && (f->index < 0 || f->index >= f->count))
		f->index = 0;

	if (!buttonMode || woke)
		return;

	f->activate = (keysDown & KEY_A) != 0;

	if (f->count <= 0)
		return;

	if (keysDown & KEY_DOWN)
		f->index = stepRow(f, f->index, +1);
	else if (keysDown & KEY_UP)
		f->index = stepRow(f, f->index, -1);
	else if (keysDown & KEY_RIGHT)
		f->index = stepInRow(f, f->index, +1);
	else if (keysDown & KEY_LEFT)
		f->index = stepInRow(f, f->index, -1);
}

bool fcFocusItemInRow(FcFocus *f, int row, float y, float h,
                        bool *outFocused)
{
	const int i = f->cursor++;

	if (row < 0)        row = 0;
	else if (row > 255) row = 255;
	f->rowCursor = row + 1;

	if (i < FC_FOCUS_MAX)
		f->rows[i] = (uint8_t)row;

	const bool focused = f->buttonMode && i == f->index && i < FC_FOCUS_MAX;
	if (outFocused)
		*outFocused = focused;

	if (focused) {
		f->y    = y;
		f->h    = h;
		f->seen = true;
	}

	return focused && f->activate;
}

bool fcFocusItem(FcFocus *f, float y, float h, bool *outFocused)
{
	return fcFocusItemInRow(f, f->rowCursor, y, h, outFocused);
}

void fcFocusEnd(FcFocus *f)
{
	f->count = f->cursor > FC_FOCUS_MAX ? FC_FOCUS_MAX : f->cursor;
}

void fcFocusCancel(FcFocus *f)
{
	f->activate = false;
}

void fcFocusRing(const FcDraw *d, float x, float y, float w, float h)
{
	const u32 c = d->theme->pal.accent;

	C2D_DrawRectSolid(x, y, 0.0f, w, 2.0f, c);
	C2D_DrawRectSolid(x, y + h - 2.0f, 0.0f, w, 2.0f, c);
	C2D_DrawRectSolid(x, y, 0.0f, 2.0f, h, c);
	C2D_DrawRectSolid(x + w - 2.0f, y, 0.0f, 2.0f, h, c);
}

void fcFocusScrollIntoView(const FcFocus *f, FcScroll *s,
                             float viewTop, float viewHeight)
{
	if (!f->buttonMode || !f->seen)
		return;

	const float above = f->y - viewTop;
	const float below = (f->y + f->h) - (viewTop + viewHeight);

	if (above < 0.0f) {
		s->offset += above;
		s->velocity = 0.0f;
	} else if (below > 0.0f) {
		s->offset += below;
		s->velocity = 0.0f;
	}

	const float maxOff = fcScrollMax(s);
	if (s->offset < 0.0f)   s->offset = 0.0f;
	if (s->offset > maxOff) s->offset = maxOff;
}
