#include "net/cache.h"
#include "net/sntp.h"

#include <3ds.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CACHE_MAGIC   0x314E4150u
#define REPLAY_CHUNK  8192

typedef struct {
	uint32_t magic;
	uint32_t bodyLen;
	int64_t  fetchedAt;
	char     etag[160];
	char     lastModified[64];
} CacheHeader;

static void cacheKeyFor(const char *url, char *dst, size_t size)
{
	uint64_t h = 1469598103934665603ULL;
	for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
		h ^= *p;
		h *= 1099511628211ULL;
	}
	snprintf(dst, size, FC_CACHE_DIR "/%016llx.bin", (unsigned long long)h);
}

bool fcCacheInit(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/fc3ds", 0777);
	return mkdir(FC_CACHE_DIR, 0777) == 0 || errno == EEXIST;
}

static bool cacheReadHeader(const char *path, CacheHeader *hdr, FILE **outFile)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	if (fread(hdr, 1, sizeof *hdr, f) != sizeof *hdr || hdr->magic != CACHE_MAGIC) {
		fclose(f);
		return false;
	}

	if (outFile)
		*outFile = f;
	else
		fclose(f);
	return true;
}

static bool cacheReplayFile(const char *path, FcHttpSink sink, void *user,
                            CacheHeader *hdrOut)
{
	CacheHeader hdr;
	FILE *f = NULL;

	if (!cacheReadHeader(path, &hdr, &f))
		return false;

	char *chunk = malloc(REPLAY_CHUNK);
	if (!chunk) {
		fclose(f);
		return false;
	}

	size_t remaining = hdr.bodyLen;
	bool ok = true;

	while (remaining > 0) {
		size_t want = remaining < REPLAY_CHUNK ? remaining : REPLAY_CHUNK;
		size_t got  = fread(chunk, 1, want, f);
		if (got == 0) {
			ok = false;
			break;
		}
		if (sink(chunk, got, user) != got) {
			ok = false;
			break;
		}
		remaining -= got;
	}

	free(chunk);
	fclose(f);

	if (ok && hdrOut)
		*hdrOut = hdr;
	return ok;
}

typedef struct {
	FcHttpSink sink;
	void        *user;
	FILE        *file;
	size_t       written;
	bool         failed;
} TeeState;

static size_t teeSink(const void *data, size_t len, void *user)
{
	TeeState *tee = (TeeState *)user;

	if (tee->file && fwrite(data, 1, len, tee->file) != len) {
		fclose(tee->file);
		tee->file   = NULL;
		tee->failed = true;
	} else if (tee->file) {
		tee->written += len;
	}

	return tee->sink(data, len, tee->user);
}

bool fcCacheReplay(const char *url, FcHttpSink sink, void *user,
                     FcCacheResult *out)
{
	char path[256];
	cacheKeyFor(url, path, sizeof path);

	CacheHeader hdr;
	memset(out, 0, sizeof *out);

	if (!cacheReplayFile(path, sink, user, &hdr)) {
		out->status = FC_CACHE_MISS;
		snprintf(out->error, sizeof out->error, "nothing cached");
		return false;
	}

	out->status    = FC_CACHE_STALE;
	out->fetchedAt = (time_t)hdr.fetchedAt;
	out->bytes     = hdr.bodyLen;
	snprintf(out->etag, sizeof out->etag, "%s", hdr.etag);
	snprintf(out->lastModified, sizeof out->lastModified, "%s", hdr.lastModified);
	return true;
}

bool fcCacheFetch(const char *url, FcHttpSink sink, void *user,
                    FcCacheResult *out)
{
	char path[256], tmpPath[272];
	cacheKeyFor(url, path, sizeof path);
	snprintf(tmpPath, sizeof tmpPath, "%s.tmp", path);

	memset(out, 0, sizeof *out);

	CacheHeader stored;
	bool haveStored = cacheReadHeader(path, &stored, NULL);

	FILE *tmp = fopen(tmpPath, "wb");
	if (tmp) {
		CacheHeader blank;
		memset(&blank, 0, sizeof blank);
		fwrite(&blank, 1, sizeof blank, tmp);
	}

	TeeState tee = { .sink = sink, .user = user, .file = tmp };

	FcHttpRequest req = {
		.url          = url,
		.sink         = teeSink,
		.user         = &tee,
		.etag         = haveStored ? stored.etag : NULL,
		.lastModified = haveStored ? stored.lastModified : NULL,
	};

	FcHttpResponse res;
	bool ok = fcHttpFetch(&req, &res);
	out->elapsedMs = res.elapsedMs;

	if (ok && res.notModified) {
		if (tee.file)
			fclose(tee.file);
		remove(tmpPath);

		CacheHeader hdr;
		if (cacheReplayFile(path, sink, user, &hdr)) {
			out->status    = FC_CACHE_VALIDATED;
			out->fetchedAt = (time_t)hdr.fetchedAt;
			out->bytes     = hdr.bodyLen;
			snprintf(out->etag, sizeof out->etag, "%s", hdr.etag);
			snprintf(out->lastModified, sizeof out->lastModified, "%s", hdr.lastModified);
			return true;
		}

		fcCacheEvict(url);
		out->status = FC_CACHE_MISS;
		snprintf(out->error, sizeof out->error, "cache body missing");
		return false;
	}

	if (ok && !tee.failed && tee.file) {
		CacheHeader hdr;
		memset(&hdr, 0, sizeof hdr);
		hdr.magic     = CACHE_MAGIC;
		hdr.bodyLen   = (uint32_t)tee.written;
		hdr.fetchedAt = (int64_t)fcNow();
		snprintf(hdr.etag, sizeof hdr.etag, "%s", res.etag);
		snprintf(hdr.lastModified, sizeof hdr.lastModified, "%s", res.lastModified);

		fseek(tee.file, 0, SEEK_SET);
		fwrite(&hdr, 1, sizeof hdr, tee.file);
		fclose(tee.file);
		tee.file = NULL;

		remove(path);
		if (rename(tmpPath, path) != 0)
			remove(tmpPath);

		out->status    = FC_CACHE_FRESH;
		out->fetchedAt = (time_t)hdr.fetchedAt;
		out->bytes     = tee.written;
		snprintf(out->etag, sizeof out->etag, "%s", res.etag);
		snprintf(out->lastModified, sizeof out->lastModified, "%s", res.lastModified);
		return true;
	}

	if (tee.file)
		fclose(tee.file);
	remove(tmpPath);

	if (ok) {
		out->status    = FC_CACHE_FRESH;
		out->fetchedAt = fcNow();
		out->bytes     = res.bytes;
		return true;
	}

	CacheHeader hdr;
	if (cacheReplayFile(path, sink, user, &hdr)) {
		out->status    = FC_CACHE_STALE;
		out->fetchedAt = (time_t)hdr.fetchedAt;
		out->bytes     = hdr.bodyLen;
		snprintf(out->error, sizeof out->error, "%s", res.error);
		return true;
	}

	out->status = FC_CACHE_MISS;
	snprintf(out->error, sizeof out->error, "%s", res.error);
	return false;
}

void fcCacheEvict(const char *url)
{
	if (url) {
		char path[256];
		cacheKeyFor(url, path, sizeof path);
		remove(path);
		return;
	}

	DIR *d = opendir(FC_CACHE_DIR);
	if (!d)
		return;

	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		char path[512];
		snprintf(path, sizeof path, FC_CACHE_DIR "/%s", e->d_name);
		remove(path);
	}
	closedir(d);
}

void fcCacheFormatAge(time_t fetchedAt, time_t now, char *dst, size_t size)
{
	if (fetchedAt <= 0) {
		snprintf(dst, size, "never");
		return;
	}

	long secs = (long)(now - fetchedAt);
	if (secs < 0)
		secs = 0;

	if (secs < 60)
		snprintf(dst, size, "just now");
	else if (secs < 3600)
		snprintf(dst, size, "%ld min ago", secs / 60);
	else if (secs < 86400)
		snprintf(dst, size, "%ld hr ago", secs / 3600);
	else
		snprintf(dst, size, "%ld days ago", secs / 86400);
}
