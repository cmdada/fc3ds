#include "data/sha256.h"

#include <string.h>

static const uint32_t kRoundConstants[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

static void transform(FcSha256 *ctx, const uint8_t *block)
{
	uint32_t w[64];

	for (int i = 0; i < 16; i++)
		w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];

	for (int i = 16; i < 64; i++) {
		const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2],
	         d = ctx->state[3], e = ctx->state[4], f = ctx->state[5],
	         g = ctx->state[6], h = ctx->state[7];

	for (int i = 0; i < 64; i++) {
		const uint32_t s1    = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
		const uint32_t ch    = (e & f) ^ (~e & g);
		const uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
		const uint32_t s0    = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
		const uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
		const uint32_t temp2 = s0 + maj;

		h = g; g = f; f = e;
		e = d + temp1;
		d = c; c = b; b = a;
		a = temp1 + temp2;
	}

	ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
	ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
	ctx->state[6] += g; ctx->state[7] += h;
}

void fcSha256Init(FcSha256 *ctx)
{
	ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
	ctx->bitCount = 0;
	ctx->blockLen = 0;
}

void fcSha256Update(FcSha256 *ctx, const void *data, size_t len)
{
	const uint8_t *p = data;

	ctx->bitCount += (uint64_t)len * 8;

	while (len > 0) {
		const size_t room = sizeof ctx->block - ctx->blockLen;
		const size_t take = len < room ? len : room;

		memcpy(ctx->block + ctx->blockLen, p, take);
		ctx->blockLen += take;
		p   += take;
		len -= take;

		if (ctx->blockLen == sizeof ctx->block) {
			transform(ctx, ctx->block);
			ctx->blockLen = 0;
		}
	}
}

void fcSha256Final(FcSha256 *ctx, uint8_t out[FC_SHA256_SIZE])
{
	const uint64_t bits = ctx->bitCount;

	ctx->block[ctx->blockLen++] = 0x80;

	if (ctx->blockLen > 56) {
		memset(ctx->block + ctx->blockLen, 0, sizeof ctx->block - ctx->blockLen);
		transform(ctx, ctx->block);
		ctx->blockLen = 0;
	}
	memset(ctx->block + ctx->blockLen, 0, 56 - ctx->blockLen);

	for (int i = 0; i < 8; i++)
		ctx->block[56 + i] = (uint8_t)(bits >> (56 - i * 8));
	transform(ctx, ctx->block);

	for (int i = 0; i < 8; i++) {
		out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
		out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
	}
}

void fcSha256(const void *data, size_t len, uint8_t out[FC_SHA256_SIZE])
{
	FcSha256 ctx;
	fcSha256Init(&ctx);
	fcSha256Update(&ctx, data, len);
	fcSha256Final(&ctx, out);
}

void fcSha256Hex(const uint8_t digest[FC_SHA256_SIZE], char *dst, size_t size)
{
	static const char kHex[] = "0123456789abcdef";

	if (!dst || size == 0)
		return;

	size_t n = 0;
	for (int i = 0; i < FC_SHA256_SIZE && n + 2 < size; i++) {
		dst[n++] = kHex[digest[i] >> 4];
		dst[n++] = kHex[digest[i] & 0x0F];
	}
	dst[n] = '\0';
}

bool fcSha256MatchesHex(const uint8_t digest[FC_SHA256_SIZE], const char *expected)
{
	if (!expected || strlen(expected) != FC_SHA256_HEX_LEN)
		return false;

	char got[FC_SHA256_HEX_LEN + 1];
	fcSha256Hex(digest, got, sizeof got);

	for (int i = 0; i < FC_SHA256_HEX_LEN; i++) {
		char e = expected[i];
		if (e >= 'A' && e <= 'F')
			e = (char)(e - 'A' + 'a');
		if (got[i] != e)
			return false;
	}

	return true;
}
