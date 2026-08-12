#include "harness.h"

#include "data/cia.h"
#include "data/ciabuild.h"
#include "data/fontslot.h"
#include "data/romfsbuild.h"
#include "data/sha256.h"

#include <stdlib.h>
#include <string.h>

#define MEDIA_UNIT 0x200

static uint32_t rd32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint64_t rd64le(const uint8_t *p)
{
	return (uint64_t)rd32le(p) | ((uint64_t)rd32le(p + 4) << 32);
}

static uint32_t rd32be(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t alignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static uint8_t g_certs[2560];
static uint8_t g_ticket[848];
static uint8_t g_tmd[2868];

static void makeTemplates(FcCiaTemplates *t)
{
	memset(g_certs, 0xC0, sizeof g_certs);
	memset(g_ticket, 0, sizeof g_ticket);
	memset(g_tmd, 0, sizeof g_tmd);

	g_ticket[3] = 0x04; g_ticket[1] = 0x01;
	g_tmd[3]    = 0x04; g_tmd[1]    = 0x01;

	t->certs = g_certs;   t->certsLen = sizeof g_certs;
	t->ticket = g_ticket; t->ticketLen = sizeof g_ticket;
	t->tmd = g_tmd;       t->tmdLen = sizeof g_tmd;
}

void testCiaBuild(void)
{
	const uint64_t stdFont = 0x0004009B00014002ULL;

	TEST_CASE("romfsbuild: a single-file image is well formed");
	{
		const char *payload = "not really a font, but bytes are bytes";
		uint8_t *img = NULL;
		size_t len = 0;
		char err[64];

		CHECK(fcRomfsBuildSingle("cbf_std.bcfnt.lz", payload, strlen(payload),
		                           &img, &len, err, sizeof err));
		CHECK_EQ_STR(err, "");
		CHECK(img != NULL);

		CHECK(memcmp(img, "IVFC", 4) == 0);
		CHECK_EQ_INT(rd32le(img + 4), 0x00010000);

		CHECK_EQ_INT(rd32le(img + 8), 32);

		CHECK_EQ_INT(len % FC_IVFC_BLOCK, 0);

		free(img);
	}

	TEST_CASE("romfsbuild: the file name and length survive the round trip");
	{
		const char *payload = "abcdefghij";
		uint8_t *img = NULL;
		size_t len = 0;

		CHECK(fcRomfsBuildSingle("cbf_std.bcfnt.lz", payload, 10, &img, &len,
		                           NULL, 0));

		const uint8_t *l3 = img + FC_IVFC_BLOCK;
		CHECK_EQ_INT(rd32le(l3 + 0x00), 0x28);

		const uint32_t fileMetaOff = rd32le(l3 + 0x1C);
		const uint32_t fileDataOff = rd32le(l3 + 0x24);
		const uint8_t *file = l3 + fileMetaOff;

		CHECK_EQ_INT(rd64le(file + 0x10), 10);
		CHECK_EQ_INT(rd32le(file + 0x1C), 32);

		CHECK_EQ_INT(file[0x20], 'c');
		CHECK_EQ_INT(file[0x21], 0);
		CHECK_EQ_INT(file[0x22], 'b');

		CHECK(memcmp(l3 + fileDataOff, payload, 10) == 0);

		free(img);
	}

	TEST_CASE("romfsbuild: the level 3 header matches mkromfs3ds byte for byte");
	{
		const char *payload = "0123456789";
		uint8_t *img = NULL;
		size_t len = 0;
		CHECK(fcRomfsBuildSingle("cbf_std.bcfnt.lz", payload, 10, &img, &len,
		                           NULL, 0));

		const uint8_t *l3 = img + FC_IVFC_BLOCK;

		CHECK_EQ_INT(rd32le(l3 + 0x00), 0x28);
		CHECK_EQ_INT(rd32le(l3 + 0x04), 0x28);
		CHECK_EQ_INT(rd32le(l3 + 0x08), 0x0C);
		CHECK_EQ_INT(rd32le(l3 + 0x0C), 0x34);
		CHECK_EQ_INT(rd32le(l3 + 0x10), 0x18);
		CHECK_EQ_INT(rd32le(l3 + 0x14), 0x4C);
		CHECK_EQ_INT(rd32le(l3 + 0x18), 0x0C);
		CHECK_EQ_INT(rd32le(l3 + 0x1C), 0x58);
		CHECK_EQ_INT(rd32le(l3 + 0x20), 0x40);
		CHECK_EQ_INT(rd32le(l3 + 0x24), 0x98);

		CHECK_EQ_INT(rd32le(l3 + 0x28), 0);
		CHECK_EQ_INT(rd32le(l3 + 0x2C), 0xFFFFFFFF);
		CHECK_EQ_INT(rd32le(l3 + 0x30), 0xFFFFFFFF);
		CHECK_EQ_INT(rd32le(l3 + 0x4C), 0);
		CHECK_EQ_INT(rd32le(l3 + 0x50), 0xFFFFFFFF);
		CHECK_EQ_INT(rd32le(l3 + 0x54), 0xFFFFFFFF);

		free(img);
	}

	TEST_CASE("romfsbuild: bad input is refused");
	{
		uint8_t *img = NULL;
		size_t len = 0;
		char err[64];

		CHECK(!fcRomfsBuildSingle("x", "d", 0, &img, &len, err, sizeof err));
		CHECK_EQ_STR(err, "file is empty");
		CHECK(!fcRomfsBuildSingle("", "d", 1, &img, &len, err, sizeof err));
		CHECK_EQ_STR(err, "no file to store");

		char huge[128];
		memset(huge, 'a', sizeof huge);
		huge[sizeof huge - 1] = '\0';
		CHECK(!fcRomfsBuildSingle(huge, "d", 1, &img, &len, err, sizeof err));
		CHECK_EQ_STR(err, "file name is too long");
	}

	TEST_CASE("ciabuild: a built CIA passes the installer's own gate");
	{
		FcCiaTemplates t;
		makeTemplates(&t);

		const size_t fontLen = 40000;
		uint8_t *font = malloc(fontLen);
		for (size_t i = 0; i < fontLen; i++)
			font[i] = (uint8_t)(i * 31);

		uint8_t *cia = NULL;
		size_t len = 0;
		char err[96];

		CHECK(fcCiaBuildFont(stdFont, font, fontLen, "cbf_std.bcfnt.lz", &t,
		                       &cia, &len, err, sizeof err));
		CHECK_EQ_STR(err, "");

		FcCiaInfo info;
		char parseErr[64];
		CHECK(fcCiaInspect(cia, len, &info, parseErr, sizeof parseErr));
		CHECK_EQ_STR(parseErr, "");
		CHECK(info.titleId == stdFont);
		CHECK(info.ticketTitleId == stdFont);
		CHECK(fcFontSlotIsFontTitle(info.titleId));
		CHECK_EQ_INT(fcFontSlotForTitleId(info.titleId), FC_SLOT_STD);

		free(cia);
		free(font);
	}

	TEST_CASE("ciabuild: the content hash in the TMD matches the content");
	{
		FcCiaTemplates t;
		makeTemplates(&t);

		const char *font = "a small but complete payload";
		uint8_t *cia = NULL;
		size_t len = 0;

		CHECK(fcCiaBuildFont(stdFont, font, strlen(font), "cbf_std.bcfnt.lz",
		                       &t, &cia, &len, NULL, 0));

		const uint32_t certLen = rd32le(cia + 0x08);
		const uint32_t tikLen  = rd32le(cia + 0x0C);
		const uint32_t tmdLen  = rd32le(cia + 0x10);
		const uint64_t contLen = rd64le(cia + 0x18);

		const size_t certOff = alignUp(0x2020, 64);
		const size_t tikOff  = alignUp(certOff + certLen, 64);
		const size_t tmdOff  = alignUp(tikOff + tikLen, 64);
		const size_t contOff = alignUp(tmdOff + tmdLen, 64);

		CHECK(memcmp(cia + contOff + 0x100, "NCCH", 4) == 0);

		CHECK_EQ_INT(contLen % MEDIA_UNIT, 0);

		const size_t body  = 4 + 0x100 + 0x3C;
		const size_t chunk = tmdOff + body + 0xC4 + 64 * 0x24;

		CHECK_EQ_INT(rd32be(cia + chunk + 0x00), 0);
		CHECK_EQ_INT(cia[chunk + 0x07], 0);

		uint64_t chunkSize = 0;
		for (int i = 0; i < 8; i++)
			chunkSize = (chunkSize << 8) | cia[chunk + 0x08 + i];
		CHECK(chunkSize == contLen);

		uint8_t digest[FC_SHA256_SIZE];
		fcSha256(cia + contOff, (size_t)contLen, digest);
		CHECK(memcmp(cia + chunk + 0x10, digest, FC_SHA256_SIZE) == 0);

		const size_t info = tmdOff + body + 0xC4;
		uint8_t infoDigest[FC_SHA256_SIZE];
		fcSha256(cia + chunk, 0x30, infoDigest);
		CHECK(memcmp(cia + info + 0x04, infoDigest, FC_SHA256_SIZE) == 0);

		uint8_t arrayDigest[FC_SHA256_SIZE];
		fcSha256(cia + info, 64 * 0x24, arrayDigest);
		CHECK(memcmp(cia + tmdOff + body + 0xA4, arrayDigest,
		             FC_SHA256_SIZE) == 0);

		free(cia);
	}

	TEST_CASE("ciabuild: the NCCH describes its own RomFS");
	{
		FcCiaTemplates t;
		makeTemplates(&t);

		const size_t fontLen = 9000;
		uint8_t *font = calloc(1, fontLen);
		memset(font, 0x5A, fontLen);

		uint8_t *cia = NULL;
		size_t len = 0;
		CHECK(fcCiaBuildFont(stdFont, font, fontLen, "cbf_std.bcfnt.lz", &t,
		                       &cia, &len, NULL, 0));

		const size_t certOff = alignUp(0x2020, 64);
		const size_t tikOff  = alignUp(certOff + rd32le(cia + 0x08), 64);
		const size_t tmdOff  = alignUp(tikOff + rd32le(cia + 0x0C), 64);
		const size_t contOff = alignUp(tmdOff + rd32le(cia + 0x10), 64);

		const uint8_t *ncch = cia + contOff;

		CHECK_EQ_INT(ncch[0x188 + 5], 0x01);
		CHECK(ncch[0x188 + 7] & 0x04);

		const uint32_t romfsOff = rd32le(ncch + 0x1B0) * MEDIA_UNIT;
		CHECK_EQ_INT(romfsOff, MEDIA_UNIT);
		CHECK(memcmp(ncch + romfsOff, "IVFC", 4) == 0);

		uint8_t digest[FC_SHA256_SIZE];
		fcSha256(ncch + romfsOff, fcRomfsHashRegionSize(), digest);
		CHECK(memcmp(ncch + 0x1E0, digest, FC_SHA256_SIZE) == 0);

		CHECK_EQ_INT(rd32le(ncch + 0x1B8) * MEDIA_UNIT,
		             (int)fcRomfsHashRegionSize());

		free(cia);
		free(font);
	}

	TEST_CASE("ciabuild: every font slot builds its own title");
	{
		FcCiaTemplates t;
		makeTemplates(&t);

		for (int i = 0; i < FC_SLOT_COUNT; i++) {
			const FcFontSlotInfo *slot = fcFontSlotInfo(i);

			uint8_t *cia = NULL;
			size_t len = 0;
			CHECK(fcCiaBuildFont(slot->titleId, "payload", 7, slot->fileName,
			                       &t, &cia, &len, NULL, 0));

			FcCiaInfo info;
			CHECK(fcCiaInspect(cia, len, &info, NULL, 0));
			CHECK(info.titleId == slot->titleId);
			CHECK_EQ_INT(fcFontSlotForTitleId(info.titleId), i);

			free(cia);
		}
	}

	TEST_CASE("ciabuild: bad input is refused");
	{
		FcCiaTemplates t;
		makeTemplates(&t);

		uint8_t *cia = NULL;
		size_t len = 0;
		char err[96];

		CHECK(!fcCiaBuildFont(stdFont, NULL, 0, "x", &t, &cia, &len,
		                        err, sizeof err));
		CHECK_EQ_STR(err, "nothing to build");

		FcCiaTemplates empty;
		memset(&empty, 0, sizeof empty);
		CHECK(!fcCiaBuildFont(stdFont, "d", 1, "x", &empty, &cia, &len,
		                        err, sizeof err));
		CHECK_EQ_STR(err, "CIA templates are missing");

		FcCiaTemplates bad = t;
		static uint8_t badTmd[2868];
		memset(badTmd, 0, sizeof badTmd);
		badTmd[3] = 0x099;
		bad.tmd = badTmd;
		CHECK(!fcCiaBuildFont(stdFont, "d", 1, "x", &bad, &cia, &len,
		                        err, sizeof err));
		CHECK_EQ_STR(err, "metadata template is malformed");
	}
}
