#include "app/config.h"

#include "cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const FcTimezoneChoice kTimezones[] = {
	{ "Hawaii (HST)",          -10 * 60 },
	{ "Alaska (AKDT)",          -8 * 60 },
	{ "Pacific (PDT)",          -7 * 60 },
	{ "Pacific (PST)",          -8 * 60 },
	{ "Mountain (MDT)",         -6 * 60 },
	{ "Mountain (MST)",         -7 * 60 },
	{ "Central (CDT)",          -5 * 60 },
	{ "Central (CST)",          -6 * 60 },
	{ "Eastern (EDT)",          -4 * 60 },
	{ "Eastern (EST)",          -5 * 60 },
	{ "Atlantic (ADT)",         -3 * 60 },
	{ "Sao Paulo",              -3 * 60 },
	{ "UTC",                          0 },
	{ "UK (BST)",                1 * 60 },
	{ "UK (GMT)",                     0 },
	{ "Central Europe (CEST)",   2 * 60 },
	{ "Central Europe (CET)",    1 * 60 },
	{ "Eastern Europe (EEST)",   3 * 60 },
	{ "Moscow",                  3 * 60 },
	{ "India",                   5 * 60 + 30 },
	{ "Bangkok",                 7 * 60 },
	{ "China / Singapore",       8 * 60 },
	{ "Japan / Korea",           9 * 60 },
	{ "Sydney (AEST)",          10 * 60 },
	{ "Sydney (AEDT)",          11 * 60 },
	{ "New Zealand (NZST)",     12 * 60 },
};

const FcTimezoneChoice *fcTimezoneChoices(int *count)
{
	if (count)
		*count = (int)(sizeof kTimezones / sizeof kTimezones[0]);
	return kTimezones;
}

int fcTimezoneIndexFor(int minutes)
{
	const int n = (int)(sizeof kTimezones / sizeof kTimezones[0]);
	for (int i = 0; i < n; i++) {
		if (kTimezones[i].minutes == minutes)
			return i;
	}
	return -1;
}

void fcFormatUtcOffset(int minutes, char *dst, size_t size)
{
	const char sign = minutes < 0 ? '-' : '+';
	const int mag = minutes < 0 ? -minutes : minutes;
	snprintf(dst, size, "UTC%c%02d:%02d", sign, mag / 60, mag % 60);
}

void fcConfigDefaults(FcConfig *cfg)
{
	memset(cfg, 0, sizeof *cfg);

	cfg->utcOffsetMinutes = 0;
	cfg->use24Hour        = false;
	cfg->darkTheme        = true;
	cfg->swapEyes         = false;
	cfg->slot             = 0;
	cfg->fontSizeAdjust   = 0;
	cfg->acknowledgedRisk = false;

	snprintf(cfg->catalogUrl, sizeof cfg->catalogUrl, "%s",
	         FC_DEFAULT_CATALOG_URL);
}

static bool jsonBool(const cJSON *obj, const char *key, bool fallback)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsBool(v) ? cJSON_IsTrue(v) : fallback;
}

static double jsonNum(const cJSON *obj, const char *key, double fallback)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static void jsonStr(char *dst, size_t size, const cJSON *obj, const char *key)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(v) && v->valuestring)
		snprintf(dst, size, "%s", v->valuestring);
}

bool fcConfigLoad(FcConfig *cfg)
{
	fcConfigDefaults(cfg);

	FILE *f = fopen(FC_CONFIG_PATH, "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 256 * 1024) {
		fclose(f);
		return false;
	}

	char *text = malloc((size_t)size + 1);
	if (!text) {
		fclose(f);
		return false;
	}

	const size_t got = fread(text, 1, (size_t)size, f);
	fclose(f);
	text[got] = '\0';

	cJSON *root = cJSON_Parse(text);
	free(text);

	if (!root)
		return false;

	const cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "units");
	if (cJSON_IsObject(units)) {
		cfg->use24Hour = jsonBool(units, "clock24", cfg->use24Hour);
		cfg->darkTheme = jsonBool(units, "darkTheme", cfg->darkTheme);
		cfg->swapEyes  = jsonBool(units, "swapEyes", cfg->swapEyes);
		cfg->utcOffsetMinutes =
			(int)jsonNum(units, "utcOffsetMinutes", cfg->utcOffsetMinutes);
	}

	const cJSON *fonts = cJSON_GetObjectItemCaseSensitive(root, "fonts");
	if (cJSON_IsObject(fonts)) {
		jsonStr(cfg->catalogUrl, sizeof cfg->catalogUrl, fonts, "catalogUrl");
		jsonStr(cfg->installedName, sizeof cfg->installedName, fonts, "installedName");
		jsonStr(cfg->installedId, sizeof cfg->installedId, fonts, "installedId");

		cfg->slot = (int)jsonNum(fonts, "slot", cfg->slot);
		cfg->fontSizeAdjust =
			(int)jsonNum(fonts, "sizeAdjust", cfg->fontSizeAdjust);

		if (cfg->fontSizeAdjust < FC_SIZE_ADJUST_MIN)
			cfg->fontSizeAdjust = FC_SIZE_ADJUST_MIN;
		if (cfg->fontSizeAdjust > FC_SIZE_ADJUST_MAX)
			cfg->fontSizeAdjust = FC_SIZE_ADJUST_MAX;
		cfg->acknowledgedRisk = jsonBool(fonts, "acknowledgedRisk",
		                                 cfg->acknowledgedRisk);

		if (cfg->slot < 0 || cfg->slot >= 4)
			cfg->slot = 0;
	}

	cJSON_Delete(root);
	return true;
}

bool fcConfigSave(const FcConfig *cfg)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(FC_CONFIG_DIR, 0777);

	cJSON *root = cJSON_CreateObject();
	if (!root)
		return false;

	cJSON *units = cJSON_AddObjectToObject(root, "units");
	cJSON_AddBoolToObject(units, "clock24", cfg->use24Hour);
	cJSON_AddBoolToObject(units, "darkTheme", cfg->darkTheme);
	cJSON_AddBoolToObject(units, "swapEyes", cfg->swapEyes);
	cJSON_AddNumberToObject(units, "utcOffsetMinutes", cfg->utcOffsetMinutes);

	cJSON *fonts = cJSON_AddObjectToObject(root, "fonts");
	cJSON_AddStringToObject(fonts, "catalogUrl", cfg->catalogUrl);
	cJSON_AddNumberToObject(fonts, "slot", cfg->slot);
	cJSON_AddNumberToObject(fonts, "sizeAdjust", cfg->fontSizeAdjust);
	cJSON_AddBoolToObject(fonts, "acknowledgedRisk", cfg->acknowledgedRisk);
	cJSON_AddStringToObject(fonts, "installedName", cfg->installedName);
	cJSON_AddStringToObject(fonts, "installedId", cfg->installedId);

	char *text = cJSON_Print(root);
	cJSON_Delete(root);

	if (!text)
		return false;

	const char *tmpPath = FC_CONFIG_PATH ".tmp";
	FILE *f = fopen(tmpPath, "wb");
	if (!f) {
		free(text);
		return false;
	}

	const size_t len = strlen(text);
	const bool wrote = fwrite(text, 1, len, f) == len;
	fclose(f);
	free(text);

	if (!wrote) {
		remove(tmpPath);
		return false;
	}

	remove(FC_CONFIG_PATH);
	if (rename(tmpPath, FC_CONFIG_PATH) != 0) {
		remove(tmpPath);
		return false;
	}

	return true;
}
