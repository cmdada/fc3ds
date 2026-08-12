#include "data/cia.h"

#include <stdio.h>
#include <string.h>

#define CIA_HEADER_SIZE 0x2020
#define CIA_ALIGN       64

static uint32_t rd32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint64_t rd64le(const uint8_t *p)
{
	return (uint64_t)rd32le(p) | ((uint64_t)rd32le(p + 4) << 32);
}

static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static uint32_t rd32be(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t rd64be(const uint8_t *p)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return v;
}

static size_t alignUp(size_t v)
{
	return (v + (CIA_ALIGN - 1)) & ~(size_t)(CIA_ALIGN - 1);
}

static size_t signatureSize(uint32_t type)
{
	switch (type) {
	case 0x00010000:
	case 0x00010003:
		return 0x200 + 0x3C;
	case 0x00010001:
	case 0x00010004:
		return 0x100 + 0x3C;
	case 0x00010002:
	case 0x00010005:
		return 0x3C + 0x40;
	default:
		return 0;
	}
}

static bool fail(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return false;
}

bool fcCiaInspect(const uint8_t *data, size_t len, FcCiaInfo *out,
                    char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';

	if (!data || !out)
		return fail(err, errSize, "no data");

	memset(out, 0, sizeof *out);

	if (len < CIA_HEADER_SIZE)
		return fail(err, errSize, "too small to be a CIA");

	if (rd32le(data) != CIA_HEADER_SIZE)
		return fail(err, errSize, "not a CIA");

	out->certChainSize = rd32le(data + 0x08);
	out->ticketSize    = rd32le(data + 0x0C);
	out->tmdSize       = rd32le(data + 0x10);
	out->metaSize      = rd32le(data + 0x14);
	out->contentSize   = rd64le(data + 0x18);

	if (out->ticketSize == 0 || out->tmdSize == 0)
		return fail(err, errSize, "CIA has no ticket or TMD");

	const size_t certOff   = alignUp(CIA_HEADER_SIZE);
	const size_t ticketOff = alignUp(certOff + out->certChainSize);
	const size_t tmdOff    = alignUp(ticketOff + out->ticketSize);
	const size_t contentOff= alignUp(tmdOff + out->tmdSize);

	out->expectedSize = alignUp(contentOff + (size_t)out->contentSize) +
	                    out->metaSize;

	if (tmdOff + out->tmdSize > len)
		return fail(err, errSize, "CIA is truncated");

	const uint8_t *ticket = data + ticketOff;
	const size_t tkSig = signatureSize(rd32be(ticket));
	if (tkSig == 0)
		return fail(err, errSize, "unknown ticket signature");

	const size_t tkTitleId = 4 + tkSig + 0x9C;
	if (ticketOff + tkTitleId + 8 > len)
		return fail(err, errSize, "ticket is truncated");

	out->ticketTitleId = rd64be(ticket + tkTitleId);

	const uint8_t *tmd = data + tmdOff;
	const size_t tmdSig = signatureSize(rd32be(tmd));
	if (tmdSig == 0)
		return fail(err, errSize, "unknown TMD signature");

	const size_t tmdBody = 4 + tmdSig;
	if (tmdOff + tmdBody + 0x9E > len)
		return fail(err, errSize, "TMD is truncated");

	out->titleId      = rd64be(tmd + tmdBody + 0x4C);
	out->titleVersion = rd16be(tmd + tmdBody + 0x9C);

	if (out->titleId != out->ticketTitleId)
		return fail(err, errSize, "ticket and TMD disagree on the title");

	return true;
}

void fcCiaFormatTitleId(uint64_t titleId, char *dst, size_t size)
{
	if (!dst || size == 0)
		return;

	snprintf(dst, size, "%08lX%08lX",
	         (unsigned long)(titleId >> 32),
	         (unsigned long)(titleId & 0xFFFFFFFFULL));
}
