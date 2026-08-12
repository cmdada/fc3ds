#include "harness.h"

#include "data/sha256.h"

#include <stdlib.h>
#include <string.h>

static const char *hashOf(const char *s)
{
	static char hex[FC_SHA256_HEX_LEN + 1];
	uint8_t digest[FC_SHA256_SIZE];

	fcSha256(s, strlen(s), digest);
	fcSha256Hex(digest, hex, sizeof hex);
	return hex;
}

void testSha256(void)
{
	TEST_CASE("sha256: NIST vectors");
	CHECK_EQ_STR(hashOf(""),
	             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK_EQ_STR(hashOf("abc"),
	             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_EQ_STR(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
	             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	TEST_CASE("sha256: spans block boundaries when fed in pieces");
	{
		char buf[1000];
		memset(buf, 'a', sizeof buf);

		uint8_t whole[FC_SHA256_SIZE], streamed[FC_SHA256_SIZE];
		fcSha256(buf, sizeof buf, whole);

		FcSha256 ctx;
		fcSha256Init(&ctx);
		size_t off = 0;
		for (size_t chunk = 1; off < sizeof buf; chunk = chunk * 3 + 1) {
			size_t take = chunk;
			if (take > sizeof buf - off)
				take = sizeof buf - off;
			fcSha256Update(&ctx, buf + off, take);
			off += take;
		}
		fcSha256Final(&ctx, streamed);

		CHECK(memcmp(whole, streamed, FC_SHA256_SIZE) == 0);
	}

	TEST_CASE("sha256: length spilling into an extra block");
	{
		for (size_t len = 54; len <= 66; len++) {
			char buf[80];
			memset(buf, 'x', len);

			uint8_t whole[FC_SHA256_SIZE], split[FC_SHA256_SIZE];
			fcSha256(buf, len, whole);

			FcSha256 ctx;
			fcSha256Init(&ctx);
			fcSha256Update(&ctx, buf, len / 2);
			fcSha256Update(&ctx, buf + len / 2, len - len / 2);
			fcSha256Final(&ctx, split);

			CHECK(memcmp(whole, split, FC_SHA256_SIZE) == 0);
		}
	}

	TEST_CASE("sha256: hex comparison is case-insensitive and strict");
	{
		uint8_t digest[FC_SHA256_SIZE];
		fcSha256("abc", 3, digest);

		CHECK(fcSha256MatchesHex(digest,
		        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
		CHECK(fcSha256MatchesHex(digest,
		        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));

		CHECK(!fcSha256MatchesHex(digest, ""));
		CHECK(!fcSha256MatchesHex(digest, NULL));
		CHECK(!fcSha256MatchesHex(digest, "ba7816bf"));
		CHECK(!fcSha256MatchesHex(digest,
		        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae"));
	}
}
