#pragma once

#include "data/bcfntbuild.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	FC_COVER_ASCII,
	FC_COVER_LATIN1,
	FC_COVER_LATIN_EXT,
	FC_COVER_COUNT,
} FcCoverage;

const char *fcCoverageName(FcCoverage coverage);

int fcCoverageCodepoints(FcCoverage coverage, const uint32_t **out);

typedef struct {
	FcGlyphSource *glyphs;
	int            count;

	int            pixelSize;

	int            requested;

	int            tooBig;

	char           family[64];
	char           style[32];
	char           error[128];
} FcTtfResult;

bool fcTtfRasterise(const void *data, size_t len, int pixelSize,
                      FcCoverage coverage, FcTtfResult *out);

bool fcTtfRasteriseForCell(const void *data, size_t len,
                             int cellWidth, int cellHeight, int baseline,
                             int targetCapHeight,
                             FcCoverage coverage, FcTtfResult *out);

void fcTtfFreeGlyphs(FcTtfResult *result);

bool fcTtfToBcfnt(const void *data, size_t len, int pixelSize,
                    FcCoverage coverage, uint8_t **out, size_t *outLen,
                    char *err, size_t errSize);
