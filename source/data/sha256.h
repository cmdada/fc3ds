#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FC_SHA256_SIZE    32
#define FC_SHA256_HEX_LEN 64

typedef struct {
	uint32_t state[8];
	uint64_t bitCount;
	uint8_t  block[64];
	size_t   blockLen;
} FcSha256;

void fcSha256Init(FcSha256 *ctx);
void fcSha256Update(FcSha256 *ctx, const void *data, size_t len);
void fcSha256Final(FcSha256 *ctx, uint8_t out[FC_SHA256_SIZE]);

void fcSha256(const void *data, size_t len, uint8_t out[FC_SHA256_SIZE]);

void fcSha256Hex(const uint8_t digest[FC_SHA256_SIZE], char *dst, size_t size);

bool fcSha256MatchesHex(const uint8_t digest[FC_SHA256_SIZE], const char *expected);
