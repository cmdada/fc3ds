#include "data/bcfntedit.h"

#include "data/bcfnt.h"

#include <stdio.h>
#include <string.h>

#define BLOCK_HEADER 8
#define FINF_OFFSET  0x14

#define CMAP_DIRECT 0
#define CMAP_TABLE  1
#define CMAP_SCAN   2

#define MAX_CHAIN 4096

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static bool fail(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return false;
}

static size_t resolve(uint32_t stored, uint32_t base)
{
	return (size_t)(stored - base);
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

bool fcBcfntEditRebase(uint8_t *font, size_t len, uint32_t oldBase,
                         uint32_t newBase)
{
	if (!font || len < FINF_OFFSET + 0x20)
		return false;
	if (oldBase == newBase)
		return true;

	const uint32_t delta = newBase - oldBase;
	uint8_t *finf = font + FINF_OFFSET;

	const uint32_t tglp = rd32(finf + 0x10);
	const uint32_t cwdh = rd32(finf + 0x14);
	const uint32_t cmap = rd32(finf + 0x18);

	if (resolve(tglp, oldBase) + 0x18 > len ||
	    resolve(cwdh, oldBase) + 8 > len ||
	    resolve(cmap, oldBase) + 0x0E > len)
		return false;

	for (uint32_t a = cwdh, hop = 0; a && hop < MAX_CHAIN; hop++) {
		const size_t at = resolve(a, oldBase);
		if (at + 8 > len)
			return false;
		const uint32_t next = rd32(font + at + 0x04);
		wr32(font + at + 0x04, next ? next + delta : 0);
		a = next;
	}

	for (uint32_t a = cmap, hop = 0; a && hop < MAX_CHAIN; hop++) {
		const size_t at = resolve(a, oldBase);
		if (at + 0x0E > len)
			return false;
		const uint32_t next = rd32(font + at + 0x08);
		wr32(font + at + 0x08, next ? next + delta : 0);
		a = next;
	}

	uint8_t *tglpBlock = font + resolve(tglp, oldBase);
	wr32(tglpBlock + 0x14, rd32(tglpBlock + 0x14) + delta);

	wr32(finf + 0x10, tglp + delta);
	wr32(finf + 0x14, cwdh + delta);
	wr32(finf + 0x18, cmap + delta);

	return true;
}

bool fcBcfntEditOpen(const uint8_t *font, size_t len, uint32_t pointerBase,
                       FcBcfntEdit *out, char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';
	if (!font || !out)
		return fail(err, errSize, "no font");

	memset(out, 0, sizeof *out);

	if (!fcBcfntLooksLikeFont(font, len))
		return fail(err, errSize, "not a BCFNT");
	if (len < FINF_OFFSET + 0x20)
		return fail(err, errSize, "font is truncated");
	if (memcmp(font + FINF_OFFSET, "FINF", 4) != 0)
		return fail(err, errSize, "font has no FINF");

	out->pointerBase = pointerBase;

	const uint32_t tglpPtr = rd32(font + FINF_OFFSET + 0x10);
	const size_t tglpOff = resolve(tglpPtr, pointerBase);
	if (tglpOff < BLOCK_HEADER || tglpOff + 0x18 > len)
		return fail(err, errSize, "font has no usable TGLP");

	const uint8_t *tglp = font + tglpOff;

	out->cellWidth       = tglp[0x00];
	out->cellHeight      = tglp[0x01];
	out->baseline        = tglp[0x02];
	out->maxCharWidth    = tglp[0x03];
	out->sheetSize       = rd32(tglp + 0x04);
	out->sheetCount      = rd16(tglp + 0x08);
	out->columns         = rd16(tglp + 0x0C);
	out->rows            = rd16(tglp + 0x0E);
	out->sheetWidth      = rd16(tglp + 0x10);
	out->sheetHeight     = rd16(tglp + 0x12);
	out->sheetDataOffset = resolve(rd32(tglp + 0x14), pointerBase);

	if (rd16(tglp + 0x0A) != 11)
		return fail(err, errSize, "font sheets are not A4");
	if (out->cellWidth == 0 || out->cellHeight == 0 ||
	    out->columns == 0 || out->rows == 0 || out->sheetCount == 0)
		return fail(err, errSize, "font has a degenerate glyph grid");
	if (out->sheetWidth == 0 || (out->sheetWidth & 7))
		return fail(err, errSize, "font sheet width is not a tile multiple");

	const size_t need = out->sheetDataOffset +
	                    (size_t)out->sheetCount * out->sheetSize;
	if (out->sheetDataOffset == 0 || need > len)
		return fail(err, errSize, "font sheets fall outside the file");
	if ((size_t)out->sheetSize <
	    (size_t)out->sheetWidth * out->sheetHeight / 2)
		return fail(err, errSize, "font sheet size disagrees with its bounds");

	out->glyphCapacity = (int)out->columns * out->rows * out->sheetCount;
	return true;
}

int fcBcfntEditFindGlyph(const uint8_t *font, size_t len,
                           const FcBcfntEdit *info, uint32_t codepoint)
{
	if (!font || !info || len < FINF_OFFSET + 0x20 || codepoint > 0xFFFF)
		return -1;

	const uint32_t base = info->pointerBase;
	uint32_t ptr = rd32(font + FINF_OFFSET + 0x18);

	for (int hop = 0; hop < MAX_CHAIN && ptr != 0; hop++) {
		const size_t off = resolve(ptr, base);
		if (off + 0x0E > len)
			return -1;

		const uint8_t *m = font + off;
		const uint16_t begin  = rd16(m + 0x00);
		const uint16_t end    = rd16(m + 0x02);
		const uint16_t method = rd16(m + 0x04);
		const uint32_t next   = rd32(m + 0x08);

		if (codepoint >= begin && codepoint <= end) {
			const uint32_t rel = codepoint - begin;

			if (method == CMAP_DIRECT) {
				const uint16_t first = rd16(m + 0x0C);
				if (first == 0xFFFF)
					return -1;
				return (int)(first + rel);
			}

			if (method == CMAP_TABLE) {
				const size_t at = off + 0x0C + (size_t)rel * 2;
				if (at + 2 > len)
					return -1;
				const uint16_t idx = rd16(font + at);
				return idx == 0xFFFF ? -1 : (int)idx;
			}

			if (method == CMAP_SCAN) {
				const uint16_t n = rd16(m + 0x0C);
				for (uint16_t i = 0; i < n; i++) {
					const size_t at = off + 0x0E + (size_t)i * 4;
					if (at + 4 > len)
						return -1;
					if (rd16(font + at) == (uint16_t)codepoint) {
						const uint16_t idx = rd16(font + at + 2);
						return idx == 0xFFFF ? -1 : (int)idx;
					}
				}
				return -1;
			}

			return -1;
		}

		ptr = next;
	}

	return -1;
}

uint8_t *fcBcfntEditWidthEntry(uint8_t *font, size_t len,
                                 const FcBcfntEdit *info, int glyphIndex)
{
	if (!font || !info || len < FINF_OFFSET + 0x20 || glyphIndex < 0)
		return NULL;

	const uint32_t base = info->pointerBase;
	uint32_t ptr = rd32(font + FINF_OFFSET + 0x14);

	for (int hop = 0; hop < MAX_CHAIN && ptr != 0; hop++) {
		const size_t off = resolve(ptr, base);
		if (off + 8 > len)
			return NULL;

		const uint8_t *c = font + off;
		const uint16_t first = rd16(c + 0x00);
		const uint16_t last  = rd16(c + 0x02);
		const uint32_t next  = rd32(c + 0x04);

		if (glyphIndex >= first && glyphIndex <= last) {
			const size_t at = off + 8 + (size_t)(glyphIndex - first) * 3;
			if (at + 3 > len)
				return NULL;
			return font + at;
		}

		ptr = next;
	}

	return NULL;
}

bool fcBcfntEditReplaceGlyph(uint8_t *font, size_t len, const FcBcfntEdit *info,
                               int glyphIndex, const FcGlyphSource *glyph)
{
	if (!font || !info || !glyph)
		return false;
	if (glyphIndex < 0 || glyphIndex >= info->glyphCapacity)
		return false;
	if (glyph->width > info->cellWidth || glyph->height > info->cellHeight)
		return false;

	const int perSheet = (int)info->columns * info->rows;
	const int sheet    = glyphIndex / perSheet;
	const int inSheet  = glyphIndex % perSheet;
	const int line     = inSheet / info->columns;
	const int col      = inSheet % info->columns;

	if (sheet >= info->sheetCount)
		return false;

	const int originX = col * (info->cellWidth + 1) + 1;
	const int originY = line * (info->cellHeight + 1) + 1;

	if (originX + info->cellWidth > info->sheetWidth ||
	    originY + info->cellHeight > info->sheetHeight)
		return false;

	uint8_t *sheetData = font + info->sheetDataOffset +
	                     (size_t)sheet * info->sheetSize;

	for (int y = 0; y < info->cellHeight; y++) {
		for (int x = 0; x < info->cellWidth; x++) {
			bool high = false;
			const size_t at = fcA4PixelOffset(originX + x, originY + y,
			                                    info->sheetWidth, &high);
			sheetData[at] = high ? (uint8_t)(sheetData[at] & 0x0F)
			                     : (uint8_t)(sheetData[at] & 0xF0);
		}
	}

	const int offsetY = info->baseline - glyph->top;

	for (int y = 0; y < glyph->height; y++) {
		const int sy = originY + offsetY + y;
		if (sy < originY || sy >= originY + info->cellHeight)
			continue;

		for (int x = 0; x < glyph->width; x++) {
			const int sx = originX + x;
			if (sx >= originX + info->cellWidth)
				continue;

			const uint8_t a = (uint8_t)(glyph->bitmap[y * glyph->width + x] >> 4);

			bool high = false;
			const size_t at = fcA4PixelOffset(sx, sy, info->sheetWidth, &high);
			sheetData[at] = high ? (uint8_t)((sheetData[at] & 0x0F) | (a << 4))
			                     : (uint8_t)((sheetData[at] & 0xF0) | a);
		}
	}

	uint8_t *w = fcBcfntEditWidthEntry(font, len, info, glyphIndex);
	if (w) {
		w[0] = (uint8_t)glyph->left;
		w[1] = glyph->width;
		w[2] = glyph->advance ? glyph->advance : (uint8_t)(glyph->width + 1);
	}

	return true;
}

bool fcBcfntEditGlyphInk(const uint8_t *font, size_t len,
                           const FcBcfntEdit *info, int glyphIndex,
                           int *top, int *bottom, int *left, int *right)
{
	if (!font || !info || glyphIndex < 0 || glyphIndex >= info->glyphCapacity)
		return false;

	const int perSheet = (int)info->columns * info->rows;
	const int sheet    = glyphIndex / perSheet;
	const int inSheet  = glyphIndex % perSheet;
	const int originX  = (inSheet % info->columns) * (info->cellWidth + 1) + 1;
	const int originY  = (inSheet / info->columns) * (info->cellHeight + 1) + 1;

	if (sheet >= info->sheetCount)
		return false;
	if (originX + info->cellWidth > info->sheetWidth ||
	    originY + info->cellHeight > info->sheetHeight)
		return false;

	const size_t base = info->sheetDataOffset + (size_t)sheet * info->sheetSize;
	if (base + info->sheetSize > len)
		return false;

	const uint8_t *sheetData = font + base;

	int t = info->cellHeight, b = -1, l = info->cellWidth, r = -1;

	for (int y = 0; y < info->cellHeight; y++) {
		for (int x = 0; x < info->cellWidth; x++) {
			bool high = false;
			const size_t at = fcA4PixelOffset(originX + x, originY + y,
			                                    info->sheetWidth, &high);
			const uint8_t a = high ? (uint8_t)(sheetData[at] >> 4)
			                       : (uint8_t)(sheetData[at] & 0x0F);
			if (a == 0)
				continue;

			if (y < t) t = y;
			if (y > b) b = y;
			if (x < l) l = x;
			if (x > r) r = x;
		}
	}

	if (b < 0)
		return false;

	if (top)    *top = t;
	if (bottom) *bottom = b;
	if (left)   *left = l;
	if (right)  *right = r;
	return true;
}
