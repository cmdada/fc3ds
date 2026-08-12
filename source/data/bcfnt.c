#include "data/bcfnt.h"

#include <stdio.h>
#include <string.h>

#define CFNT_HEADER_SIZE 0x14
#define FINF_OFFSET      0x14
#define FINF_SIZE        0x20
#define BLOCK_HEADER     8

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

bool fcBcfntLooksLikeFont(const uint8_t *data, size_t len)
{
	if (!data || len < CFNT_HEADER_SIZE)
		return false;

	return memcmp(data, "CFNT", 4) == 0 || memcmp(data, "CFNU", 4) == 0;
}

bool fcBcfntInspect(const uint8_t *data, size_t len, FcBcfntInfo *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);

	if (!fcBcfntLooksLikeFont(data, len))
		return false;

	memcpy(out->magic, data, 4);
	out->magic[4] = '\0';

	if (rd16(data + 4) != 0xFEFF)
		return false;

	out->version    = rd32(data + 8);
	out->fileSize   = rd32(data + 12);
	out->blockCount = rd32(data + 16);

	if (len < FINF_OFFSET + FINF_SIZE)
		return true;
	if (memcmp(data + FINF_OFFSET, "FINF", 4) != 0)
		return true;

	const uint8_t *finf = data + FINF_OFFSET;
	out->haveFinf = true;
	out->lineFeed = finf[0x09];
	out->encoding = finf[0x0F];
	out->height   = finf[0x1C];
	out->width    = finf[0x1D];
	out->ascent   = finf[0x1E];

	const uint32_t tglpOffset = rd32(finf + 0x10);
	if (tglpOffset < BLOCK_HEADER)
		return true;

	const size_t tglpStart = (size_t)tglpOffset - BLOCK_HEADER;
	if (tglpStart + BLOCK_HEADER + 0x18 > len)
		return true;
	if (memcmp(data + tglpStart, "TGLP", 4) != 0)
		return true;

	const uint8_t *tglp = data + tglpStart + BLOCK_HEADER;
	out->haveTglp     = true;
	out->cellWidth    = tglp[0x00];
	out->cellHeight   = tglp[0x01];
	out->baseline     = tglp[0x02];
	out->maxCharWidth = tglp[0x03];
	out->sheetSize    = rd32(tglp + 0x04);
	out->sheetCount   = rd16(tglp + 0x08);
	out->sheetFormat  = rd16(tglp + 0x0A);
	out->sheetWidth   = rd16(tglp + 0x10);
	out->sheetHeight  = rd16(tglp + 0x12);

	return true;
}

const char *fcBcfntFormatName(uint16_t format)
{
	static const char *const kNames[] = {
		"RGBA8", "RGB8", "RGBA5551", "RGB565", "RGBA4",
		"LA8", "HILO8", "L8", "A8", "LA4", "L4", "A4",
		"ETC1", "ETC1A4",
	};

	if (format < sizeof kNames / sizeof kNames[0])
		return kNames[format];
	return "?";
}

void fcBcfntDescribe(const FcBcfntInfo *info, char *dst, size_t size)
{
	if (!dst || size == 0)
		return;

	if (!info || !info->haveTglp) {
		snprintf(dst, size, "%s font", info && info->magic[0] ? info->magic : "BCFNT");
		return;
	}

	snprintf(dst, size, "%ux%u cells - %u sheet%s - %s",
	         info->cellWidth, info->cellHeight,
	         info->sheetCount, info->sheetCount == 1 ? "" : "s",
	         fcBcfntFormatName(info->sheetFormat));
}
