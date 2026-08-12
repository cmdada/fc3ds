#pragma once

#include "ui/stereo.h"
#include "ui/theme.h"

#include <citro2d.h>
#include <stdbool.h>

typedef struct {
	const FcTheme *theme;
	C2D_TextBuf      textBuf;
	FcEye          eye;
	float            screenW;
	float            screenH;
} FcDraw;

void fcClipBegin(const FcDraw *d, float x, float y, float w, float h);
void fcClipEnd(void);

typedef enum {
	FC_ALIGN_LEFT   = C2D_AlignLeft,
	FC_ALIGN_CENTER = C2D_AlignCenter,
	FC_ALIGN_RIGHT  = C2D_AlignRight,
} FcAlign;

void fcText(const FcDraw *d, const char *str, float x, float y, float depth,
              float scale, u32 color, FcAlign align);

void fcTextInFont(const FcDraw *d, C2D_Font font, const char *str,
                    float x, float y, float depth, float scale, u32 color,
                    FcAlign align);

float fcTextWidthInFont(const FcDraw *d, C2D_Font font, const char *str,
                          float scale);

void fcTextEllipsized(const FcDraw *d, const char *str, float x, float y,
                        float depth, float scale, u32 color, float maxWidth);

float fcTextWidth(const FcDraw *d, const char *str, float scale);

void fcDisc(float cx, float cy, float radius, u32 color);

void fcDiscSplit(float cx, float cy, float radius, const u32 *colors, int count);

void fcBarSplit(float x, float y, float w, float h, const u32 *colors, int count);

void fcRing(float cx, float cy, float radius, float thickness, u32 color);

void fcRoundedRect(float x, float y, float w, float h, float radius,
                     float depth, u32 color, const FcEye *eye);

void fcCard(const FcDraw *d, float x, float y, float w, float h,
              float depth, const FcEye *eye);

void fcCardWithStripe(const FcDraw *d, float x, float y, float w, float h,
                        float depth, const u32 *colors, int count,
                        const FcEye *eye);

void fcBackdrop(const FcDraw *d, float w, float h);

typedef enum {
	FC_BTN_NORMAL,
	FC_BTN_ACTIVE,
	FC_BTN_SELECTED,
	FC_BTN_DISABLED,
} FcButtonState;

void fcButton(const FcDraw *d, const char *label, float x, float y,
                float w, float h, FcButtonState state);

void fcToggle(const FcDraw *d, float x, float y, bool on);

void fcColorSwatch(const FcDraw *d, float cx, float cy, float radius,
                     u32 color, bool selected);

void fcChip(const FcDraw *d, const char *label, float x, float y, u32 color);

void fcScrollbar(const FcDraw *d, float x, float y, float h,
                   float offset, float contentHeight, float viewHeight);

void fcSpinner(const FcDraw *d, float cx, float cy, float radius,
                 float phase, u32 color);
