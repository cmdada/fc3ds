#pragma once

#include "ui/touch.h"
#include "ui/widgets.h"

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

#define FC_FOCUS_MAX 96

typedef struct {
	int  index;
	int  count;
	int  cursor;
	int  rowCursor;
	bool buttonMode;
	bool activate;
	bool seen;

	float y, h;

	uint8_t rows[FC_FOCUS_MAX];
} FcFocus;

void fcFocusReset(FcFocus *f);

void fcFocusBegin(FcFocus *f, bool buttonMode, u32 keysDown);

bool fcFocusItem(FcFocus *f, float y, float h, bool *outFocused);

bool fcFocusItemInRow(FcFocus *f, int row, float y, float h,
                        bool *outFocused);

void fcFocusEnd(FcFocus *f);

void fcFocusCancel(FcFocus *f);

void fcFocusRing(const FcDraw *d, float x, float y, float w, float h);

void fcFocusScrollIntoView(const FcFocus *f, FcScroll *s,
                             float viewTop, float viewHeight);
