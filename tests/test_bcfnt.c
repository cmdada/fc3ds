#include "harness.h"

#include "data/bcfnt.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static size_t buildFont(uint8_t *buf, size_t cap, const char *magic)
{
	const size_t tglpStart = 0x34;
	const size_t total     = tglpStart + 8 + 0x18;

	if (cap < total)
		return 0;

	memset(buf, 0, total);

	memcpy(buf, magic, 4);
	put16(buf + 4, 0xFEFF);
	put16(buf + 6, 0x10);
	put32(buf + 8, 0x03000000);
	put32(buf + 12, (uint32_t)total);
	put32(buf + 16, 2);

	uint8_t *finf = buf + 0x14;
	memcpy(finf, "FINF", 4);
	put32(finf + 4, 0x20);
	finf[0x09] = 30;
	finf[0x0F] = 1;
	finf[0x1C] = 29;
	finf[0x1D] = 22;
	finf[0x1E] = 24;
	put32(finf + 0x10, (uint32_t)(tglpStart + 8));

	uint8_t *tglp = buf + tglpStart;
	memcpy(tglp, "TGLP", 4);
	put32(tglp + 4, 0x20);
	uint8_t *body = tglp + 8;
	body[0x00] = 24;
	body[0x01] = 30;
	body[0x02] = 24;
	body[0x03] = 23;
	put32(body + 0x04, 0x10000);
	put16(body + 0x08, 4);
	put16(body + 0x0A, 11);
	put16(body + 0x10, 256);
	put16(body + 0x12, 512);

	return total;
}

void testBcfnt(void)
{
	uint8_t buf[256];

	TEST_CASE("bcfnt: a well-formed font parses completely");
	{
		const size_t len = buildFont(buf, sizeof buf, "CFNU");
		CHECK(len > 0);

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(buf, len, &info));
		CHECK_EQ_STR(info.magic, "CFNU");
		CHECK_EQ_INT(info.version, 0x03000000);
		CHECK_EQ_INT(info.blockCount, 2);

		CHECK(info.haveFinf);
		CHECK_EQ_INT(info.height, 29);
		CHECK_EQ_INT(info.width, 22);
		CHECK_EQ_INT(info.ascent, 24);
		CHECK_EQ_INT(info.lineFeed, 30);

		CHECK(info.haveTglp);
		CHECK_EQ_INT(info.cellWidth, 24);
		CHECK_EQ_INT(info.cellHeight, 30);
		CHECK_EQ_INT(info.sheetCount, 4);
		CHECK_EQ_INT(info.sheetWidth, 256);
		CHECK_EQ_INT(info.sheetHeight, 512);
		CHECK_EQ_STR(fcBcfntFormatName(info.sheetFormat), "A4");

		char desc[64];
		fcBcfntDescribe(&info, desc, sizeof desc);
		CHECK_EQ_STR(desc, "24x30 cells - 4 sheets - A4");
	}

	TEST_CASE("bcfnt: both magics are accepted");
	{
		const size_t len = buildFont(buf, sizeof buf, "CFNT");
		FcBcfntInfo info;
		CHECK(fcBcfntInspect(buf, len, &info));
		CHECK_EQ_STR(info.magic, "CFNT");
	}

	TEST_CASE("bcfnt: non-fonts are rejected");
	{
		FcBcfntInfo info;

		const uint8_t html[] = "<!DOCTYPE html><html><body>404";
		CHECK(!fcBcfntLooksLikeFont(html, sizeof html));
		CHECK(!fcBcfntInspect(html, sizeof html, &info));

		const uint8_t compressed[] = { 0x11, 0x40, 0x00, 0x00, 0x00 };
		CHECK(!fcBcfntLooksLikeFont(compressed, sizeof compressed));

		CHECK(!fcBcfntLooksLikeFont(NULL, 0));
		CHECK(!fcBcfntLooksLikeFont((const uint8_t *)"CF", 2));
	}

	TEST_CASE("bcfnt: a big-endian font is refused");
	{
		const size_t len = buildFont(buf, sizeof buf, "CFNT");
		put16(buf + 4, 0xFFFE);

		FcBcfntInfo info;
		CHECK(!fcBcfntInspect(buf, len, &info));
	}

	TEST_CASE("bcfnt: a font whose blocks do not resolve still describes itself");
	{
		const size_t len = buildFont(buf, sizeof buf, "CFNT");

		put32(buf + 0x14 + 0x10, 0x7FFFFFFF);

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(buf, len, &info));
		CHECK(info.haveFinf);
		CHECK(!info.haveTglp);

		char desc[64];
		fcBcfntDescribe(&info, desc, sizeof desc);
		CHECK_EQ_STR(desc, "CFNT font");
	}

	TEST_CASE("bcfnt: truncation past the header is survivable");
	{
		const size_t len = buildFont(buf, sizeof buf, "CFNT");
		CHECK(len > 0x20);

		FcBcfntInfo info;
		CHECK(fcBcfntInspect(buf, 0x20, &info));
		CHECK(!info.haveFinf);
		CHECK(!info.haveTglp);
	}
}
