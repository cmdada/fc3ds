#include "harness.h"

#include "data/bcfntbuild.h"
#include "data/bcfntedit.h"

#include <stdlib.h>
#include <string.h>

static uint8_t g_ink[64 * 64];

static FcGlyphSource glyph(uint32_t cp, uint8_t w, uint8_t h, uint8_t adv)
{
	FcGlyphSource g;
	memset(&g, 0, sizeof g);
	g.codepoint = cp;
	g.bitmap    = g_ink;
	g.width     = w;
	g.height    = h;
	g.top       = (int8_t)h;
	g.advance   = adv;
	return g;
}

static uint8_t *buildStock(size_t *len, int *count)
{
	static FcGlyphSource src[95];
	for (int i = 0; i < 95; i++)
		src[i] = glyph((uint32_t)(0x20 + i), 10, 12, 11);

	FcBcfntBuildParams p;
	fcBcfntBuildDefaults(src, 95, &p);

	uint8_t *font = NULL;
	if (!fcBcfntBuild(src, 95, &p, &font, len, NULL, 0))
		return NULL;
	*count = 95;
	return font;
}

static uint8_t readPixel(const uint8_t *sheet, int x, int y, int sheetW)
{
	bool high = false;
	const size_t off = fcA4PixelOffset(x, y, sheetW, &high);
	return high ? (uint8_t)(sheet[off] >> 4) : (uint8_t)(sheet[off] & 0x0F);
}

void testBcfntEdit(void)
{
	memset(g_ink, 0xFF, sizeof g_ink);

	TEST_CASE("bcfntedit: opening reads the glyph grid");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);
		CHECK(font != NULL);

		FcBcfntEdit e;
		char err[64];
		CHECK(fcBcfntEditOpen(font, len, 0, &e, err, sizeof err));
		CHECK_EQ_STR(err, "");
		CHECK(e.cellWidth > 0 && e.cellHeight > 0);
		CHECK(e.columns > 0 && e.rows > 0);
		CHECK(e.glyphCapacity >= count);
		CHECK(e.sheetDataOffset % 128 == 0);

		free(font);
	}

	TEST_CASE("bcfntedit: codepoints resolve to the right glyph index");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, 0, &e, NULL, 0));

		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, ' '), 0);
		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, 'A'), 'A' - 0x20);
		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, '~'), '~' - 0x20);

		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, 0x3042), -1);
		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, 0x1F600), -1);

		free(font);
	}

	TEST_CASE("bcfntedit: replacing a glyph changes only that cell");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, 0, &e, NULL, 0));

		uint8_t *before = malloc(len);
		memcpy(before, font, len);

		const int idx = fcBcfntEditFindGlyph(font, len, &e, 'A');
		CHECK(idx > 0);

		FcGlyphSource g = glyph('A', 5, 6, 7);
		CHECK(fcBcfntEditReplaceGlyph(font, len, &e, idx, &g));

		const int perSheet = e.columns * e.rows;
		const int other = idx + 1;
		const int oc = (other % perSheet) % e.columns;
		const int ol = (other % perSheet) / e.columns;
		const int ox = oc * (e.cellWidth + 1) + 1;
		const int oy = ol * (e.cellHeight + 1) + 1;
		const uint8_t *sheetNow = font + e.sheetDataOffset +
		                          (size_t)(other / perSheet) * e.sheetSize;
		const uint8_t *sheetWas = before + e.sheetDataOffset +
		                          (size_t)(other / perSheet) * e.sheetSize;
		for (int y = 0; y < e.cellHeight; y++) {
			for (int x = 0; x < e.cellWidth; x++) {
				CHECK_EQ_INT(readPixel(sheetNow, ox + x, oy + y, e.sheetWidth),
				             readPixel(sheetWas, ox + x, oy + y, e.sheetWidth));
			}
		}

		const int c = (idx % perSheet) % e.columns;
		const int l = (idx % perSheet) / e.columns;
		const int x0 = c * (e.cellWidth + 1) + 1;
		const int y0 = l * (e.cellHeight + 1) + 1;
		const uint8_t *sheet = font + e.sheetDataOffset +
		                       (size_t)(idx / perSheet) * e.sheetSize;

		const int top = e.baseline - g.top;
		CHECK_EQ_INT(readPixel(sheet, x0, y0 + top, e.sheetWidth), 0x0F);
		CHECK_EQ_INT(readPixel(sheet, x0 + 4, y0 + top + 5, e.sheetWidth), 0x0F);
		CHECK_EQ_INT(readPixel(sheet, x0 + 6, y0 + top, e.sheetWidth), 0x00);

		free(before);
		free(font);
	}

	TEST_CASE("bcfntedit: the width entry travels with the glyph");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, 0, &e, NULL, 0));

		const int idx = fcBcfntEditFindGlyph(font, len, &e, 'M');
		uint8_t *w = fcBcfntEditWidthEntry(font, len, &e, idx);
		CHECK(w != NULL);
		CHECK_EQ_INT(w[2], 11);

		FcGlyphSource g = glyph('M', 7, 9, 8);
		g.left = 2;
		CHECK(fcBcfntEditReplaceGlyph(font, len, &e, idx, &g));

		w = fcBcfntEditWidthEntry(font, len, &e, idx);
		CHECK_EQ_INT((int8_t)w[0], 2);
		CHECK_EQ_INT(w[1], 7);
		CHECK_EQ_INT(w[2], 8);

		free(font);
	}

	TEST_CASE("bcfntedit: a glyph too big for the cell is refused, not clipped");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, 0, &e, NULL, 0));

		const int idx = fcBcfntEditFindGlyph(font, len, &e, 'B');

		FcGlyphSource wide = glyph('B', (uint8_t)(e.cellWidth + 1), 8, 9);
		CHECK(!fcBcfntEditReplaceGlyph(font, len, &e, idx, &wide));

		FcGlyphSource tall = glyph('B', 6, (uint8_t)(e.cellHeight + 1), 9);
		CHECK(!fcBcfntEditReplaceGlyph(font, len, &e, idx, &tall));

		FcGlyphSource ok = glyph('B', 5, 5, 6);
		CHECK(!fcBcfntEditReplaceGlyph(font, len, &e, -1, &ok));
		CHECK(!fcBcfntEditReplaceGlyph(font, len, &e, e.glyphCapacity, &ok));

		free(font);
	}

	TEST_CASE("bcfntedit: an edited font still reads back as a valid font");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, 0, &e, NULL, 0));

		for (uint32_t cp = 'a'; cp <= 'z'; cp++) {
			const int idx = fcBcfntEditFindGlyph(font, len, &e, cp);
			CHECK(idx >= 0);
			FcGlyphSource g = glyph(cp, 8, 10, 9);
			CHECK(fcBcfntEditReplaceGlyph(font, len, &e, idx, &g));
		}

		FcBcfntEdit again;
		CHECK(fcBcfntEditOpen(font, len, 0, &again, NULL, 0));
		CHECK_EQ_INT(again.glyphCapacity, e.glyphCapacity);
		CHECK_EQ_INT(again.sheetCount, e.sheetCount);
		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, 'z'), 'z' - 0x20);
		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, 0x3042), -1);

		free(font);
	}

	TEST_CASE("bcfntedit: ink bounds are measured from pixels");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, 0, &e, NULL, 0));

		const int idx = fcBcfntEditFindGlyph(font, len, &e, 'H');
		CHECK(idx >= 0);

		FcGlyphSource g = glyph('H', 6, 9, 7);
		CHECK(fcBcfntEditReplaceGlyph(font, len, &e, idx, &g));

		int top = -1, bottom = -1, left = -1, right = -1;
		CHECK(fcBcfntEditGlyphInk(font, len, &e, idx, &top, &bottom,
		                            &left, &right));

		CHECK_EQ_INT(top, e.baseline - 9);
		CHECK_EQ_INT(bottom, e.baseline - 1);
		CHECK_EQ_INT(left, 0);
		CHECK_EQ_INT(right, 5);

		CHECK_EQ_INT(e.baseline - top, 9);

		FcGlyphSource blank = glyph(' ', 0, 0, 5);
		blank.bitmap = g_ink;
		const int spaceIdx = fcBcfntEditFindGlyph(font, len, &e, ' ');
		CHECK(fcBcfntEditReplaceGlyph(font, len, &e, spaceIdx, &blank));
		CHECK(!fcBcfntEditGlyphInk(font, len, &e, spaceIdx, &top, &bottom,
		                             &left, &right));

		free(font);
	}

	TEST_CASE("bcfntedit: rebasing pointers round-trips exactly");
	{
		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);

		uint8_t *original = malloc(len);
		memcpy(original, font, len);

		const uint32_t fakeBase = 0x18000000;
		CHECK(fcBcfntEditRebase(font, len, 0, fakeBase));
		CHECK(memcmp(font, original, len) != 0);

		FcBcfntEdit e;
		CHECK(fcBcfntEditOpen(font, len, fakeBase, &e, NULL, 0));
		CHECK_EQ_INT(fcBcfntEditFindGlyph(font, len, &e, 'A'), 'A' - 0x20);

		CHECK(fcBcfntEditRebase(font, len, fakeBase, 0));
		CHECK(memcmp(font, original, len) == 0);

		free(original);
		free(font);
	}

	TEST_CASE("bcfntedit: garbage is refused with a reason");
	{
		FcBcfntEdit e;
		char err[64];

		const uint8_t junk[64] = { 0 };
		CHECK(!fcBcfntEditOpen(junk, sizeof junk, 0, &e, err, sizeof err));
		CHECK_EQ_STR(err, "not a BCFNT");

		CHECK(!fcBcfntEditOpen(NULL, 0, 0, &e, err, sizeof err));
		CHECK_EQ_STR(err, "no font");

		size_t len = 0;
		int count = 0;
		uint8_t *font = buildStock(&len, &count);
		font[0x14 + 0x10] = 0xFF;
		font[0x14 + 0x11] = 0xFF;
		font[0x14 + 0x12] = 0xFF;
		font[0x14 + 0x13] = 0x7F;
		CHECK(!fcBcfntEditOpen(font, len, 0, &e, err, sizeof err));
		CHECK_EQ_STR(err, "font has no usable TGLP");

		free(font);
	}
}
