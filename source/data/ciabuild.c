#include "data/ciabuild.h"

#include "data/romfsbuild.h"
#include "data/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_UNIT   0x200
#define CIA_HEADER   0x2020
#define CIA_ALIGN    64

#define TMD_TITLE_ID       0x4C
#define TMD_TITLE_VERSION  0x9C
#define TMD_CONTENT_COUNT  0x9E
#define TMD_INFO_HASH      0xA4
#define TMD_INFO_RECORDS   0xC4
#define TMD_INFO_COUNT     64
#define TMD_INFO_SIZE      0x24
#define TMD_CHUNK_SIZE     0x30

#define TICKET_TITLE_ID    0x9C

static void w32le(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (i * 8));
}

static void w64le(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (i * 8));
}

static void w16be(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void w32be(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (24 - i * 8));
}

static void w64be(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (56 - i * 8));
}

static uint32_t rd32be(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t alignUp(size_t v, size_t a)
{
	return (v + a - 1) & ~(a - 1);
}

static size_t signatureSize(uint32_t type)
{
	switch (type) {
	case 0x00010000: case 0x00010003: return 0x200 + 0x3C;
	case 0x00010001: case 0x00010004: return 0x100 + 0x3C;
	case 0x00010002: case 0x00010005: return 0x3C + 0x40;
	default:                          return 0;
	}
}

static bool fail(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return false;
}

static uint8_t *readAll(const char *path, size_t *outLen)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 64 * 1024) {
		fclose(f);
		return NULL;
	}

	uint8_t *buf = malloc((size_t)size);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	const size_t got = fread(buf, 1, (size_t)size, f);
	fclose(f);

	if (got != (size_t)size) {
		free(buf);
		return NULL;
	}

	*outLen = got;
	return buf;
}

bool fcCiaTemplatesLoad(const char *dir, FcCiaTemplates *out,
                          char *err, size_t errSize)
{
	if (!out || !dir)
		return false;

	memset(out, 0, sizeof *out);

	char path[256];
	size_t len = 0;

	snprintf(path, sizeof path, "%s/certs.bin", dir);
	uint8_t *certs = readAll(path, &len);
	if (!certs)
		return fail(err, errSize, "missing CIA certificate template");
	out->certs = certs;
	out->certsLen = len;

	snprintf(path, sizeof path, "%s/ticket.bin", dir);
	uint8_t *ticket = readAll(path, &len);
	if (!ticket) {
		fcCiaTemplatesFree(out);
		return fail(err, errSize, "missing CIA ticket template");
	}
	out->ticket = ticket;
	out->ticketLen = len;

	snprintf(path, sizeof path, "%s/tmd.bin", dir);
	uint8_t *tmd = readAll(path, &len);
	if (!tmd) {
		fcCiaTemplatesFree(out);
		return fail(err, errSize, "missing CIA metadata template");
	}
	out->tmd = tmd;
	out->tmdLen = len;

	return true;
}

void fcCiaTemplatesFree(FcCiaTemplates *t)
{
	if (!t)
		return;

	free((void *)t->certs);
	free((void *)t->ticket);
	free((void *)t->tmd);
	memset(t, 0, sizeof *t);
}

static uint8_t *buildNcch(uint64_t titleId, const uint8_t *romfs, size_t romfsLen,
                          size_t *outLen)
{
	const size_t romfsOff = MEDIA_UNIT;
	const size_t total = alignUp(romfsOff + romfsLen, MEDIA_UNIT);

	uint8_t *ncch = calloc(1, total);
	if (!ncch)
		return NULL;

	memcpy(ncch + 0x100, "NCCH", 4);
	w32le(ncch + 0x104, (uint32_t)(total / MEDIA_UNIT));
	w64le(ncch + 0x108, titleId);
	memcpy(ncch + 0x110, "00", 2);
	ncch[0x112] = 2;
	w64le(ncch + 0x118, titleId);
	memcpy(ncch + 0x150, "CTR-P-CFNT", 10);

	w32le(ncch + 0x180, 0);

	ncch[0x188 + 3] = 0x00;
	ncch[0x188 + 4] = 0x01;
	ncch[0x188 + 5] = 0x01;
	ncch[0x188 + 6] = 0x00;
	ncch[0x188 + 7] = 0x01 | 0x04;

	w32le(ncch + 0x1B0, (uint32_t)(romfsOff / MEDIA_UNIT));
	w32le(ncch + 0x1B4, (uint32_t)(alignUp(romfsLen, MEDIA_UNIT) / MEDIA_UNIT));
	w32le(ncch + 0x1B8, (uint32_t)(fcRomfsHashRegionSize() / MEDIA_UNIT));

	memcpy(ncch + romfsOff, romfs, romfsLen);

	fcSha256(ncch + romfsOff, fcRomfsHashRegionSize(), ncch + 0x1E0);

	*outLen = total;
	return ncch;
}

bool fcCiaBuildFont(uint64_t titleId, const void *font, size_t fontLen,
                      const char *fileName, const FcCiaTemplates *t,
                      uint8_t **out, size_t *outLen, char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';
	if (!font || fontLen == 0 || !fileName || !t || !out)
		return fail(err, errSize, "nothing to build");
	if (!t->certs || !t->ticket || !t->tmd)
		return fail(err, errSize, "CIA templates are missing");

	uint8_t *romfs = NULL;
	size_t romfsLen = 0;
	if (!fcRomfsBuildSingle(fileName, font, fontLen, &romfs, &romfsLen,
	                          err, errSize))
		return false;

	size_t ncchLen = 0;
	uint8_t *ncch = buildNcch(titleId, romfs, romfsLen, &ncchLen);
	free(romfs);
	if (!ncch)
		return fail(err, errSize, "out of memory");

	uint8_t contentHash[FC_SHA256_SIZE];
	fcSha256(ncch, ncchLen, contentHash);

	uint8_t *ticket = malloc(t->ticketLen);
	if (!ticket) {
		free(ncch);
		return fail(err, errSize, "out of memory");
	}
	memcpy(ticket, t->ticket, t->ticketLen);

	const size_t tkSig = signatureSize(rd32be(ticket));
	if (tkSig == 0 || 4 + tkSig + TICKET_TITLE_ID + 8 > t->ticketLen) {
		free(ticket);
		free(ncch);
		return fail(err, errSize, "ticket template is malformed");
	}
	w64be(ticket + 4 + tkSig + TICKET_TITLE_ID, titleId);

	const size_t tmdSig = signatureSize(rd32be(t->tmd));
	const size_t tmdBody = 4 + tmdSig;
	const size_t chunkOff = tmdBody + TMD_INFO_RECORDS +
	                        TMD_INFO_COUNT * TMD_INFO_SIZE;

	if (tmdSig == 0 || chunkOff + TMD_CHUNK_SIZE > t->tmdLen) {
		free(ticket);
		free(ncch);
		return fail(err, errSize, "metadata template is malformed");
	}

	const size_t tmdLen = chunkOff + TMD_CHUNK_SIZE;
	uint8_t *tmd = calloc(1, tmdLen);
	if (!tmd) {
		free(ticket);
		free(ncch);
		return fail(err, errSize, "out of memory");
	}
	memcpy(tmd, t->tmd, tmdLen);

	uint8_t *body = tmd + tmdBody;
	w64be(body + TMD_TITLE_ID, titleId);
	w16be(body + TMD_TITLE_VERSION, 0);
	w16be(body + TMD_CONTENT_COUNT, 1);

	uint8_t *chunk = tmd + chunkOff;
	memset(chunk, 0, TMD_CHUNK_SIZE);
	w32be(chunk + 0x00, 0);
	w16be(chunk + 0x04, 0);
	w16be(chunk + 0x06, 0);
	w64be(chunk + 0x08, (uint64_t)ncchLen);
	memcpy(chunk + 0x10, contentHash, FC_SHA256_SIZE);

	uint8_t *info = body + TMD_INFO_RECORDS;
	memset(info, 0, TMD_INFO_COUNT * TMD_INFO_SIZE);
	w16be(info + 0x00, 0);
	w16be(info + 0x02, 1);
	fcSha256(chunk, TMD_CHUNK_SIZE, info + 0x04);

	fcSha256(info, TMD_INFO_COUNT * TMD_INFO_SIZE, body + TMD_INFO_HASH);

	const size_t certOff    = alignUp(CIA_HEADER, CIA_ALIGN);
	const size_t ticketOff  = alignUp(certOff + t->certsLen, CIA_ALIGN);
	const size_t tmdOff     = alignUp(ticketOff + t->ticketLen, CIA_ALIGN);
	const size_t contentOff = alignUp(tmdOff + tmdLen, CIA_ALIGN);
	const size_t total      = alignUp(contentOff + ncchLen, CIA_ALIGN);

	uint8_t *cia = calloc(1, total);
	if (!cia) {
		free(tmd);
		free(ticket);
		free(ncch);
		return fail(err, errSize, "out of memory");
	}

	w32le(cia + 0x00, CIA_HEADER);
	w32le(cia + 0x08, (uint32_t)t->certsLen);
	w32le(cia + 0x0C, (uint32_t)t->ticketLen);
	w32le(cia + 0x10, (uint32_t)tmdLen);
	w32le(cia + 0x14, 0);
	w64le(cia + 0x18, (uint64_t)ncchLen);

	cia[0x20] = 0x80;

	memcpy(cia + certOff, t->certs, t->certsLen);
	memcpy(cia + ticketOff, ticket, t->ticketLen);
	memcpy(cia + tmdOff, tmd, tmdLen);
	memcpy(cia + contentOff, ncch, ncchLen);

	free(tmd);
	free(ticket);
	free(ncch);

	*out = cia;
	if (outLen)
		*outLen = total;
	return true;
}
