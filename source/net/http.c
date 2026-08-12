#include "net/http.h"

#include <3ds.h>
#include <curl/curl.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000

#define CA_BUNDLE_PATH "romfs:/cacert.pem"
#define USER_AGENT     "Font Changer/0.1 (Nintendo 3DS)"

#define DEFAULT_TIMEOUT_SEC 45L
#define CONNECT_TIMEOUT_SEC 20L

static u32   *s_socBuf;
static CURL  *s_curl;
static char  *s_caData;
static size_t s_caLen;
static int    s_caCount;
static bool   s_ready;

void fcHttpBufInit(FcHttpBuf *buf, size_t limit)
{
	buf->data  = NULL;
	buf->len   = 0;
	buf->cap   = 0;
	buf->limit = limit;
}

void fcHttpBufFree(FcHttpBuf *buf)
{
	free(buf->data);
	fcHttpBufInit(buf, buf->limit);
}

size_t fcHttpBufSink(const void *data, size_t len, void *user)
{
	FcHttpBuf *buf = (FcHttpBuf *)user;

	if (buf->limit && buf->len + len > buf->limit)
		return 0;

	if (buf->len + len + 1 > buf->cap) {
		size_t cap = buf->cap ? buf->cap : 4096;
		while (cap < buf->len + len + 1)
			cap *= 2;
		char *grown = realloc(buf->data, cap);
		if (!grown)
			return 0;
		buf->data = grown;
		buf->cap  = cap;
	}

	memcpy(buf->data + buf->len, data, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
	return len;
}

typedef struct {
	const FcHttpRequest *req;
	FcHttpResponse      *res;
	uint64_t               startMs;
	bool                   sawFirstByte;
} XferState;

static size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	XferState *st  = (XferState *)userdata;
	size_t     len = size * nmemb;

	if (!st->sawFirstByte) {
		st->sawFirstByte      = true;
		st->res->handshakeMs  = osGetTime() - st->startMs;
	}

	size_t taken = st->req->sink(ptr, len, st->req->user);
	st->res->bytes += taken;
	return taken;
}

static const char *headerValue(const char *line, size_t len, const char *name)
{
	size_t nlen = strlen(name);
	if (len < nlen + 1 || strncasecmp(line, name, nlen) != 0 || line[nlen] != ':')
		return NULL;

	const char *v = line + nlen + 1;
	while (*v == ' ' || *v == '\t')
		v++;
	return v;
}

static void copyTrimmed(char *dst, size_t dstSize, const char *src)
{
	size_t n = 0;
	while (src[n] && src[n] != '\r' && src[n] != '\n' && n + 1 < dstSize) {
		dst[n] = src[n];
		n++;
	}
	dst[n] = '\0';
}

static size_t headerCb(char *buffer, size_t size, size_t nitems, void *userdata)
{
	XferState *st  = (XferState *)userdata;
	size_t     len = size * nitems;
	const char *v;

	if ((v = headerValue(buffer, len, "ETag")) != NULL)
		copyTrimmed(st->res->etag, sizeof st->res->etag, v);
	else if ((v = headerValue(buffer, len, "Last-Modified")) != NULL)
		copyTrimmed(st->res->lastModified, sizeof st->res->lastModified, v);

	return len;
}

static bool loadCaBundle(void)
{
	FILE *f = fopen(CA_BUNDLE_PATH, "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 1024 * 1024) {
		fclose(f);
		return false;
	}

	s_caData = malloc((size_t)size + 1);
	if (!s_caData) {
		fclose(f);
		return false;
	}

	s_caLen = fread(s_caData, 1, (size_t)size, f);
	fclose(f);
	s_caData[s_caLen] = '\0';

	s_caCount = 0;
	for (const char *p = s_caData;
	     (p = strstr(p, "-----BEGIN CERTIFICATE-----")) != NULL;
	     p += 27)
		s_caCount++;

	return s_caLen > 0 && s_caCount > 0;
}

bool fcHttpInit(void)
{
	if (s_ready)
		return true;

	s_socBuf = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
	if (!s_socBuf)
		return false;

	if (R_FAILED(socInit(s_socBuf, SOC_BUFFERSIZE))) {
		free(s_socBuf);
		s_socBuf = NULL;
		return false;
	}

	if (!loadCaBundle())
		goto fail;

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
		goto fail;

	s_curl = curl_easy_init();
	if (!s_curl) {
		curl_global_cleanup();
		goto fail;
	}

	s_ready = true;
	return true;

fail:
	free(s_caData);
	s_caData = NULL;
	socExit();
	free(s_socBuf);
	s_socBuf = NULL;
	return false;
}

void fcHttpExit(void)
{
	if (s_curl) {
		curl_easy_cleanup(s_curl);
		s_curl = NULL;
		curl_global_cleanup();
	}
	free(s_caData);
	s_caData  = NULL;
	s_caLen   = 0;
	s_caCount = 0;

	if (s_socBuf) {
		socExit();
		free(s_socBuf);
		s_socBuf = NULL;
	}
	s_ready = false;
}

bool fcHttpReady(void)
{
	return s_ready;
}

int fcHttpTrustAnchorCount(void)
{
	return s_caCount;
}

bool fcHttpFetch(const FcHttpRequest *req, FcHttpResponse *res)
{
	memset(res, 0, sizeof *res);

	if (!s_ready) {
		snprintf(res->error, sizeof res->error, "network not initialised");
		return false;
	}
	if (!req->url || !req->sink) {
		snprintf(res->error, sizeof res->error, "malformed request");
		return false;
	}

	XferState st = { .req = req, .res = res, .startMs = osGetTime() };

	curl_easy_reset(s_curl);
	curl_easy_setopt(s_curl, CURLOPT_URL, req->url);
	curl_easy_setopt(s_curl, CURLOPT_USERAGENT,
	                 req->userAgent ? req->userAgent : USER_AGENT);
	curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, writeCb);
	curl_easy_setopt(s_curl, CURLOPT_WRITEDATA, &st);
	curl_easy_setopt(s_curl, CURLOPT_HEADERFUNCTION, headerCb);
	curl_easy_setopt(s_curl, CURLOPT_HEADERDATA, &st);
	curl_easy_setopt(s_curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(s_curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(s_curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(s_curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SEC);
	curl_easy_setopt(s_curl, CURLOPT_TIMEOUT,
	                 req->timeoutSec > 0 ? req->timeoutSec : DEFAULT_TIMEOUT_SEC);

	curl_easy_setopt(s_curl, CURLOPT_ACCEPT_ENCODING, "");

	curl_easy_setopt(s_curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(s_curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(s_curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

	struct curl_blob ca = {
		.data  = s_caData,
		.len   = s_caLen,
		.flags = CURL_BLOB_NOCOPY,
	};
	curl_easy_setopt(s_curl, CURLOPT_CAINFO_BLOB, &ca);

	struct curl_slist *headers = NULL;
	char etagHdr[224], modHdr[128];

	if (req->etag && req->etag[0]) {
		snprintf(etagHdr, sizeof etagHdr, "If-None-Match: %s", req->etag);
		headers = curl_slist_append(headers, etagHdr);
	}
	if (req->lastModified && req->lastModified[0]) {
		snprintf(modHdr, sizeof modHdr, "If-Modified-Since: %s", req->lastModified);
		headers = curl_slist_append(headers, modHdr);
	}
	if (headers)
		curl_easy_setopt(s_curl, CURLOPT_HTTPHEADER, headers);

	CURLcode rc = curl_easy_perform(s_curl);

	curl_easy_getinfo(s_curl, CURLINFO_RESPONSE_CODE, &res->status);
	res->curlCode  = (int)rc;
	res->elapsedMs = osGetTime() - st.startMs;

	if (headers)
		curl_slist_free_all(headers);

	if (rc != CURLE_OK) {
		if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE)
			snprintf(res->error, sizeof res->error,
			         "certificate rejected (%s) - check the system clock",
			         curl_easy_strerror(rc));
		else
			snprintf(res->error, sizeof res->error, "%s", curl_easy_strerror(rc));
		return false;
	}

	res->notModified = (res->status == 304);
	res->ok          = (res->status >= 200 && res->status < 300) || res->notModified;

	if (!res->ok)
		snprintf(res->error, sizeof res->error, "HTTP %ld", res->status);

	return res->ok;
}

bool fcHttpFetchMem(const char *url, FcHttpBuf *buf, FcHttpResponse *res)
{
	FcHttpRequest req = {
		.url  = url,
		.sink = fcHttpBufSink,
		.user = buf,
	};
	return fcHttpFetch(&req, res);
}
