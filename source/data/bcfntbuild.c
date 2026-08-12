#include "data/bcfntbuild.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_HEADER   8
#define CFNT_SIZE      0x14
#define FINF_BODY      0x20
#define TGLP_BODY      0x18

#define FORMAT_A4      11

#define SHEET_ALIGN    128

#define CMAP_TABLE     1

#define CMAP_TABLE_OFF 0x0C

#define CMAP_MAX_SPAN 8192

#define GLYPH_FALLBACK 0

static void w16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

size_t fcA4PixelOffset(int x, int y, int sheetWidth, bool *highNibble)
{
	const int tx = x >> 3, ty = y >> 3;
	const int px = x & 7,  py = y & 7;

	const int morton = ((px & 1) << 0) | ((py & 1) << 1) |
	                   ((px & 2) << 1) | ((py & 2) << 2) |
	                   ((px & 4) << 2) | ((py & 4) << 3);

	const int tilesPerRow = sheetWidth >> 3;
	const size_t index = (size_t)(ty * tilesPerRow + tx) * 64 + (size_t)morton;

	if (highNibble)
		*highNibble = (index & 1) != 0;
	return index >> 1;
}

static int compareGlyphs(const void *a, const void *b)
{
	const FcGlyphSource *ga = a, *gb = b;
	if (ga->codepoint < gb->codepoint)
		return -1;
	if (ga->codepoint > gb->codepoint)
		return 1;
	return 0;
}

static size_t alignUp4(size_t v)
{
	return (v + 3) & ~(size_t)3;
}

static uint16_t roundUpPow2(uint16_t v)
{
	uint16_t p = 8;
	while (p < v && p < 1024)
		p = (uint16_t)(p << 1);
	return p;
}

void fcBcfntBuildDefaults(const FcGlyphSource *glyphs, int count,
                            FcBcfntBuildParams *out)
{
	memset(out, 0, sizeof *out);

	int maxW = 1, maxH = 1, maxTop = 1, maxAdv = 1;
	for (int i = 0; i < count; i++) {
		if (glyphs[i].width > maxW)
			maxW = glyphs[i].width;
		if (glyphs[i].height > maxH)
			maxH = glyphs[i].height;
		if (glyphs[i].top > maxTop)
			maxTop = glyphs[i].top;
		if (glyphs[i].advance > maxAdv)
			maxAdv = glyphs[i].advance;
	}

	int maxDescend = 1;
	for (int i = 0; i < count; i++) {
		const int below = glyphs[i].height - glyphs[i].top;
		if (below > maxDescend)
			maxDescend = below;
	}

	out->cellWidth    = (uint8_t)(maxW > 255 ? 255 : maxW);
	out->cellHeight   = (uint8_t)((maxTop + maxDescend) > 255 ? 255
	                                                          : (maxTop + maxDescend));
	out->baseline     = (uint8_t)(maxTop > 255 ? 255 : maxTop);
	out->maxCharWidth = (uint8_t)(maxAdv > 255 ? 255 : maxAdv);
	out->ascent       = out->baseline;
	out->lineFeed     = (uint8_t)(out->cellHeight + 1);

	uint16_t sheet = 128;
	while (sheet < 1024) {
		const int cols = (sheet - 1) / (out->cellWidth + 1);
		const int rows = (sheet - 1) / (out->cellHeight + 1);
		if (cols > 0 && rows > 0 && cols * rows * 4 >= count)
			break;
		sheet = (uint16_t)(sheet << 1);
	}

	out->sheetWidth  = roundUpPow2(sheet);
	out->sheetHeight = roundUpPow2(sheet);
}

static bool fail(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return false;
}

bool fcBcfntBuild(const FcGlyphSource *glyphs, int count,
                    const FcBcfntBuildParams *params,
                    uint8_t **out, size_t *outLen, char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';
	if (!glyphs || !params || !out || count <= 0)
		return fail(err, errSize, "nothing to build");
	if (params->cellWidth == 0 || params->cellHeight == 0)
		return fail(err, errSize, "cell has no size");
	if (params->sheetWidth < 8 || params->sheetHeight < 8 ||
	    (params->sheetWidth & 7) || (params->sheetHeight & 7))
		return fail(err, errSize, "sheet is not a multiple of the tile size");

	FcGlyphSource *g = malloc((size_t)count * sizeof *g);
	if (!g)
		return fail(err, errSize, "out of memory");
	memcpy(g, glyphs, (size_t)count * sizeof *g);
	qsort(g, (size_t)count, sizeof *g, compareGlyphs);

	for (int i = 0; i < count; i++) {
		if (g[i].width > params->cellWidth || g[i].height > params->cellHeight) {
			free(g);
			return fail(err, errSize, "a glyph is bigger than the cell");
		}
		if (i > 0 && g[i].codepoint == g[i - 1].codepoint) {
			free(g);
			return fail(err, errSize, "duplicate codepoint");
		}
		if (g[i].codepoint > 0xFFFF) {
			free(g);
			return fail(err, errSize, "codepoint is outside the BMP");
		}
	}

	const int cellW = params->cellWidth + 1;
	const int cellH = params->cellHeight + 1;
	const int cols  = (params->sheetWidth - 1) / cellW;
	const int rows  = (params->sheetHeight - 1) / cellH;

	if (cols <= 0 || rows <= 0) {
		free(g);
		return fail(err, errSize, "cell does not fit the sheet");
	}

	const int perSheet   = cols * rows;
	const int sheetCount = (count + perSheet - 1) / perSheet;
	const size_t sheetSize = (size_t)params->sheetWidth * params->sheetHeight / 2;

	if (sheetCount > 255) {
		free(g);
		return fail(err, errSize, "too many glyphs for one font");
	}

	const uint32_t cmapFirst = g[0].codepoint;
	const uint32_t cmapLast  = g[count - 1].codepoint;
	const size_t cmapSpan = (size_t)(cmapLast - cmapFirst) + 1;

	if (cmapSpan > CMAP_MAX_SPAN) {
		free(g);
		return fail(err, errSize, "codepoints are spread too far apart");
	}

	const size_t tglpDataStart = CFNT_SIZE + BLOCK_HEADER + FINF_BODY +
	                             BLOCK_HEADER + TGLP_BODY;
	const size_t sheetPad = ((tglpDataStart + SHEET_ALIGN - 1) &
	                         ~(size_t)(SHEET_ALIGN - 1)) - tglpDataStart;

	const size_t tglpSize = BLOCK_HEADER + TGLP_BODY + sheetPad +
	                        (size_t)sheetCount * sheetSize;
	const size_t cwdhSize = BLOCK_HEADER + 8 + (size_t)count * 3;

	const size_t cmapTotal = alignUp4(BLOCK_HEADER + CMAP_TABLE_OFF + cmapSpan * 2);

	const size_t finfOff = CFNT_SIZE;
	const size_t tglpOff = finfOff + BLOCK_HEADER + FINF_BODY;
	const size_t cwdhOff = tglpOff + tglpSize;
	const size_t cmapOff = cwdhOff + cwdhSize;
	const size_t total   = cmapOff + cmapTotal;

	uint8_t *buf = calloc(1, total);
	if (!buf) {
		free(g);
		return fail(err, errSize, "out of memory");
	}

	memcpy(buf, "CFNU", 4);
	w16(buf + 4, 0xFEFF);
	w16(buf + 6, CFNT_SIZE);
	w32(buf + 8, 0x03000000);
	w32(buf + 12, (uint32_t)total);
	w32(buf + 16, 4);

	uint8_t *finf = buf + finfOff;
	memcpy(finf, "FINF", 4);
	w32(finf + 4, BLOCK_HEADER + FINF_BODY);

	uint8_t *fb = finf + BLOCK_HEADER;
	fb[0x00] = 1;
	fb[0x01] = params->lineFeed;
	w16(fb + 0x02, 0);
	fb[0x04] = 0;
	fb[0x05] = params->cellWidth;
	fb[0x06] = params->maxCharWidth;
	fb[0x07] = 1;
	w32(fb + 0x08, (uint32_t)(tglpOff + BLOCK_HEADER));
	w32(fb + 0x0C, (uint32_t)(cwdhOff + BLOCK_HEADER));
	w32(fb + 0x10, (uint32_t)(cmapOff + BLOCK_HEADER));
	fb[0x14] = params->cellHeight;
	fb[0x15] = params->cellWidth;
	fb[0x16] = params->ascent;
	fb[0x17] = 0;

	uint8_t *tglp = buf + tglpOff;
	memcpy(tglp, "TGLP", 4);
	w32(tglp + 4, (uint32_t)tglpSize);

	uint8_t *tb = tglp + BLOCK_HEADER;
	tb[0x00] = params->cellWidth;
	tb[0x01] = params->cellHeight;
	tb[0x02] = params->baseline;
	tb[0x03] = params->maxCharWidth;
	w32(tb + 0x04, (uint32_t)sheetSize);
	w16(tb + 0x08, (uint16_t)sheetCount);
	w16(tb + 0x0A, FORMAT_A4);
	w16(tb + 0x0C, (uint16_t)cols);
	w16(tb + 0x0E, (uint16_t)rows);
	w16(tb + 0x10, params->sheetWidth);
	w16(tb + 0x12, params->sheetHeight);
	w32(tb + 0x14, (uint32_t)(tglpOff + BLOCK_HEADER + TGLP_BODY + sheetPad));

	uint8_t *sheets = tglp + BLOCK_HEADER + TGLP_BODY + sheetPad;

	for (int i = 0; i < count; i++) {
		const FcGlyphSource *src = &g[i];
		const int sheet = i / perSheet;
		const int slot  = i % perSheet;
		const int originX = (slot % cols) * cellW + 1;
		const int originY = (slot / cols) * cellH + 1;

		uint8_t *dst = sheets + (size_t)sheet * sheetSize;

		const int offsetY = params->baseline - src->top;

		for (int y = 0; y < src->height; y++) {
			const int sy = originY + offsetY + y;
			if (sy < 0 || sy >= params->sheetHeight)
				continue;

			for (int x = 0; x < src->width; x++) {
				const int sx = originX + x;
				if (sx < 0 || sx >= params->sheetWidth)
					continue;

				const uint8_t a = (uint8_t)(src->bitmap[y * src->width + x] >> 4);

				bool high = false;
				const size_t off = fcA4PixelOffset(sx, sy, params->sheetWidth,
				                                     &high);
				if (high)
					dst[off] = (uint8_t)((dst[off] & 0x0F) | (a << 4));
				else
					dst[off] = (uint8_t)((dst[off] & 0xF0) | a);
			}
		}
	}

	uint8_t *cwdh = buf + cwdhOff;
	memcpy(cwdh, "CWDH", 4);
	w32(cwdh + 4, (uint32_t)cwdhSize);

	uint8_t *cb = cwdh + BLOCK_HEADER;
	w16(cb + 0, 0);
	w16(cb + 2, (uint16_t)(count - 1));
	w32(cb + 4, 0);

	for (int i = 0; i < count; i++) {
		cb[8 + i * 3 + 0] = (uint8_t)g[i].left;
		cb[8 + i * 3 + 1] = g[i].width;
		cb[8 + i * 3 + 2] = g[i].advance;
	}

	uint8_t *cmap = buf + cmapOff;

	memcpy(cmap, "CMAP", 4);
	w32(cmap + 4, (uint32_t)cmapTotal);

	uint8_t *mb = cmap + BLOCK_HEADER;
	w16(mb + 0x00, (uint16_t)cmapFirst);
	w16(mb + 0x02, (uint16_t)cmapLast);
	w16(mb + 0x04, CMAP_TABLE);
	w16(mb + 0x06, 0);
	w32(mb + 0x08, 0);

	uint8_t *table = mb + 0x0C;
	for (size_t i = 0; i < cmapSpan; i++)
		w16(table + i * 2, GLYPH_FALLBACK);
	for (int i = 0; i < count; i++)
		w16(table + (g[i].codepoint - cmapFirst) * 2, (uint16_t)i);

	free(g);

	*out = buf;
	if (outLen)
		*outLen = total;
	return true;
}
