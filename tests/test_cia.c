#include "harness.h"

#include "data/cia.h"
#include "data/fontslot.h"

#include <stdlib.h>
#include <string.h>

#define CERT_SIZE   0x40
#define TICKET_SIZE 0x350
#define TMD_SIZE    0x300
#define CONTENT     0x400

#define SIG_TYPE 0x00010004u
#define BODY_OFF 0x140u

static void put32le(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (i * 8));
}

static void put64le(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (i * 8));
}

static void put32be(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (24 - i * 8));
}

static void put64be(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (56 - i * 8));
}

static size_t alignUp(size_t v) { return (v + 63) & ~(size_t)63; }

typedef struct {
	uint8_t *data;
	size_t   len;
	size_t   ticketOff, tmdOff;
} FakeCia;

static FakeCia buildCia(uint64_t ticketTitleId, uint64_t tmdTitleId)
{
	FakeCia c;
	memset(&c, 0, sizeof c);

	const size_t certOff    = alignUp(0x2020);
	c.ticketOff             = alignUp(certOff + CERT_SIZE);
	c.tmdOff                = alignUp(c.ticketOff + TICKET_SIZE);
	const size_t contentOff = alignUp(c.tmdOff + TMD_SIZE);

	c.len  = alignUp(contentOff + CONTENT);
	c.data = calloc(1, c.len);

	put32le(c.data + 0x00, 0x2020);
	put32le(c.data + 0x08, CERT_SIZE);
	put32le(c.data + 0x0C, TICKET_SIZE);
	put32le(c.data + 0x10, TMD_SIZE);
	put32le(c.data + 0x14, 0);
	put64le(c.data + 0x18, CONTENT);

	uint8_t *ticket = c.data + c.ticketOff;
	put32be(ticket, SIG_TYPE);
	put64be(ticket + BODY_OFF + 0x9C, ticketTitleId);

	uint8_t *tmd = c.data + c.tmdOff;
	put32be(tmd, SIG_TYPE);
	put64be(tmd + BODY_OFF + 0x4C, tmdTitleId);
	tmd[BODY_OFF + 0x9C] = 0x00;
	tmd[BODY_OFF + 0x9D] = 0x2A;

	return c;
}

void testCia(void)
{
	const uint64_t stdFont = 0x0004009B00014002ULL;

	TEST_CASE("cia: a well-formed font CIA parses");
	{
		FakeCia c = buildCia(stdFont, stdFont);

		FcCiaInfo info;
		char err[64];
		CHECK(fcCiaInspect(c.data, c.len, &info, err, sizeof err));
		CHECK_EQ_STR(err, "");
		CHECK(info.titleId == stdFont);
		CHECK(info.ticketTitleId == stdFont);
		CHECK_EQ_INT(info.titleVersion, 42);
		CHECK_EQ_INT(info.contentSize, CONTENT);
		CHECK_EQ_INT(info.tmdSize, TMD_SIZE);

		char tid[24];
		fcCiaFormatTitleId(info.titleId, tid, sizeof tid);
		CHECK_EQ_STR(tid, "0004009B00014002");

		CHECK(fcFontSlotIsFontTitle(info.titleId));
		CHECK_EQ_INT(fcFontSlotForTitleId(info.titleId), FC_SLOT_STD);

		free(c.data);
	}

	TEST_CASE("cia: every font slot maps back to its title");
	{
		const uint64_t ids[] = {
			0x0004009B00014002ULL, 0x0004009B00014102ULL,
			0x0004009B00014202ULL, 0x0004009B00014302ULL,
		};
		for (int i = 0; i < FC_SLOT_COUNT; i++) {
			const FcFontSlotInfo *slot = fcFontSlotInfo(i);
			CHECK(slot != NULL);
			CHECK(slot->titleId == ids[i]);
			CHECK_EQ_INT(fcFontSlotForTitleId(ids[i]), i);
		}

		CHECK(fcFontSlotInfo(-1) == NULL);
		CHECK(fcFontSlotInfo(FC_SLOT_COUNT) == NULL);
		CHECK_EQ_INT(fcFontSlotFromId("std"), FC_SLOT_STD);
		CHECK_EQ_INT(fcFontSlotFromId("kor"), FC_SLOT_KOR);
		CHECK_EQ_INT(fcFontSlotFromId("nope"), -1);
		CHECK_EQ_INT(fcFontSlotFromId(NULL), -1);
	}

	TEST_CASE("cia: a non-font title is recognised as one");
	{
		const uint64_t homeMenu = 0x0004003000008F02ULL;
		FakeCia c = buildCia(homeMenu, homeMenu);

		FcCiaInfo info;
		CHECK(fcCiaInspect(c.data, c.len, &info, NULL, 0));
		CHECK(info.titleId == homeMenu);
		CHECK(!fcFontSlotIsFontTitle(info.titleId));
		CHECK_EQ_INT(fcFontSlotForTitleId(info.titleId), -1);

		free(c.data);
	}

	TEST_CASE("cia: a ticket and TMD that disagree are refused");
	{
		FakeCia c = buildCia(stdFont, 0x0004003000008F02ULL);

		FcCiaInfo info;
		char err[64];
		CHECK(!fcCiaInspect(c.data, c.len, &info, err, sizeof err));
		CHECK_EQ_STR(err, "ticket and TMD disagree on the title");

		free(c.data);
	}

	TEST_CASE("cia: malformed input is refused with a reason");
	{
		FcCiaInfo info;
		char err[64];

		CHECK(!fcCiaInspect(NULL, 0, &info, err, sizeof err));

		const uint8_t stub[16] = { 0 };
		CHECK(!fcCiaInspect(stub, sizeof stub, &info, err, sizeof err));
		CHECK_EQ_STR(err, "too small to be a CIA");

		FakeCia c = buildCia(stdFont, stdFont);

		uint8_t *bad = malloc(c.len);
		memcpy(bad, c.data, c.len);
		put32le(bad, 0x1234);
		CHECK(!fcCiaInspect(bad, c.len, &info, err, sizeof err));
		CHECK_EQ_STR(err, "not a CIA");
		free(bad);

		CHECK(!fcCiaInspect(c.data, c.tmdOff + 16, &info, err, sizeof err));
		CHECK_EQ_STR(err, "CIA is truncated");

		uint8_t *sig = malloc(c.len);
		memcpy(sig, c.data, c.len);
		put32be(sig + c.tmdOff, 0x00090009);
		CHECK(!fcCiaInspect(sig, c.len, &info, err, sizeof err));
		CHECK_EQ_STR(err, "unknown TMD signature");
		free(sig);

		free(c.data);
	}

	TEST_CASE("cia: a CIA with no ticket is refused");
	{
		FakeCia c = buildCia(stdFont, stdFont);
		put32le(c.data + 0x0C, 0);

		FcCiaInfo info;
		char err[64];
		CHECK(!fcCiaInspect(c.data, c.len, &info, err, sizeof err));
		CHECK_EQ_STR(err, "CIA has no ticket or TMD");

		free(c.data);
	}
}
