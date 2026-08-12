#include "ui/widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void fcTextInFont(const FcDraw *d, C2D_Font font, const char *str,
                    float x, float y, float depth, float scale, u32 color,
                    FcAlign align)
{
	if (!str || !*str)
		return;

	C2D_Text text;
	C2D_TextFontParse(&text, font, d->textBuf, str);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | (u32)align,
	             x + fcShift(&d->eye, depth), y, 0.5f, scale, scale, color);
}

void fcText(const FcDraw *d, const char *str, float x, float y, float depth,
              float scale, u32 color, FcAlign align)
{
	fcTextInFont(d, d->theme->font, str, x, y, depth, scale, color, align);
}

static C2D_TextBuf fcMeasureBuf(void)
{
	static C2D_TextBuf buf;
	if (!buf)
		buf = C2D_TextBufNew(512);
	C2D_TextBufClear(buf);
	return buf;
}

float fcTextWidthInFont(const FcDraw *d, C2D_Font font, const char *str,
                          float scale)
{
	(void)d;

	if (!str || !*str)
		return 0.0f;

	C2D_Text text;
	C2D_TextFontParse(&text, font, fcMeasureBuf(), str);

	float w = 0.0f, h = 0.0f;
	C2D_TextGetDimensions(&text, scale, scale, &w, &h);
	return w;
}

float fcTextWidth(const FcDraw *d, const char *str, float scale)
{
	return fcTextWidthInFont(d, d->theme->font, str, scale);
}

void fcTextEllipsized(const FcDraw *d, const char *str, float x, float y,
                        float depth, float scale, u32 color, float maxWidth)
{
	if (!str || !*str)
		return;

	if (fcTextWidth(d, str, scale) <= maxWidth) {
		fcText(d, str, x, y, depth, scale, color, FC_ALIGN_LEFT);
		return;
	}

	char buf[160];
	size_t len = strlen(str);
	if (len >= sizeof buf - 4)
		len = sizeof buf - 4;

	while (len > 1) {
		memcpy(buf, str, len);
		buf[len]     = '.';
		buf[len + 1] = '.';
		buf[len + 2] = '.';
		buf[len + 3] = '\0';

		if (fcTextWidth(d, buf, scale) <= maxWidth)
			break;
		len--;
	}

	fcText(d, buf, x, y, depth, scale, color, FC_ALIGN_LEFT);
}

void fcClipBegin(const FcDraw *d, float x, float y, float w, float h)
{
	C2D_Flush();

	const float screenW = d->screenW > 0.0f ? d->screenW : FC_BOT_W;
	const float fbW     = 240.0f;

	float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
	if (x0 < 0.0f) x0 = 0.0f;
	if (y0 < 0.0f) y0 = 0.0f;
	if (x1 > screenW) x1 = screenW;
	if (y1 > fbW)     y1 = fbW;

	if (x1 <= x0 || y1 <= y0) {
		C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 1, 1);
		return;
	}

	C3D_SetScissor(GPU_SCISSOR_NORMAL,
	               (u32)(fbW - y1),
	               (u32)(screenW - x1),
	               (u32)(fbW - y0),
	               (u32)(screenW - x0));
}

void fcClipEnd(void)
{
	C2D_Flush();
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

static int arcSegments(float radius)
{
	int n = (int)(radius * 1.2f);
	if (n < 3)  n = 3;
	if (n > 24) n = 24;
	return n;
}

static void cornerArc(float cx, float cy, float radius, float startAngle, u32 color)
{
	const int segs = arcSegments(radius);
	const float step = ((float)M_PI / 2.0f) / segs;

	float px = cx + cosf(startAngle) * radius;
	float py = cy + sinf(startAngle) * radius;

	for (int i = 1; i <= segs; i++) {
		const float a = startAngle + step * i;
		const float x = cx + cosf(a) * radius;
		const float y = cy + sinf(a) * radius;
		C2D_DrawTriangle(cx, cy, color, px, py, color, x, y, color, 0.0f);
		px = x;
		py = y;
	}
}

void fcDisc(float cx, float cy, float radius, u32 color)
{
	if (radius <= 0.0f)
		return;

	const int segs = arcSegments(radius) * 4;
	const float step = 2.0f * (float)M_PI / segs;

	float px = cx + radius, py = cy;
	for (int i = 1; i <= segs; i++) {
		const float a = step * i;
		const float x = cx + cosf(a) * radius;
		const float y = cy + sinf(a) * radius;
		C2D_DrawTriangle(cx, cy, color, px, py, color, x, y, color, 0.0f);
		px = x;
		py = y;
	}
}

static void discWedge(float cx, float cy, float radius, float a0, float a1,
                      u32 color)
{
	int segs = (int)((a1 - a0) * radius * 0.5f);
	if (segs < 2)  segs = 2;
	if (segs > 24) segs = 24;

	const float step = (a1 - a0) / segs;
	float px = cx + cosf(a0) * radius, py = cy + sinf(a0) * radius;

	for (int i = 1; i <= segs; i++) {
		const float a = a0 + step * i;
		const float x = cx + cosf(a) * radius, y = cy + sinf(a) * radius;
		C2D_DrawTriangle(cx, cy, color, px, py, color, x, y, color, 0.0f);
		px = x;
		py = y;
	}
}

void fcDiscSplit(float cx, float cy, float radius, const u32 *colors, int count)
{
	if (count <= 1) {
		fcDisc(cx, cy, radius, count == 1 ? colors[0] : 0);
		return;
	}

	const float span = 2.0f * (float)M_PI / (float)count;
	for (int i = 0; i < count; i++) {
		const float a0 = -(float)M_PI / 2.0f + span * (float)i;
		discWedge(cx, cy, radius, a0, a0 + span, colors[i]);
	}
}

void fcBarSplit(float x, float y, float w, float h, const u32 *colors, int count)
{
	if (count <= 0)
		return;
	if (count == 1) {
		C2D_DrawRectSolid(x, y, 0.0f, w, h, colors[0]);
		return;
	}

	for (int i = 0; i < count; i++) {
		const float y0 = y + h * (float)i / (float)count;
		const float y1 = y + h * (float)(i + 1) / (float)count;
		C2D_DrawRectSolid(x, y0, 0.0f, w, y1 - y0, colors[i]);
	}
}

void fcRing(float cx, float cy, float radius, float thickness, u32 color)
{
	if (radius <= 0.0f || thickness <= 0.0f)
		return;

	const int segs = arcSegments(radius) * 4;
	const float step = 2.0f * (float)M_PI / segs;
	const float inner = radius - thickness;

	for (int i = 0; i < segs; i++) {
		const float a0 = step * i, a1 = step * (i + 1);
		const float c0 = cosf(a0), s0 = sinf(a0);
		const float c1 = cosf(a1), s1 = sinf(a1);

		C2D_DrawTriangle(cx + c0 * inner,  cy + s0 * inner,  color,
		                 cx + c0 * radius, cy + s0 * radius, color,
		                 cx + c1 * radius, cy + s1 * radius, color, 0.0f);
		C2D_DrawTriangle(cx + c0 * inner,  cy + s0 * inner,  color,
		                 cx + c1 * radius, cy + s1 * radius, color,
		                 cx + c1 * inner,  cy + s1 * inner,  color, 0.0f);
	}
}

void fcRoundedRect(float x, float y, float w, float h, float radius,
                     float depth, u32 color, const FcEye *eye)
{
	const float sx = x + fcShift(eye, depth);

	if (radius <= 0.5f || w < radius * 2 || h < radius * 2) {
		C2D_DrawRectSolid(sx, y, 0.0f, w, h, color);
		return;
	}

	C2D_DrawRectSolid(sx + radius, y, 0.0f, w - radius * 2, h, color);
	C2D_DrawRectSolid(sx, y + radius, 0.0f, radius, h - radius * 2, color);
	C2D_DrawRectSolid(sx + w - radius, y + radius, 0.0f, radius, h - radius * 2, color);

	cornerArc(sx + radius,     y + radius,     radius, (float)M_PI,        color);
	cornerArc(sx + w - radius, y + radius,     radius, -(float)M_PI / 2.0f, color);
	cornerArc(sx + w - radius, y + h - radius, radius, 0.0f,               color);
	cornerArc(sx + radius,     y + h - radius, radius, (float)M_PI / 2.0f, color);
}

void fcCard(const FcDraw *d, float x, float y, float w, float h,
              float depth, const FcEye *eye)
{
	const FcPalette *p = &d->theme->pal;

	fcRoundedRect(x, y + 2.0f, w, h, FC_RADIUS, depth, p->cardShadow, eye);
	fcRoundedRect(x, y, w, h, FC_RADIUS, depth, p->cardEdge, eye);
	fcRoundedRect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, FC_RADIUS - 1.0f,
	                depth, p->card, eye);

	const float sx = x + fcShift(eye, depth);
	C2D_DrawRectSolid(sx + FC_RADIUS, y + 1.0f, 0.0f,
	                  w - FC_RADIUS * 2, 1.0f, p->highlight);
}

void fcCardWithStripe(const FcDraw *d, float x, float y, float w, float h,
                        float depth, const u32 *colors, int count,
                        const FcEye *eye)
{
	fcCard(d, x, y, w, h, depth, eye);

	const float sx = x + fcShift(eye, depth);
	fcBarSplit(sx + 2.0f, y + FC_RADIUS * 0.5f + 1.0f, 3.0f,
	             h - FC_RADIUS - 2.0f, colors, count);
}

void fcBackdrop(const FcDraw *d, float w, float h)
{
	const FcPalette *p = &d->theme->pal;

	C2D_DrawRectangle(0, 0, 0.0f, w, h,
	                  p->groundEdge, p->groundEdge, p->ground, p->ground);
}

void fcButton(const FcDraw *d, const char *label, float x, float y,
                float w, float h, FcButtonState state)
{
	const FcPalette *p = &d->theme->pal;
	const FcEye flat = fcEyeFlat();

	u32 fill = p->card, ink = p->ink;

	switch (state) {
	case FC_BTN_ACTIVE:   fill = p->accentSoft; ink = p->accent;  break;
	case FC_BTN_SELECTED: fill = p->accent;     ink = p->base;      break;
	case FC_BTN_DISABLED: fill = p->ground;     ink = p->inkFaint; break;
	default: break;
	}

	if (state != FC_BTN_DISABLED)
		fcRoundedRect(x, y + 1.0f, w, h, FC_RADIUS, 0.0f, p->cardShadow, &flat);

	fcRoundedRect(x, y, w, h, FC_RADIUS, 0.0f, p->cardEdge, &flat);
	fcRoundedRect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, FC_RADIUS - 1.0f,
	                0.0f, fill, &flat);

	fcText(d, label, x + w / 2, y + h / 2 - 8.0f, 0.0f, 0.46f, ink,
	         FC_ALIGN_CENTER);
}

void fcToggle(const FcDraw *d, float x, float y, bool on)
{
	const FcPalette *p = &d->theme->pal;
	const FcEye flat = fcEyeFlat();

	const float w = 34.0f, h = 18.0f, r = h / 2;

	fcRoundedRect(x, y, w, h, r, 0.0f, on ? p->accent : p->inkFaint, &flat);

	const float knobR = r - 2.0f;
	const float cx = on ? x + w - r : x + r;
	fcDisc(cx, y + r, knobR, p->base);
}

void fcColorSwatch(const FcDraw *d, float cx, float cy, float radius,
                     u32 color, bool selected)
{
	const FcPalette *p = &d->theme->pal;

	if (selected)
		fcDisc(cx, cy, radius + 3.0f, p->accent);

	fcDisc(cx, cy, radius + 1.0f, p->card);
	fcDisc(cx, cy, radius, color);
}

void fcChip(const FcDraw *d, const char *label, float x, float y, u32 color)
{
	const FcEye flat = fcEyeFlat();

	const float pad = 5.0f;
	const float w   = fcTextWidth(d, label, 0.34f) + pad * 2;
	const float h   = 14.0f;

	u32 soft = (color & 0x00FFFFFF) | 0x30000000;
	fcRoundedRect(x, y, w, h, h / 2, 0.0f, soft, &flat);
	fcText(d, label, x + pad, y + 1.0f, 0.0f, 0.38f, color, FC_ALIGN_LEFT);
}

void fcScrollbar(const FcDraw *d, float x, float y, float h,
                   float offset, float contentHeight, float viewHeight)
{
	if (contentHeight <= viewHeight)
		return;

	const FcPalette *p = &d->theme->pal;
	const FcEye flat = fcEyeFlat();

	const float trackW = 3.0f;
	float thumbH = h * (viewHeight / contentHeight);
	if (thumbH < 16.0f)
		thumbH = 16.0f;

	const float maxOffset = contentHeight - viewHeight;
	float t = maxOffset > 0 ? offset / maxOffset : 0.0f;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	fcRoundedRect(x, y, trackW, h, trackW / 2, 0.0f,
	                (p->inkFaint & 0x00FFFFFF) | 0x30000000, &flat);
	fcRoundedRect(x, y + t * (h - thumbH), trackW, thumbH, trackW / 2, 0.0f,
	                p->inkDim, &flat);
}

void fcSpinner(const FcDraw *d, float cx, float cy, float radius,
                 float phase, u32 color)
{
	const int dots = 8;
	for (int i = 0; i < dots; i++) {
		float a = (float)i / dots * 2.0f * (float)M_PI;
		float x = cx + cosf(a) * radius;
		float y = cy + sinf(a) * radius;

		float lead = phase - (float)i / dots;
		lead -= floorf(lead);
		float alpha = 0.15f + 0.85f * (1.0f - lead);

		u32 c = (color & 0x00FFFFFF) | ((u32)(alpha * 255.0f) << 24);
		fcDisc(x, y, 2.0f, c);
	}
}
