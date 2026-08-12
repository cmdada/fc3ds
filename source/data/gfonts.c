#include "data/gfonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFONTS_MAX_BYTES (1024 * 1024)

static char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool fcGFontsParse(const char *text, size_t len, FcGFontList *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);

	if (!text || len == 0) {
		snprintf(out->error, sizeof out->error, "empty font list");
		return false;
	}

	int lines = 0;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n')
			lines++;
	}
	if (len > 0 && text[len - 1] != '\n')
		lines++;

	if (lines == 0) {
		snprintf(out->error, sizeof out->error, "font list has no entries");
		return false;
	}

	FcGFontFamily *items = calloc((size_t)lines, sizeof *items);
	if (!items) {
		snprintf(out->error, sizeof out->error, "out of memory");
		return false;
	}

	int n = 0;
	size_t i = 0;

	while (i < len && n < lines) {
		size_t end = i;
		while (end < len && text[end] != '\n')
			end++;

		size_t tab = i;
		while (tab < end && text[tab] != '\t')
			tab++;

		if (tab < end) {
			const size_t catLen = tab - i;
			const size_t famLen = end - (tab + 1);

			if (famLen > 0 && famLen < sizeof items[n].family) {
				memcpy(items[n].category, text + i,
				       catLen < sizeof items[n].category - 1
				           ? catLen : sizeof items[n].category - 1);
				memcpy(items[n].family, text + tab + 1, famLen);
				n++;
			}
		}

		i = end + 1;
	}

	if (n == 0) {
		free(items);
		snprintf(out->error, sizeof out->error, "font list has no usable rows");
		return false;
	}

	out->items = items;
	out->count = n;
	return true;
}

bool fcGFontsLoad(const char *path, FcGFontList *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);

	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(out->error, sizeof out->error, "no bundled font list");
		return false;
	}

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > GFONTS_MAX_BYTES) {
		fclose(f);
		snprintf(out->error, sizeof out->error, "font list is the wrong size");
		return false;
	}

	char *text = malloc((size_t)size);
	if (!text) {
		fclose(f);
		snprintf(out->error, sizeof out->error, "out of memory");
		return false;
	}

	const size_t got = fread(text, 1, (size_t)size, f);
	fclose(f);

	const bool ok = fcGFontsParse(text, got, out);
	free(text);
	return ok;
}

void fcGFontsFree(FcGFontList *list)
{
	if (!list)
		return;

	free(list->items);
	list->items = NULL;
	list->count = 0;
}

int fcGFontsSearch(const FcGFontList *list, const char *query,
                     int *out, int maxOut)
{
	if (!list || !out || maxOut <= 0)
		return 0;

	if (!query || !query[0]) {
		const int n = list->count < maxOut ? list->count : maxOut;
		for (int i = 0; i < n; i++)
			out[i] = i;
		return n;
	}

	char needle[64];
	size_t nl = 0;
	for (const char *p = query; *p && nl + 1 < sizeof needle; p++)
		needle[nl++] = lower(*p);
	needle[nl] = '\0';

	if (nl == 0)
		return 0;

	int n = 0;

	for (int i = 0; i < list->count && n < maxOut; i++) {
		char hay[64];
		size_t hl = 0;
		for (const char *p = list->items[i].family; *p && hl + 1 < sizeof hay; p++)
			hay[hl++] = lower(*p);
		hay[hl] = '\0';

		if (hl < nl)
			continue;

		for (size_t s = 0; s + nl <= hl; s++) {
			if (memcmp(hay + s, needle, nl) == 0) {
				out[n++] = i;
				break;
			}
		}
	}

	return n;
}

static bool unreserved(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

bool fcGFontsCssUrl(const char *family, int weight, char *dst, size_t size)
{
	if (!family || !family[0] || !dst || size == 0)
		return false;

	static const char kPrefix[] = "https://fonts.googleapis.com/css2?family=";
	static const char kHex[] = "0123456789ABCDEF";

	size_t n = 0;
	const size_t prefixLen = sizeof kPrefix - 1;
	if (prefixLen + 1 >= size)
		return false;

	memcpy(dst, kPrefix, prefixLen);
	n = prefixLen;

	for (const char *p = family; *p; p++) {
		if (n + 3 + 10 >= size)
			return false;

		if (*p == ' ') {
			dst[n++] = '+';
		} else if (unreserved(*p)) {
			dst[n++] = *p;
		} else {
			const unsigned char c = (unsigned char)*p;
			dst[n++] = '%';
			dst[n++] = kHex[c >> 4];
			dst[n++] = kHex[c & 0x0F];
		}
	}

	if (weight > 0 && weight != 400)
		n += (size_t)snprintf(dst + n, size - n, ":wght@%d", weight);
	else
		dst[n] = '\0';

	return true;
}

bool fcGFontsTtfUrlFromCss(const char *css, char *dst, size_t size)
{
	if (!css || !dst || size == 0)
		return false;

	dst[0] = '\0';

	for (const char *p = css; (p = strstr(p, "url(")) != NULL; p += 4) {
		const char *start = p + 4;

		if (*start == '\'' || *start == '"')
			start++;

		const char *end = start;
		while (*end && *end != ')' && *end != '\'' && *end != '"')
			end++;

		const size_t len = (size_t)(end - start);
		if (len == 0 || len >= size)
			continue;

		const char *tail = end;
		const char *stop = tail;
		while (*stop && *stop != ';' && *stop != '}')
			stop++;

		bool truetype = false;
		const char *fmt = strstr(tail, "format(");
		if (fmt && fmt < stop) {
			const char *q = fmt + 7;
			if (*q == '\'' || *q == '"')
				q++;

			const char *qe = q;
			while (*qe && *qe != '\'' && *qe != '"' && *qe != ')')
				qe++;

			const size_t fl = (size_t)(qe - q);
			truetype = (fl == 8 && memcmp(q, "truetype", 8) == 0) ||
			           (fl == 8 && memcmp(q, "opentype", 8) == 0);
		} else {
			truetype = len > 4 && (memcmp(end - 4, ".ttf", 4) == 0 ||
			                       memcmp(end - 4, ".otf", 4) == 0);
		}

		if (truetype) {
			memcpy(dst, start, len);
			dst[len] = '\0';
			return true;
		}
	}

	return false;
}

static int hexDigit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool fcGFontsFamilyFromUrl(const char *url, char *dst, size_t size)
{
	if (!url || !dst || size == 0)
		return false;

	dst[0] = '\0';

	const char *p = url;
	if (strncmp(p, "https://", 8) == 0)
		p += 8;
	else if (strncmp(p, "http://", 7) == 0)
		p += 7;
	else
		return false;

	if (strncmp(p, "www.", 4) == 0)
		p += 4;
	if (strncmp(p, "fonts.google.com", 16) != 0)
		return false;
	p += 16;

	if (strncmp(p, "/specimen/", 10) != 0)
		return false;
	p += 10;

	size_t n = 0;

	while (*p && *p != '/' && *p != '?' && *p != '#' && n + 1 < size) {
		if (*p == '+') {
			dst[n++] = ' ';
			p++;
			continue;
		}
		if (*p == '%') {
			const int hi = hexDigit(p[1]);
			const int lo = hi < 0 ? -1 : hexDigit(p[2]);
			if (lo < 0)
				return false;
			dst[n++] = (char)((hi << 4) | lo);
			p += 3;
			continue;
		}
		dst[n++] = *p++;
	}

	dst[n] = '\0';
	return n > 0;
}
