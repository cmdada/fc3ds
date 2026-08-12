#include "harness.h"

#include "data/lz11.h"

#include <stdlib.h>
#include <string.h>

static bool roundTrip(const uint8_t *src, size_t len)
{
	const size_t bound = fcLz11CompressBound(len);
	uint8_t *packed = malloc(bound);
	uint8_t *back   = malloc(len ? len : 1);
	bool ok = false;

	size_t packedLen = 0, backLen = 0;

	if (packed && back &&
	    fcLz11Compress(src, len, packed, bound, &packedLen) &&
	    fcLz11Decompress(packed, packedLen, back, len, &backLen))
		ok = backLen == len && memcmp(src, back, len) == 0;

	free(packed);
	free(back);
	return ok;
}

void testLz11(void)
{
	TEST_CASE("lz11: header sniffing");
	{
		const uint8_t good[] = { 0x11, 0x10, 0x00, 0x00, 0x00 };
		const uint8_t wrong[] = { 0x10, 0x10, 0x00, 0x00, 0x00 };

		size_t size = 0;
		CHECK(fcLz11IsCompressed(good, sizeof good));
		CHECK(fcLz11DecompressedSize(good, sizeof good, &size));
		CHECK_EQ_INT(size, 0x10);

		CHECK(!fcLz11IsCompressed(wrong, sizeof wrong));
		CHECK(!fcLz11IsCompressed(good, 2));

		const uint8_t extended[] = { 0x11, 0, 0, 0, 0x00, 0x00, 0x00, 0x02 };
		CHECK(fcLz11DecompressedSize(extended, sizeof extended, &size));
		CHECK_EQ_INT(size, 0x02000000);
	}

	TEST_CASE("lz11: round trip over shapes a glyph sheet actually has");
	{
		uint8_t small[3] = { 1, 2, 3 };
		CHECK(roundTrip(small, sizeof small));

		uint8_t zeros[8192];
		memset(zeros, 0, sizeof zeros);
		CHECK(roundTrip(zeros, sizeof zeros));

		uint8_t repeating[4096];
		for (size_t i = 0; i < sizeof repeating; i++)
			repeating[i] = (uint8_t)(i % 7);
		CHECK(roundTrip(repeating, sizeof repeating));

		uint8_t incompressible[2048];
		unsigned seed = 12345;
		for (size_t i = 0; i < sizeof incompressible; i++) {
			seed = seed * 1103515245u + 12345u;
			incompressible[i] = (uint8_t)(seed >> 16);
		}
		CHECK(roundTrip(incompressible, sizeof incompressible));

		uint8_t longRun[70000];
		memset(longRun, 0xAB, sizeof longRun);
		for (size_t i = 0; i < 256; i++)
			longRun[i] = (uint8_t)i;
		CHECK(roundTrip(longRun, sizeof longRun));
	}

	TEST_CASE("lz11: compression actually compresses");
	{
		uint8_t zeros[16384];
		memset(zeros, 0, sizeof zeros);

		uint8_t *packed = malloc(fcLz11CompressBound(sizeof zeros));
		size_t packedLen = 0;
		CHECK(fcLz11Compress(zeros, sizeof zeros, packed,
		                       fcLz11CompressBound(sizeof zeros), &packedLen));
		CHECK(packedLen < sizeof zeros / 10);
		free(packed);
	}

	TEST_CASE("lz11: a corrupt stream fails rather than reading past the buffer");
	{
		uint8_t dst[64];

		const uint8_t truncated[] = { 0x11, 0x40, 0x00, 0x00 };
		CHECK(!fcLz11Decompress(truncated, sizeof truncated, dst, sizeof dst, NULL));

		const uint8_t backref[] = { 0x11, 0x40, 0x00, 0x00, 0x80, 0x20, 0x00 };
		CHECK(!fcLz11Decompress(backref, sizeof backref, dst, sizeof dst, NULL));

		const uint8_t huge[] = { 0x11, 0x00, 0x10, 0x00, 0x00 };
		CHECK(!fcLz11Decompress(huge, sizeof huge, dst, sizeof dst, NULL));

		const uint8_t plain[] = { 'C', 'F', 'N', 'T' };
		CHECK(!fcLz11Decompress(plain, sizeof plain, dst, sizeof dst, NULL));
	}

	TEST_CASE("lz11: compression refuses a destination that is too small");
	{
		uint8_t src[4096];
		memset(src, 0x5A, sizeof src);

		uint8_t tiny[8];
		CHECK(!fcLz11Compress(src, sizeof src, tiny, sizeof tiny, NULL));
	}
}
