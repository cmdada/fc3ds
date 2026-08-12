#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_CATALOG_MAX 64

typedef struct {
	char     url[256];
	uint32_t size;
	char     sha256[65];
} FcDownload;

typedef struct FcFontEntry {
	char id[40];
	char name[64];
	char author[64];
	char license[40];
	char notes[192];

	int  slot;

	FcDownload cia;
	FcDownload preview;

	FcDownload ttf;

	char googleFamily[64];
} FcFontEntry;

typedef struct {
	char        name[64];
	int         count;
	FcFontEntry items[FC_CATALOG_MAX];
	char        error[128];
} FcCatalog;

bool fcFontEntryInstallable(const FcFontEntry *e);

bool fcFontEntryPreviewable(const FcFontEntry *e);

bool fcCatalogParse(const char *json, size_t len, FcCatalog *out);

bool fcFontEntryFromUrl(FcFontEntry *out, const char *url, int slot);

bool fcFontEntryFromGoogle(FcFontEntry *out, const char *family, int slot);
