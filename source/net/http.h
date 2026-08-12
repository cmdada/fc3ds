#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef size_t (*FcHttpSink)(const void *data, size_t len, void *user);

typedef struct {
	const char  *url;
	FcHttpSink sink;
	void        *user;
	const char  *etag;
	const char  *lastModified;
	long         timeoutSec;

	const char  *userAgent;
} FcHttpRequest;

typedef struct {
	long     status;
	bool     ok;
	bool     notModified;
	char     etag[160];
	char     lastModified[64];
	size_t   bytes;
	int      curlCode;
	char     error[192];
	uint64_t elapsedMs;
	uint64_t handshakeMs;
} FcHttpResponse;

typedef struct {
	char  *data;
	size_t len;
	size_t cap;
	size_t limit;
} FcHttpBuf;

bool fcHttpInit(void);
void fcHttpExit(void);

bool fcHttpReady(void);

bool fcHttpFetch(const FcHttpRequest *req, FcHttpResponse *res);

bool fcHttpFetchMem(const char *url, FcHttpBuf *buf, FcHttpResponse *res);

void fcHttpBufInit(FcHttpBuf *buf, size_t limit);
void fcHttpBufFree(FcHttpBuf *buf);

size_t fcHttpBufSink(const void *data, size_t len, void *user);

int fcHttpTrustAnchorCount(void);
