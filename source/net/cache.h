#pragma once

#include "net/http.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define FC_CACHE_DIR "sdmc:/3ds/fc3ds/cache"

typedef enum {
	FC_CACHE_FRESH,
	FC_CACHE_VALIDATED,
	FC_CACHE_STALE,
	FC_CACHE_MISS,
} FcCacheStatus;

typedef struct {
	FcCacheStatus status;
	time_t          fetchedAt;
	size_t          bytes;
	char            etag[160];
	char            lastModified[64];
	char            error[192];
	uint64_t        elapsedMs;
} FcCacheResult;

bool fcCacheInit(void);

bool fcCacheFetch(const char *url, FcHttpSink sink, void *user,
                    FcCacheResult *out);

bool fcCacheReplay(const char *url, FcHttpSink sink, void *user,
                     FcCacheResult *out);

void fcCacheEvict(const char *url);

void fcCacheFormatAge(time_t fetchedAt, time_t now, char *dst, size_t size);
