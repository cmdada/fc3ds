#include "data/lz11.h"

#include <stdlib.h>
#include <string.h>

#define LZ11_TYPE 0x11

static size_t headerLen(const uint8_t *d, size_t len, size_t *outSize)
{
	if (len < 4 || d[0] != LZ11_TYPE)
		return 0;

	size_t size = (size_t)d[1] | ((size_t)d[2] << 8) | ((size_t)d[3] << 16);
	if (size != 0) {
		*outSize = size;
		return 4;
	}

	if (len < 8)
		return 0;

	size = (size_t)d[4] | ((size_t)d[5] << 8) | ((size_t)d[6] << 16) |
	       ((size_t)d[7] << 24);
	if (size == 0)
		return 0;

	*outSize = size;
	return 8;
}

bool fcLz11IsCompressed(const uint8_t *data, size_t len)
{
	size_t size = 0;
	return data && headerLen(data, len, &size) != 0;
}

bool fcLz11DecompressedSize(const uint8_t *data, size_t len, size_t *out)
{
	if (!data || !out)
		return false;

	size_t size = 0;
	if (headerLen(data, len, &size) == 0)
		return false;

	*out = size;
	return true;
}

bool fcLz11Decompress(const uint8_t *src, size_t srcLen,
                        uint8_t *dst, size_t dstCap, size_t *written)
{
	if (!src || !dst)
		return false;

	size_t want = 0;
	const size_t hdr = headerLen(src, srcLen, &want);
	if (hdr == 0 || want > dstCap)
		return false;

	size_t in = hdr, out = 0;

	while (out < want) {
		if (in >= srcLen)
			return false;

		uint8_t flags = src[in++];

		for (int bit = 0; bit < 8 && out < want; bit++, flags = (uint8_t)(flags << 1)) {
			if (!(flags & 0x80)) {
				if (in >= srcLen)
					return false;
				dst[out++] = src[in++];
				continue;
			}

			if (in >= srcLen)
				return false;

			const uint8_t b0 = src[in];
			const uint8_t kind = (uint8_t)(b0 >> 4);
			size_t length, disp;

			if (kind == 0) {
				if (in + 3 > srcLen)
					return false;
				length = (size_t)(((b0 & 0x0F) << 4) | (src[in + 1] >> 4)) + 0x11;
				disp   = (size_t)(((src[in + 1] & 0x0F) << 8) | src[in + 2]) + 1;
				in += 3;
			} else if (kind == 1) {
				if (in + 4 > srcLen)
					return false;
				length = (size_t)(((b0 & 0x0F) << 12) | (src[in + 1] << 4) |
				                  (src[in + 2] >> 4)) + 0x111;
				disp   = (size_t)(((src[in + 2] & 0x0F) << 8) | src[in + 3]) + 1;
				in += 4;
			} else {
				if (in + 2 > srcLen)
					return false;
				length = (size_t)kind + 1;
				disp   = (size_t)(((b0 & 0x0F) << 8) | src[in + 1]) + 1;
				in += 2;
			}

			if (disp > out)
				return false;
			if (length > want - out)
				length = want - out;

			for (size_t i = 0; i < length; i++, out++)
				dst[out] = dst[out - disp];
		}
	}

	if (written)
		*written = out;
	return true;
}

size_t fcLz11CompressBound(size_t srcLen)
{
	return srcLen + (srcLen + 7) / 8 + 16;
}

#define MIN_MATCH   3
#define MAX_MATCH   0x10110
#define HASH_BITS   15
#define HASH_SIZE   (1 << HASH_BITS)
#define MAX_CHAIN   32

typedef struct {
	int32_t head[HASH_SIZE];
	int32_t prev[FC_LZ11_WINDOW];
} Matcher;

static uint32_t hash3(const uint8_t *p)
{
	return (uint32_t)(((p[0] << 10) ^ (p[1] << 5) ^ p[2]) & (HASH_SIZE - 1));
}

static void matcherInsert(Matcher *m, const uint8_t *src, size_t pos)
{
	const uint32_t h = hash3(src + pos);
	m->prev[pos & (FC_LZ11_WINDOW - 1)] = m->head[h];
	m->head[h] = (int32_t)pos;
}

static size_t findMatch(Matcher *m, const uint8_t *src, size_t srcLen,
                        size_t pos, size_t *outDisp)
{
	if (pos + MIN_MATCH > srcLen)
		return 0;

	const size_t limit = pos > FC_LZ11_WINDOW ? pos - FC_LZ11_WINDOW : 0;
	size_t maxLen = srcLen - pos;
	if (maxLen > MAX_MATCH)
		maxLen = MAX_MATCH;

	size_t bestLen = 0, bestDisp = 0;
	int32_t cand = m->head[hash3(src + pos)];

	for (int chain = 0; chain < MAX_CHAIN && cand >= 0; chain++) {
		const size_t cpos = (size_t)cand;
		if (cpos < limit)
			break;

		if (bestLen == 0 || src[cpos + bestLen] == src[pos + bestLen]) {
			size_t len = 0;
			while (len < maxLen && src[cpos + len] == src[pos + len])
				len++;

			if (len > bestLen) {
				bestLen  = len;
				bestDisp = pos - cpos;
				if (len == maxLen)
					break;
			}
		}

		cand = m->prev[cpos & (FC_LZ11_WINDOW - 1)];
	}

	if (bestLen < MIN_MATCH)
		return 0;

	*outDisp = bestDisp;
	return bestLen;
}

bool fcLz11Compress(const uint8_t *src, size_t srcLen,
                      uint8_t *dst, size_t dstCap, size_t *written)
{
	if (!src || !dst || srcLen == 0)
		return false;

	Matcher *m = malloc(sizeof *m);
	if (!m)
		return false;
	memset(m->head, 0xFF, sizeof m->head);
	memset(m->prev, 0xFF, sizeof m->prev);

	size_t out = 0;

	if (srcLen < 0x1000000) {
		if (dstCap < 4) { free(m); return false; }
		dst[out++] = LZ11_TYPE;
		dst[out++] = (uint8_t)(srcLen & 0xFF);
		dst[out++] = (uint8_t)((srcLen >> 8) & 0xFF);
		dst[out++] = (uint8_t)((srcLen >> 16) & 0xFF);
	} else {
		if (dstCap < 8) { free(m); return false; }
		dst[out++] = LZ11_TYPE;
		dst[out++] = 0;
		dst[out++] = 0;
		dst[out++] = 0;
		dst[out++] = (uint8_t)(srcLen & 0xFF);
		dst[out++] = (uint8_t)((srcLen >> 8) & 0xFF);
		dst[out++] = (uint8_t)((srcLen >> 16) & 0xFF);
		dst[out++] = (uint8_t)((srcLen >> 24) & 0xFF);
	}

	size_t pos = 0;

	while (pos < srcLen) {
		uint8_t token[8 * 4];
		size_t  tokenLen = 0;
		uint8_t flags = 0;

		for (int bit = 0; bit < 8 && pos < srcLen; bit++) {
			size_t disp = 0;
			const size_t len = findMatch(m, src, srcLen, pos, &disp);

			if (len >= MIN_MATCH) {
				flags |= (uint8_t)(0x80 >> bit);

				const size_t d = disp - 1;
				if (len >= 0x111) {
					const size_t l = len - 0x111;
					token[tokenLen++] = (uint8_t)(0x10 | ((l >> 12) & 0x0F));
					token[tokenLen++] = (uint8_t)((l >> 4) & 0xFF);
					token[tokenLen++] = (uint8_t)(((l & 0x0F) << 4) | ((d >> 8) & 0x0F));
					token[tokenLen++] = (uint8_t)(d & 0xFF);
				} else if (len >= 0x11) {
					const size_t l = len - 0x11;
					token[tokenLen++] = (uint8_t)((l >> 4) & 0x0F);
					token[tokenLen++] = (uint8_t)(((l & 0x0F) << 4) | ((d >> 8) & 0x0F));
					token[tokenLen++] = (uint8_t)(d & 0xFF);
				} else {
					token[tokenLen++] = (uint8_t)(((len - 1) << 4) | ((d >> 8) & 0x0F));
					token[tokenLen++] = (uint8_t)(d & 0xFF);
				}

				for (size_t i = 0; i < len; i++) {
					if (pos + i + MIN_MATCH <= srcLen)
						matcherInsert(m, src, pos + i);
				}
				pos += len;
			} else {
				token[tokenLen++] = src[pos];
				if (pos + MIN_MATCH <= srcLen)
					matcherInsert(m, src, pos);
				pos++;
			}
		}

		if (out + 1 + tokenLen > dstCap) {
			free(m);
			return false;
		}
		dst[out++] = flags;
		memcpy(dst + out, token, tokenLen);
		out += tokenLen;
	}

	free(m);

	if (written)
		*written = out;
	return true;
}
