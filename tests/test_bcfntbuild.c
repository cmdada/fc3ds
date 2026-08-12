#include "harness.h"

#include "data/bcfnt.h"
#include "data/bcfntbuild.h"

#include <stdlib.h>
#include <string.h>

static uint8_t g_solid[32 * 32];

static FcGlyphSource makeGlyph(uint32_t cp, uint8_t w, uint8_t h)
{
	FcGlyphSource g;
	memset(&g, 0, sizeof g);
	g.codepoint = cp;
	g.bitmap    = g_solid;
	g.width     = w;
	g.height    = h;
	g.left      = 0;
	g.top       = (int8_t)h;
	g.advance   = (uint8_t)(w + 1);
	return g;
}

static uint8_t readPixel(const uint8_t *sheet, int x, int y, int sheetW)
{
	bool high = false;
	const size_t off = fcA4PixelOffset(x, y, sheetW, &high);
	return high ? (uint8_t)(sheet[off] >> 4) : (uint8_t)(sheet[off] & 0x0F);
}

void testBcfntBuild(void)
{
	memset(g_solid, 0xFF, sizeof g_solid);

	TEST_CASE("bcfntbuild: tiled pixel offsets");
	{
		bool high = false;

		CHECK_EQ_INT(fcA4PixelOffset(0, 0, 64, &high), 0);
		CHECK(!high);

		CHECK_EQ_INT(fcA4PixelOffset(1, 0, 64, &high), 0);
		CHECK(high);

		CHECK_EQ_INT(fcA4PixelOffset(0, 1, 64, &high), 1);
		CHECK(!high);

		CHECK_EQ_INT(fcA4PixelOffset(8, 0, 64, &high), 32);

		CHECK_EQ_INT(fcA4PixelOffset(0, 8, 64, &high), (64 / 8) * 32);

		bool seen[64];
		memset(seen, 0, sizeof seen);
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++) {
				const size_t off = fcA4PixelOffset(x, y, 64, &high);
				const int idx = (int)(off * 2 + (high ? 1 : 0));
				CHECK(idx >= 0 && idx < 64);
				CHECK(!seen[idx]);
				seen[idx] = true;
			}
		}
	}

	TEST_CASE("bcfntbuild: a built font reads back");
	{
		FcGlyphSource glyphs[8];
		for (int i = 0; i < 8; i++)
			glyphs[i] = makeGlyph((uint32_t)('A' + i), 10, 12);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 8, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		char err[64];
		CHECK(fcBcfntBuild(glyphs, 8, &p, &font, &len, err, sizeof err));
		CHECK_EQ_STR(err, "");
		CHECK(font != NULL && len > 0);

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(font, len, &info));
		CHECK_EQ_STR(info.magic, "CFNU");
		CHECK(info.haveFinf);
		CHECK(info.haveTglp);
		CHECK_EQ_INT(info.cellWidth, p.cellWidth);
		CHECK_EQ_INT(info.cellHeight, p.cellHeight);
		CHECK_EQ_INT(info.sheetCount, 1);
		CHECK_EQ_STR(fcBcfntFormatName(info.sheetFormat), "A4");
		CHECK_EQ_INT(info.fileSize, (uint32_t)len);

		free(font);
	}

	TEST_CASE("bcfntbuild: sheet data is aligned for the GPU");
	{
		FcGlyphSource glyphs[4];
		for (int i = 0; i < 4; i++)
			glyphs[i] = makeGlyph((uint32_t)('a' + i), 10, 12);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 4, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(glyphs, 4, &p, &font, &len, NULL, 0));

		const uint8_t *finf = font + 0x14;
		const uint32_t tglpOff = (uint32_t)finf[0x10] |
		                         ((uint32_t)finf[0x11] << 8) |
		                         ((uint32_t)finf[0x12] << 16) |
		                         ((uint32_t)finf[0x13] << 24);
		const uint8_t *tb = font + tglpOff;
		const uint32_t dataOff = (uint32_t)tb[0x14] | ((uint32_t)tb[0x15] << 8) |
		                         ((uint32_t)tb[0x16] << 16) |
		                         ((uint32_t)tb[0x17] << 24);

		CHECK_EQ_INT(dataOff % 128, 0);

		const uint32_t sheetSize = (uint32_t)tb[0x04] |
		                           ((uint32_t)tb[0x05] << 8) |
		                           ((uint32_t)tb[0x06] << 16) |
		                           ((uint32_t)tb[0x07] << 24);
		CHECK_EQ_INT(sheetSize, p.sheetWidth * p.sheetHeight / 2);

		free(font);
	}

	TEST_CASE("bcfntbuild: glyph pixels land where the cell says");
	{
		FcGlyphSource one = makeGlyph('X', 8, 8);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(&one, 1, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(&one, 1, &p, &font, &len, NULL, 0));

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(font, len, &info));

		const uint32_t tglpOff = (uint32_t)font[0x14 + 8 + 0x08] |
		                         ((uint32_t)font[0x14 + 8 + 0x09] << 8) |
		                         ((uint32_t)font[0x14 + 8 + 0x0A] << 16) |
		                         ((uint32_t)font[0x14 + 8 + 0x0B] << 24);
		const uint8_t *tb = font + tglpOff;
		const uint32_t dataOff = (uint32_t)tb[0x14] | ((uint32_t)tb[0x15] << 8) |
		                         ((uint32_t)tb[0x16] << 16) |
		                         ((uint32_t)tb[0x17] << 24);
		const uint8_t *sheet = font + dataOff;

		const int y0 = p.baseline - one.top;
		CHECK_EQ_INT(y0, 0);

		CHECK_EQ_INT(readPixel(sheet, 0, 0, p.sheetWidth), 0x00);
		CHECK_EQ_INT(readPixel(sheet, 1, 1, p.sheetWidth), 0x0F);
		CHECK_EQ_INT(readPixel(sheet, 8, 8, p.sheetWidth), 0x0F);

		CHECK_EQ_INT(readPixel(sheet, 9, 1, p.sheetWidth), 0x00);
		CHECK_EQ_INT(readPixel(sheet, 1, 9, p.sheetWidth), 0x00);

		free(font);
	}

	TEST_CASE("bcfntbuild: glyphs sit on a shared baseline");
	{
		FcGlyphSource glyphs[2];
		glyphs[0] = makeGlyph('l', 4, 16);
		glyphs[1] = makeGlyph('o', 8, 8);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 2, &p);

		CHECK_EQ_INT(p.baseline, 16);

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(glyphs, 2, &p, &font, &len, NULL, 0));
		free(font);
	}

	TEST_CASE("bcfntbuild: contiguous codepoints become one direct CMAP");
	{
		FcGlyphSource glyphs[16];
		for (int i = 0; i < 16; i++)
			glyphs[i] = makeGlyph((uint32_t)(0x20 + i), 6, 8);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 16, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(glyphs, 16, &p, &font, &len, NULL, 0));

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(font, len, &info));
		CHECK_EQ_INT(info.blockCount, 4);

		free(font);
	}

	TEST_CASE("bcfntbuild: the CMAP matches libctru's struct field for field");
	{
		FcGlyphSource glyphs[8];
		for (int i = 0; i < 8; i++)
			glyphs[i] = makeGlyph((uint32_t)('A' + i), 6, 8);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 8, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(glyphs, 8, &p, &font, &len, NULL, 0));

		const uint8_t *finf = font + 0x14;
		const uint32_t cmapOff = (uint32_t)finf[0x18] |
		                         ((uint32_t)finf[0x19] << 8) |
		                         ((uint32_t)finf[0x1A] << 16) |
		                         ((uint32_t)finf[0x1B] << 24);
		const uint8_t *mb = font + cmapOff;

		CHECK(memcmp(mb - 8, "CMAP", 4) == 0);

		CHECK_EQ_INT(mb[0x00] | (mb[0x01] << 8), 'A');
		CHECK_EQ_INT(mb[0x02] | (mb[0x03] << 8), 'H');
		CHECK_EQ_INT(mb[0x04] | (mb[0x05] << 8), 1);

		CHECK_EQ_INT(mb[0x08] | (mb[0x09] << 8) | (mb[0x0A] << 16) |
		             (mb[0x0B] << 24), 0);

		for (int i = 0; i < 8; i++)
			CHECK_EQ_INT(mb[0x0C + i * 2] | (mb[0x0D + i * 2] << 8), i);

		free(font);
	}

	TEST_CASE("bcfntbuild: a codepoint outside the BMP is refused");
	{
		FcGlyphSource g = makeGlyph(0x1F600, 6, 8);
		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(&g, 1, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		char err[64];
		CHECK(!fcBcfntBuild(&g, 1, &p, &font, &len, err, sizeof err));
		CHECK_EQ_STR(err, "codepoint is outside the BMP");
	}

	TEST_CASE("bcfntbuild: gaps in the range become absent entries");
	{
		FcGlyphSource glyphs[6];
		for (int i = 0; i < 3; i++)
			glyphs[i] = makeGlyph((uint32_t)('a' + i), 6, 8);
		for (int i = 0; i < 3; i++)
			glyphs[3 + i] = makeGlyph((uint32_t)('m' + i), 6, 8);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 6, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(glyphs, 6, &p, &font, &len, NULL, 0));

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(font, len, &info));
		CHECK_EQ_INT(info.blockCount, 4);

		const uint8_t *finf = font + 0x14;
		const uint32_t cmapOff = (uint32_t)finf[0x18] |
		                         ((uint32_t)finf[0x19] << 8) |
		                         ((uint32_t)finf[0x1A] << 16) |
		                         ((uint32_t)finf[0x1B] << 24);
		const uint8_t *mb = font + cmapOff;
		const uint8_t *table = mb + 0x0C;

		CHECK_EQ_INT(mb[0x00] | (mb[0x01] << 8), 'a');
		CHECK_EQ_INT(mb[0x02] | (mb[0x03] << 8), 'o');

		CHECK_EQ_INT(table[0] | (table[1] << 8), 0);
		CHECK_EQ_INT(table[('m' - 'a') * 2] | (table[('m' - 'a') * 2 + 1] << 8),
		             3);

		for (int cp = 'a'; cp <= 'o'; cp++) {
			const int e = table[(cp - 'a') * 2] | (table[(cp - 'a') * 2 + 1] << 8);
			CHECK(e < 6);
		}

		free(font);
	}

	TEST_CASE("bcfntbuild: codepoints spread too far apart are refused");
	{
		FcGlyphSource glyphs[2];
		glyphs[0] = makeGlyph('a', 6, 8);
		glyphs[1] = makeGlyph(0x2603, 6, 8);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, 2, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		char err[64];
		CHECK(!fcBcfntBuild(glyphs, 2, &p, &font, &len, err, sizeof err));
		CHECK_EQ_STR(err, "codepoints are spread too far apart");
	}

	TEST_CASE("bcfntbuild: glyphs spill onto more sheets when needed");
	{
		enum { N = 200 };
		FcGlyphSource *glyphs = malloc(N * sizeof *glyphs);
		for (int i = 0; i < N; i++)
			glyphs[i] = makeGlyph((uint32_t)(0x100 + i), 24, 28);

		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(glyphs, N, &p);
		p.sheetWidth = p.sheetHeight = 128;

		uint8_t *font = NULL;
		size_t len = 0;
		CHECK(fcBcfntBuild(glyphs, N, &p, &font, &len, NULL, 0));

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(font, len, &info));
		CHECK(info.sheetCount > 1);

		free(font);
		free(glyphs);
	}

	TEST_CASE("bcfntbuild: bad input is refused rather than cropped");
	{
		FcGlyphSource g = makeGlyph('A', 10, 10);
		FcBcfntBuildParams p;
		fcBcfntBuildDefaults(&g, 1, &p);

		uint8_t *font = NULL;
		size_t len = 0;
		char err[64];

		CHECK(!fcBcfntBuild(NULL, 1, &p, &font, &len, err, sizeof err));
		CHECK(!fcBcfntBuild(&g, 0, &p, &font, &len, err, sizeof err));
		CHECK_EQ_STR(err, "nothing to build");

		FcBcfntBuildParams small = p;
		small.cellWidth = 4;
		CHECK(!fcBcfntBuild(&g, 1, &small, &font, &len, err, sizeof err));
		CHECK_EQ_STR(err, "a glyph is bigger than the cell");

		{
			FcBcfntBuildParams odd = p;
			odd.sheetWidth = 100;
			CHECK(!fcBcfntBuild(&g, 1, &odd, &font, &len, err, sizeof err));
			CHECK_EQ_STR(err, "sheet is not a multiple of the tile size");
		}

		FcGlyphSource dup[2] = { makeGlyph('A', 6, 8), makeGlyph('A', 6, 8) };
		CHECK(!fcBcfntBuild(dup, 2, &p, &font, &len, err, sizeof err));
		CHECK_EQ_STR(err, "duplicate codepoint");
	}
}
