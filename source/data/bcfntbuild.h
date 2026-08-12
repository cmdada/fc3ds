#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t       codepoint;
	const uint8_t *bitmap;
	uint8_t        width;
	uint8_t        height;
	int8_t         left;
	int8_t         top;
	uint8_t        advance;
} FcGlyphSource;

typedef struct {
	uint8_t  cellWidth;
	uint8_t  cellHeight;
	uint8_t  baseline;
	uint8_t  maxCharWidth;
	uint8_t  lineFeed;
	uint8_t  ascent;
	uint16_t sheetWidth;
	uint16_t sheetHeight;
} FcBcfntBuildParams;

void fcBcfntBuildDefaults(const FcGlyphSource *glyphs, int count,
                            FcBcfntBuildParams *out);

bool fcBcfntBuild(const FcGlyphSource *glyphs, int count,
                    const FcBcfntBuildParams *params,
                    uint8_t **out, size_t *outLen, char *err, size_t errSize);

size_t fcA4PixelOffset(int x, int y, int sheetWidth, bool *highNibble);
