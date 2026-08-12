#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	char     magic[5];
	uint32_t version;
	uint32_t fileSize;
	uint32_t blockCount;

	bool     haveFinf;
	uint8_t  lineFeed;
	uint8_t  height;
	uint8_t  width;
	uint8_t  ascent;
	uint8_t  encoding;

	bool     haveTglp;
	uint8_t  cellWidth;
	uint8_t  cellHeight;
	uint8_t  baseline;
	uint8_t  maxCharWidth;
	uint16_t sheetCount;
	uint16_t sheetFormat;
	uint16_t sheetWidth;
	uint16_t sheetHeight;
	uint32_t sheetSize;
} FcBcfntInfo;

bool fcBcfntLooksLikeFont(const uint8_t *data, size_t len);

bool fcBcfntInspect(const uint8_t *data, size_t len, FcBcfntInfo *out);

const char *fcBcfntFormatName(uint16_t format);

void fcBcfntDescribe(const FcBcfntInfo *info, char *dst, size_t size);
