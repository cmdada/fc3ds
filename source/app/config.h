#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_CONFIG_DIR  "sdmc:/3ds/fc3ds"
#define FC_CONFIG_PATH FC_CONFIG_DIR "/config.json"

#define FC_DEFAULT_CATALOG_URL "https://example.invalid/fonts.json"

#define FC_SIZE_ADJUST_MIN (-4)
#define FC_SIZE_ADJUST_MAX 4

typedef struct FcConfig {
	int  utcOffsetMinutes;

	bool use24Hour;
	bool darkTheme;

	bool swapEyes;

	char catalogUrl[256];

	int  slot;

	int  fontSizeAdjust;

	bool acknowledgedRisk;

	char installedName[64];
	char installedId[40];
} FcConfig;

void fcConfigDefaults(FcConfig *cfg);

bool fcConfigLoad(FcConfig *cfg);

bool fcConfigSave(const FcConfig *cfg);

typedef struct {
	const char *label;
	int         minutes;
} FcTimezoneChoice;

const FcTimezoneChoice *fcTimezoneChoices(int *count);

int fcTimezoneIndexFor(int minutes);

void fcFormatUtcOffset(int minutes, char *dst, size_t size);
