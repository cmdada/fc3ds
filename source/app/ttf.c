#include "app/ttf.h"

#include "app/log.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t first, last; } Range;

static const Range kAscii[] = {
	{ 0x0020, 0x007E },
};

static const Range kLatin1[] = {
	{ 0x0020, 0x007E },
	{ 0x00A0, 0x00FF },
};

static const Range kLatinExt[] = {
	{ 0x0020, 0x007E },
	{ 0x00A0, 0x00FF },
	{ 0x0100, 0x017F },
	{ 0x2010, 0x201F },
	{ 0x2020, 0x2022 },
	{ 0x20AC, 0x20AC },
};

static const struct {
	const char  *name;
	const Range *ranges;
	int          count;
} kCoverage[FC_COVER_COUNT] = {
	{ "ASCII",          kAscii,    (int)(sizeof kAscii / sizeof kAscii[0]) },
	{ "Latin-1",        kLatin1,   (int)(sizeof kLatin1 / sizeof kLatin1[0]) },
	{ "Latin extended", kLatinExt, (int)(sizeof kLatinExt / sizeof kLatinExt[0]) },
};

const char *fcCoverageName(FcCoverage coverage)
{
	if ((unsigned)coverage >= (unsigned)FC_COVER_COUNT)
		return "?";
	return kCoverage[coverage].name;
}

int fcCoverageCodepoints(FcCoverage coverage, const uint32_t **out)
{
	static uint32_t s_points[1024];
	static int      s_count;
	static FcCoverage s_for = FC_COVER_COUNT;

	if ((unsigned)coverage >= (unsigned)FC_COVER_COUNT) {
		if (out)
			*out = NULL;
		return 0;
	}

	if (s_for != coverage) {
		s_count = 0;
		for (int r = 0; r < kCoverage[coverage].count; r++) {
			const Range *range = &kCoverage[coverage].ranges[r];
			for (uint32_t cp = range->first;
			     cp <= range->last && s_count < (int)(sizeof s_points / sizeof s_points[0]);
			     cp++)
				s_points[s_count++] = cp;
		}
		s_for = coverage;
	}

	if (out)
		*out = s_points;
	return s_count;
}

static bool rasteriseFail(FcTtfResult *out, const char *msg)
{
	snprintf(out->error, sizeof out->error, "%s", msg);
	return false;
}

static int sizeForCell(FT_Face face, int cellHeight, int baseline)
{
	int size = cellHeight;

	for (int attempt = 0; attempt < 4 && size > 6; attempt++) {
		if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size) != 0)
			return 0;

		const int asc  = (int)(face->size->metrics.ascender >> 6);
		const int desc = (int)(-face->size->metrics.descender >> 6);

		if (asc <= baseline && asc + desc <= cellHeight)
			return size;

		int byAscent = asc > 0 ? size * baseline / asc : size;
		int byHeight = (asc + desc) > 0 ? size * cellHeight / (asc + desc) : size;
		int next = byAscent < byHeight ? byAscent : byHeight;

		if (next >= size)
			next = size - 1;
		size = next;
	}

	return size > 6 ? size : 0;
}

static int sizeForCapHeight(FT_Face face, int targetCap,
                            int cellWidth, int cellHeight, int baseline)
{
	FT_UInt gi = FT_Get_Char_Index(face, 'H');
	if (gi == 0)
		gi = FT_Get_Char_Index(face, 'X');
	if (gi == 0)
		return 0;

	const int trial = cellHeight > 8 ? cellHeight : 8;
	if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)trial) != 0 ||
	    FT_Load_Glyph(face, gi, FT_LOAD_RENDER) != 0)
		return 0;

	const int trialCap = face->glyph->bitmap_top;
	if (trialCap <= 0)
		return 0;

	const int estimate = (trial * targetCap + trialCap / 2) / trialCap;

	int best = 0, bestErr = 0;

	for (int size = estimate - 2; size <= estimate + 2; size++) {
		if (size < 6 || size > 64)
			continue;
		if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size) != 0 ||
		    FT_Load_Glyph(face, gi, FT_LOAD_RENDER) != 0)
			continue;

		if (face->glyph->bitmap_top > baseline ||
		    (int)face->glyph->bitmap.rows > cellHeight ||
		    (int)face->glyph->bitmap.width > cellWidth)
			continue;

		int err = face->glyph->bitmap_top - targetCap;
		if (err < 0)
			err = -err;

		if (best == 0 || err < bestErr || (err == bestErr && size > best)) {
			best = size;
			bestErr = err;
		}
	}

	return best;
}

bool fcTtfRasteriseForCell(const void *data, size_t len,
                             int cellWidth, int cellHeight, int baseline,
                             int targetCapHeight,
                             FcCoverage coverage, FcTtfResult *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);

	if (cellWidth <= 0 || cellHeight <= 0 || baseline <= 0)
		return rasteriseFail(out, "the target font has no usable cell");

	FT_Library lib = NULL;
	if (FT_Init_FreeType(&lib) != 0)
		return rasteriseFail(out, "FreeType would not start");

	FT_Face face = NULL;
	if (FT_New_Memory_Face(lib, data, (FT_Long)len, 0, &face) != 0) {
		FT_Done_FreeType(lib);
		return rasteriseFail(out, "not a font FreeType can read");
	}

	int size = 0;
	if (targetCapHeight > 0)
		size = sizeForCapHeight(face, targetCapHeight, cellWidth, cellHeight,
		                        baseline);
	if (size == 0)
		size = sizeForCell(face, cellHeight, baseline);

	FT_Done_Face(face);
	FT_Done_FreeType(lib);

	if (size == 0)
		return rasteriseFail(out, "the face will not fit the system font's cell");

	if (!fcTtfRasterise(data, len, size, coverage, out))
		return false;

	int kept = 0;
	for (int i = 0; i < out->count; i++) {
		if (out->glyphs[i].width <= cellWidth &&
		    out->glyphs[i].height <= cellHeight &&
		    out->glyphs[i].top <= baseline) {
			out->glyphs[kept++] = out->glyphs[i];
		} else {
			free((void *)out->glyphs[i].bitmap);
			out->tooBig++;
		}
	}
	out->count = kept;

	if (out->count == 0)
		return rasteriseFail(out, "no glyph from that face fits the cell");

	FC_LOG("fitted %d glyphs at %dpx into a %dx%d cell (%d too big)",
	       out->count, size, cellWidth, cellHeight, out->tooBig);
	return true;
}

bool fcTtfRasterise(const void *data, size_t len, int pixelSize,
                      FcCoverage coverage, FcTtfResult *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);

	if (!data || len == 0)
		return rasteriseFail(out, "no font data");
	if (pixelSize < 6 || pixelSize > 64)
		return rasteriseFail(out, "pixel size is out of range");

	FT_Library lib = NULL;
	if (FT_Init_FreeType(&lib) != 0)
		return rasteriseFail(out, "FreeType would not start");

	FT_Face face = NULL;
	if (FT_New_Memory_Face(lib, data, (FT_Long)len, 0, &face) != 0) {
		FT_Done_FreeType(lib);
		return rasteriseFail(out, "not a font FreeType can read");
	}

	if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixelSize) != 0) {
		FT_Done_Face(face);
		FT_Done_FreeType(lib);
		return rasteriseFail(out, "face will not scale to that size");
	}

	snprintf(out->family, sizeof out->family, "%s",
	         face->family_name ? face->family_name : "unknown");
	snprintf(out->style, sizeof out->style, "%s",
	         face->style_name ? face->style_name : "");
	out->pixelSize = pixelSize;

	const uint32_t *points = NULL;
	const int wanted = fcCoverageCodepoints(coverage, &points);
	out->requested = wanted;

	FcGlyphSource *glyphs = calloc((size_t)wanted, sizeof *glyphs);
	if (!glyphs) {
		FT_Done_Face(face);
		FT_Done_FreeType(lib);
		return rasteriseFail(out, "out of memory");
	}

	int n = 0;

	for (int i = 0; i < wanted; i++) {
		const FT_UInt index = FT_Get_Char_Index(face, points[i]);
		if (index == 0)
			continue;

		if (FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
			continue;

		const FT_GlyphSlot slot = face->glyph;
		const FT_Bitmap *bmp = &slot->bitmap;

		if (bmp->width > 255 || bmp->rows > 255)
			continue;

		FcGlyphSource *g = &glyphs[n];
		g->codepoint = points[i];
		g->width     = (uint8_t)bmp->width;
		g->height    = (uint8_t)bmp->rows;
		g->left      = (int8_t)(slot->bitmap_left < -128 ? -128
		                        : slot->bitmap_left > 127 ? 127
		                                                  : slot->bitmap_left);
		g->top       = (int8_t)(slot->bitmap_top < -128 ? -128
		                        : slot->bitmap_top > 127 ? 127
		                                                 : slot->bitmap_top);
		g->advance   = (uint8_t)((slot->advance.x >> 6) > 255 ? 255
		                                                      : (slot->advance.x >> 6));

		if (bmp->width > 0 && bmp->rows > 0) {
			uint8_t *copy = malloc((size_t)bmp->width * bmp->rows);
			if (!copy)
				continue;

			for (unsigned y = 0; y < bmp->rows; y++) {
				const unsigned char *src = bmp->buffer + (int)y * bmp->pitch;
				memcpy(copy + (size_t)y * bmp->width, src, bmp->width);
			}
			g->bitmap = copy;
		} else {
			g->bitmap = NULL;
			g->width  = 0;
			g->height = 0;
		}

		n++;
	}

	FT_Done_Face(face);
	FT_Done_FreeType(lib);

	if (n == 0) {
		free(glyphs);
		return rasteriseFail(out, "the face carries none of these characters");
	}

	out->glyphs = glyphs;
	out->count  = n;

	FC_LOG("rasterised %d glyphs from %s at %dpx", n, out->family, pixelSize);
	return true;
}

void fcTtfFreeGlyphs(FcTtfResult *result)
{
	if (!result || !result->glyphs)
		return;

	for (int i = 0; i < result->count; i++)
		free((void *)result->glyphs[i].bitmap);

	free(result->glyphs);
	result->glyphs = NULL;
	result->count  = 0;
}

bool fcTtfToBcfnt(const void *data, size_t len, int pixelSize,
                    FcCoverage coverage, uint8_t **out, size_t *outLen,
                    char *err, size_t errSize)
{
	FcTtfResult res;

	if (!fcTtfRasterise(data, len, pixelSize, coverage, &res)) {
		if (err && errSize)
			snprintf(err, errSize, "%s", res.error);
		return false;
	}

	static const uint8_t empty = 0;
	for (int i = 0; i < res.count; i++) {
		if (!res.glyphs[i].bitmap)
			res.glyphs[i].bitmap = &empty;
	}

	FcBcfntBuildParams params;
	fcBcfntBuildDefaults(res.glyphs, res.count, &params);

	const bool ok = fcBcfntBuild(res.glyphs, res.count, &params, out, outLen,
	                               err, errSize);

	for (int i = 0; i < res.count; i++) {
		if (res.glyphs[i].bitmap == &empty)
			res.glyphs[i].bitmap = NULL;
	}
	fcTtfFreeGlyphs(&res);

	return ok;
}
