#pragma once

#include "data/bcfntbuild.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t pointerBase;

	uint8_t  cellWidth;
	uint8_t  cellHeight;
	uint8_t  baseline;
	uint8_t  maxCharWidth;
	uint16_t sheetCount;
	uint16_t sheetWidth;
	uint16_t sheetHeight;
	uint16_t columns;
	uint16_t rows;
	uint32_t sheetSize;
	size_t   sheetDataOffset;
	int      glyphCapacity;
} FcBcfntEdit;

bool fcBcfntEditOpen(const uint8_t *font, size_t len, uint32_t pointerBase,
                       FcBcfntEdit *out, char *err, size_t errSize);

bool fcBcfntEditRebase(uint8_t *font, size_t len, uint32_t oldBase,
                         uint32_t newBase);

int fcBcfntEditFindGlyph(const uint8_t *font, size_t len,
                           const FcBcfntEdit *info, uint32_t codepoint);

bool fcBcfntEditReplaceGlyph(uint8_t *font, size_t len, const FcBcfntEdit *info,
                               int glyphIndex, const FcGlyphSource *glyph);

uint8_t *fcBcfntEditWidthEntry(uint8_t *font, size_t len,
                                 const FcBcfntEdit *info, int glyphIndex);

bool fcBcfntEditGlyphInk(const uint8_t *font, size_t len,
                           const FcBcfntEdit *info, int glyphIndex,
                           int *top, int *bottom, int *left, int *right);
