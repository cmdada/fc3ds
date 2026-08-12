#include "data/romfsbuild.h"

#include "data/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IVFC_HEADER_SIZE 0x5C
#define IVFC_MAGIC       "IVFC"
#define IVFC_VERSION     0x00010000

#define L3_HEADER_SIZE   0x28

#define ROMFS_NONE       0xFFFFFFFFu

static uint32_t hashBuckets(uint32_t entries)
{
	uint32_t count = entries;

	if (count < 3)
		count = 3;
	else if (count < 19)
		count |= 1;
	else {
		while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 ||
		       count % 7 == 0 || count % 11 == 0 || count % 13 == 0 ||
		       count % 17 == 0)
			count++;
	}

	return count;
}

static uint32_t nameHash(uint32_t parent, const char *name, size_t nameLen,
                         uint32_t buckets)
{
	uint32_t hash = parent ^ 123456789u;

	for (size_t i = 0; i < nameLen; i++) {
		hash = (hash >> 5) | (hash << 27);
		hash ^= (uint16_t)(unsigned char)name[i];
	}

	return hash % buckets;
}

static void w32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void w64(uint8_t *p, uint64_t v)
{
	w32(p, (uint32_t)v);
	w32(p + 4, (uint32_t)(v >> 32));
}

static size_t alignUp(size_t v, size_t a)
{
	return (v + a - 1) & ~(a - 1);
}

size_t fcRomfsHashRegionSize(void)
{
	return 0x200;
}

static bool fail(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return false;
}

static void hashLevel(const uint8_t *src, size_t srcLen, uint8_t *dst)
{
	const size_t blocks = (srcLen + FC_IVFC_BLOCK - 1) / FC_IVFC_BLOCK;

	for (size_t i = 0; i < blocks; i++) {
		uint8_t block[FC_IVFC_BLOCK];
		const size_t off = i * FC_IVFC_BLOCK;
		const size_t take = srcLen - off < FC_IVFC_BLOCK ? srcLen - off
		                                                 : FC_IVFC_BLOCK;

		memcpy(block, src + off, take);
		memset(block + take, 0, FC_IVFC_BLOCK - take);

		fcSha256(block, FC_IVFC_BLOCK, dst + i * FC_SHA256_SIZE);
	}
}

bool fcRomfsBuildSingle(const char *name, const void *data, size_t len,
                          uint8_t **out, size_t *outLen,
                          char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';
	if (!name || !name[0] || !out)
		return fail(err, errSize, "no file to store");

	const size_t nameLen = strlen(name);
	if (nameLen > 64)
		return fail(err, errSize, "file name is too long");
	if (len == 0)
		return fail(err, errSize, "file is empty");

	const size_t nameBytes  = nameLen * 2;
	const size_t namePadded = alignUp(nameBytes, 4);

	const uint32_t dirBuckets  = hashBuckets(1);
	const uint32_t fileBuckets = hashBuckets(1);

	const size_t dirHashSize  = dirBuckets * 4;
	const size_t fileHashSize = fileBuckets * 4;

	const size_t dirMetaSize  = 0x18;
	const size_t fileMetaSize = 0x20 + namePadded;

	const size_t dirHashOff  = L3_HEADER_SIZE;
	const size_t dirMetaOff  = dirHashOff + dirHashSize;
	const size_t fileHashOff = dirMetaOff + dirMetaSize;
	const size_t fileMetaOff = fileHashOff + fileHashSize;
	const size_t fileDataOff = fileMetaOff + fileMetaSize;

	const size_t l3Size = fileDataOff + len;

	uint8_t *l3 = calloc(1, alignUp(l3Size, FC_IVFC_BLOCK));
	if (!l3)
		return fail(err, errSize, "out of memory");

	w32(l3 + 0x00, L3_HEADER_SIZE);
	w32(l3 + 0x04, (uint32_t)dirHashOff);
	w32(l3 + 0x08, (uint32_t)dirHashSize);
	w32(l3 + 0x0C, (uint32_t)dirMetaOff);
	w32(l3 + 0x10, (uint32_t)dirMetaSize);
	w32(l3 + 0x14, (uint32_t)fileHashOff);
	w32(l3 + 0x18, (uint32_t)fileHashSize);
	w32(l3 + 0x1C, (uint32_t)fileMetaOff);
	w32(l3 + 0x20, (uint32_t)fileMetaSize);
	w32(l3 + 0x24, (uint32_t)fileDataOff);

	for (uint32_t i = 0; i < dirBuckets; i++)
		w32(l3 + dirHashOff + i * 4, ROMFS_NONE);
	for (uint32_t i = 0; i < fileBuckets; i++)
		w32(l3 + fileHashOff + i * 4, ROMFS_NONE);

	w32(l3 + dirHashOff + nameHash(0, "", 0, dirBuckets) * 4, 0);
	w32(l3 + fileHashOff + nameHash(0, name, nameLen, fileBuckets) * 4, 0);

	uint8_t *dir = l3 + dirMetaOff;
	w32(dir + 0x00, 0);
	w32(dir + 0x04, ROMFS_NONE);
	w32(dir + 0x08, ROMFS_NONE);
	w32(dir + 0x0C, 0);
	w32(dir + 0x10, ROMFS_NONE);
	w32(dir + 0x14, 0);

	uint8_t *file = l3 + fileMetaOff;
	w32(file + 0x00, 0);
	w32(file + 0x04, ROMFS_NONE);
	w64(file + 0x08, 0);
	w64(file + 0x10, (uint64_t)len);
	w32(file + 0x18, ROMFS_NONE);
	w32(file + 0x1C, (uint32_t)nameBytes);

	for (size_t i = 0; i < nameLen; i++) {
		file[0x20 + i * 2]     = (uint8_t)name[i];
		file[0x20 + i * 2 + 1] = 0;
	}

	memcpy(l3 + fileDataOff, data, len);

	const size_t l3Aligned = alignUp(l3Size, FC_IVFC_BLOCK);
	const size_t l2Size = (l3Aligned / FC_IVFC_BLOCK) * FC_SHA256_SIZE;
	const size_t l2Aligned = alignUp(l2Size, FC_IVFC_BLOCK);
	const size_t l1Size = (l2Aligned / FC_IVFC_BLOCK) * FC_SHA256_SIZE;
	const size_t l1Aligned = alignUp(l1Size, FC_IVFC_BLOCK);
	const size_t masterSize = (l1Aligned / FC_IVFC_BLOCK) * FC_SHA256_SIZE;

	const size_t l3Off = FC_IVFC_BLOCK;
	const size_t l1Off = l3Off + l3Aligned;
	const size_t l2Off = l1Off + l1Aligned;
	const size_t total = l2Off + l2Aligned;

	uint8_t *img = calloc(1, total);
	if (!img) {
		free(l3);
		return fail(err, errSize, "out of memory");
	}

	memcpy(img + l3Off, l3, l3Aligned);
	free(l3);

	hashLevel(img + l3Off, l3Aligned, img + l2Off);
	hashLevel(img + l2Off, l2Aligned, img + l1Off);

	memcpy(img, IVFC_MAGIC, 4);
	w32(img + 0x04, IVFC_VERSION);
	w32(img + 0x08, (uint32_t)masterSize);

	w64(img + 0x0C, 0);
	w64(img + 0x14, (uint64_t)l1Size);
	w32(img + 0x1C, 12);
	w32(img + 0x20, 0);

	w64(img + 0x24, (uint64_t)l1Aligned);
	w64(img + 0x2C, (uint64_t)l2Size);
	w32(img + 0x34, 12);
	w32(img + 0x38, 0);

	w64(img + 0x3C, (uint64_t)(l1Aligned + l2Aligned));
	w64(img + 0x44, (uint64_t)l3Size);
	w32(img + 0x4C, 12);
	w32(img + 0x50, 0);

	w32(img + 0x54, IVFC_HEADER_SIZE);
	w32(img + 0x58, 0);

	hashLevel(img + l1Off, l1Aligned, img + alignUp(IVFC_HEADER_SIZE, 0x10));

	*out = img;
	if (outLen)
		*outLen = total;
	return true;
}
