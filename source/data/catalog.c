#include "data/catalog.h"

#include "data/fontslot.h"
#include "data/gfonts.h"

#include "cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CATALOG_MAX_BYTES (512 * 1024)

static void copyStr(char *dst, size_t size, const cJSON *obj, const char *key)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(v) && v->valuestring)
		snprintf(dst, size, "%s", v->valuestring);
}

static void copyHex(char *dst, size_t size, const cJSON *obj, const char *key)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (!cJSON_IsString(v) || !v->valuestring)
		return;

	size_t n = 0;
	for (const char *p = v->valuestring; *p && n + 1 < size; p++) {
		char c = *p;
		if (c >= 'A' && c <= 'F')
			c = (char)(c - 'A' + 'a');
		dst[n++] = c;
	}
	dst[n] = '\0';
}

static void parseDownload(FcDownload *dl, const cJSON *obj, const char *key)
{
	const cJSON *node = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (!cJSON_IsObject(node))
		return;

	copyStr(dl->url, sizeof dl->url, node, "url");
	copyHex(dl->sha256, sizeof dl->sha256, node, "sha256");

	const cJSON *size = cJSON_GetObjectItemCaseSensitive(node, "size");
	if (cJSON_IsNumber(size) && size->valuedouble > 0)
		dl->size = (uint32_t)size->valuedouble;
}

static bool urlLooksFetchable(const char *url)
{
	return strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0;
}

bool fcFontEntryInstallable(const FcFontEntry *e)
{
	if (!e)
		return false;

	if (e->googleFamily[0] || e->ttf.url[0])
		return true;

	return e->cia.url[0] && urlLooksFetchable(e->cia.url);
}

bool fcFontEntryPreviewable(const FcFontEntry *e)
{
	if (!e)
		return false;

	if (e->googleFamily[0] || e->ttf.url[0])
		return true;

	return e->preview.url[0] && urlLooksFetchable(e->preview.url);
}

bool fcFontEntryFromGoogle(FcFontEntry *out, const char *family, int slot)
{
	if (!out || !family || !family[0])
		return false;
	if (strlen(family) >= sizeof out->googleFamily)
		return false;

	memset(out, 0, sizeof *out);
	out->slot = fcFontSlotInfo(slot) ? slot : FC_SLOT_STD;

	snprintf(out->googleFamily, sizeof out->googleFamily, "%s", family);
	snprintf(out->name, sizeof out->name, "%.*s",
	         (int)(sizeof out->name - 1), family);
	snprintf(out->id, sizeof out->id, "%.*s", (int)(sizeof out->id - 1), family);
	snprintf(out->author, sizeof out->author, "Google Fonts");
	snprintf(out->license, sizeof out->license, "see fonts.google.com");

	return true;
}

bool fcCatalogParse(const char *json, size_t len, FcCatalog *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);
	snprintf(out->name, sizeof out->name, "Fonts");

	if (!json || len == 0) {
		snprintf(out->error, sizeof out->error, "empty catalog");
		return false;
	}

	if (len > CATALOG_MAX_BYTES) {
		snprintf(out->error, sizeof out->error, "catalog is too large");
		return false;
	}

	cJSON *root = cJSON_ParseWithLength(json, len);
	if (!root) {
		snprintf(out->error, sizeof out->error, "catalog is not valid JSON");
		return false;
	}

	if (!cJSON_IsObject(root)) {
		snprintf(out->error, sizeof out->error, "catalog is not an object");
		cJSON_Delete(root);
		return false;
	}

	copyStr(out->name, sizeof out->name, root, "name");

	const cJSON *fonts = cJSON_GetObjectItemCaseSensitive(root, "fonts");
	if (!cJSON_IsArray(fonts)) {
		snprintf(out->error, sizeof out->error, "catalog has no font list");
		cJSON_Delete(root);
		return false;
	}

	const cJSON *node = NULL;
	cJSON_ArrayForEach(node, fonts) {
		if (out->count >= FC_CATALOG_MAX)
			break;
		if (!cJSON_IsObject(node))
			continue;

		FcFontEntry entry;
		memset(&entry, 0, sizeof entry);
		entry.slot = FC_SLOT_STD;

		copyStr(entry.id, sizeof entry.id, node, "id");
		copyStr(entry.name, sizeof entry.name, node, "name");
		copyStr(entry.author, sizeof entry.author, node, "author");
		copyStr(entry.license, sizeof entry.license, node, "license");
		copyStr(entry.notes, sizeof entry.notes, node, "notes");

		char slotId[16] = "";
		copyStr(slotId, sizeof slotId, node, "slot");
		if (slotId[0]) {
			const int slot = fcFontSlotFromId(slotId);
			if (slot < 0)
				continue;
			entry.slot = slot;
		}

		parseDownload(&entry.cia, node, "cia");
		parseDownload(&entry.preview, node, "preview");

		if (!entry.name[0])
			continue;
		if (!entry.cia.url[0] && !entry.preview.url[0])
			continue;

		if (!entry.id[0])
			snprintf(entry.id, sizeof entry.id, "%.*s",
			         (int)(sizeof entry.id - 1), entry.name);

		out->items[out->count++] = entry;
	}

	cJSON_Delete(root);

	if (out->count == 0) {
		snprintf(out->error, sizeof out->error, "catalog lists no usable fonts");
		return false;
	}

	return true;
}

bool fcFontEntryFromUrl(FcFontEntry *out, const char *url, int slot)
{
	if (!out || !url || !urlLooksFetchable(url))
		return false;

	if (strlen(url) >= sizeof out->cia.url)
		return false;

	char family[64];
	if (fcGFontsFamilyFromUrl(url, family, sizeof family))
		return fcFontEntryFromGoogle(out, family, slot);

	memset(out, 0, sizeof *out);
	out->slot = fcFontSlotInfo(slot) ? slot : FC_SLOT_STD;

	const char *tail = strrchr(url, '/');
	tail = tail ? tail + 1 : url;

	size_t n = 0;
	while (tail[n] && tail[n] != '?' && tail[n] != '#' && n + 1 < sizeof out->name)
		n++;
	memcpy(out->name, tail, n);
	out->name[n] = '\0';

	if (!out->name[0])
		snprintf(out->name, sizeof out->name, "Font from URL");

	snprintf(out->id, sizeof out->id, "url");
	snprintf(out->author, sizeof out->author, "unknown");
	snprintf(out->notes, sizeof out->notes,
	         "Added by hand. Nothing vouches for this file but the address it "
	         "came from.");

	const size_t len = strlen(url);
	const char *ext = len > 4 ? url + len - 4 : "";

	char *dst;
	if (strcmp(ext, ".cia") == 0) {
		dst = out->cia.url;
	} else if (len > 6 && strcmp(url + len - 6, ".bcfnt") == 0) {
		dst = out->preview.url;
	} else if (len > 9 && strcmp(url + len - 9, ".bcfnt.lz") == 0) {
		dst = out->preview.url;
	} else {
		dst = out->ttf.url;
	}

	memcpy(dst, url, len);
	dst[len] = '\0';

	return true;
}
