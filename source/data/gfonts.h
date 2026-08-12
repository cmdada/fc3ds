#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FC_GFONTS_PATH "romfs:/googlefonts.tsv"

#define FC_GFONTS_UA "Mozilla/5.0 (Linux; U; Android 4.0; en-us)"

typedef struct {
	char family[64];
	char category[16];
} FcGFontFamily;

typedef struct {
	FcGFontFamily *items;
	int            count;
	char           error[128];
} FcGFontList;

bool fcGFontsParse(const char *text, size_t len, FcGFontList *out);

bool fcGFontsLoad(const char *path, FcGFontList *out);

void fcGFontsFree(FcGFontList *list);

int fcGFontsSearch(const FcGFontList *list, const char *query,
                     int *out, int maxOut);

bool fcGFontsCssUrl(const char *family, int weight, char *dst, size_t size);

bool fcGFontsTtfUrlFromCss(const char *css, char *dst, size_t size);

bool fcGFontsFamilyFromUrl(const char *url, char *dst, size_t size);
