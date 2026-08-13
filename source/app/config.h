#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_CONFIG_DIR  "sdmc:/3ds/fc3ds"
#define FC_CONFIG_PATH FC_CONFIG_DIR "/config.json"

#define FC_DEFAULT_CATALOG_URL "https://example.invalid/fonts.json"

#define FC_SIZE_ADJUST_MIN (-4)
#define FC_SIZE_ADJUST_MAX 4

/**
 * Which keyboard a text prompt puts up.
 *
 * The system applet takes both screens and suspends the app until it is
 * dismissed; the touch keyboard runs in-process on the bottom screen with the
 * app still drawing above it. Anyone running a custom OSK on their console has
 * replaced the applet, so FC_KEYBOARD_SYSTEM gets theirs.
 */
typedef enum {
	FC_KEYBOARD_SYSTEM = 0,   ///< swkbdInputText.
	FC_KEYBOARD_TOUCH  = 1,   ///< ctr-osk-rt, drawn by us.
	FC_KEYBOARD_COUNT,
} FcKeyboard;

/// Display name for a keyboard choice, for the settings row.
const char *fcKeyboardLabel(FcKeyboard kb);

typedef struct FcConfig {
	int  utcOffsetMinutes;

	bool use24Hour;
	bool darkTheme;

	bool swapEyes;

	/// Which keyboard fcTextInputAsk() puts up.
	FcKeyboard keyboard;

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
