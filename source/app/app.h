#pragma once

#include "ui/focus.h"
#include "ui/stereo.h"
#include "ui/theme.h"
#include "ui/touch.h"
#include "ui/widgets.h"

#include <stdbool.h>
#include <time.h>

typedef enum {
	FC_TAB_BROWSE,
	FC_TAB_INSTALLED,
	FC_TAB_SCAN,
	FC_TAB_SETTINGS,
	FC_TAB_COUNT,
} FcTab;

typedef struct FcApp FcApp;

struct FcConfig;

typedef struct {
	const char *tabLabel;

	void (*enter)(FcApp *app);
	void (*leave)(FcApp *app);
	void (*update)(FcApp *app, float dt);
	void (*drawTop)(FcApp *app, const FcDraw *d);
	void (*drawBottom)(FcApp *app, const FcDraw *d);
} FcSceneVTable;

struct FcApp {
	FcTheme   theme;
	FcStereo  stereo;
	FcTouch   touch;
	C2D_TextBuf textBuf;

	FcTab tab;
	const FcSceneVTable *scenes[FC_TAB_COUNT];

	int  utcOffset;
	bool use24Hour;

	struct FcConfig *config;

	bool buttonMode;

	bool networkUp;
	float timeSec;
	float frameDt;
};

time_t fcAppLocalNow(const FcApp *app);

void fcFormatTime(const FcApp *app, time_t local, char *dst, size_t size);

void fcFormatDate(const FcApp *app, time_t local, char *dst, size_t size);

extern const FcSceneVTable fcSceneBrowse;
extern const FcSceneVTable fcSceneInstalled;
extern const FcSceneVTable fcSceneScan;
extern const FcSceneVTable fcSceneSettings;

void fcAppSwitchTab(FcApp *app, FcTab tab);

struct FcFontEntry;
void fcBrowseOpenEntry(FcApp *app, const struct FcFontEntry *entry);

#define FC_TABBAR_H 34.0f

bool fcDrawTabBar(FcApp *app, const FcDraw *d);
