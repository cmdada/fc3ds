#include "app/app.h"
#include "app/config.h"
#include "app/fontfeed.h"
#include "app/textinput.h"
#include "data/fontslot.h"
#include "net/sntp.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#define ROW_H 30.0f

typedef enum {
	PAGE_MAIN,
	PAGE_TIMEZONE,
	PAGE_SLOT,
} SettingsPage;

typedef struct {
	SettingsPage page;
	FcScroll   scroll;
	bool         initialised;
	FcFocus    focus;

	bool         wantCatalogUrl;
} SettingsState;

static SettingsState s_set;

static void drawFocusRing(const FcDraw *d, float y, float h)
{
	fcFocusRing(d, FC_GRID * 0.5f, y, FC_BOT_W - FC_GRID, h);
}

static void saveNow(FcApp *app)
{
	fcConfigSave(app->config);
}

static void enter(FcApp *app)
{
	if (!s_set.initialised) {
		fcScrollInit(&s_set.scroll, FC_BOT_H - FC_TABBAR_H - FC_GRID * 4);
		s_set.initialised = true;
	}
	s_set.page = PAGE_MAIN;
	s_set.scroll.offset = 0.0f;
	fcFocusReset(&s_set.focus);
	(void)app;
}

static void update(FcApp *app, float dt)
{
	(void)dt;

	if (!s_set.wantCatalogUrl)
		return;

	s_set.wantCatalogUrl = false;

	FcConfig *cfg = app->config;
	char url[sizeof cfg->catalogUrl];
	snprintf(url, sizeof url, "%s", cfg->catalogUrl);

	if (fcTextInputAsk(app, "Catalog address", url, url, sizeof url) && url[0]) {
		snprintf(cfg->catalogUrl, sizeof cfg->catalogUrl, "%s", url);
		saveNow(app);
		fcFontFeedRefresh(cfg->catalogUrl);
	}
}

static void drawTop(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;

	fcBackdrop(d, d->screenW, d->screenH);

	const float cardX = FC_GRID * 3.0f;
	const float cardY = FC_GRID * 4.0f;
	const float cardW = FC_TOP_W - FC_GRID * 6.0f;
	const float cardH = FC_GRID * 16.0f;
	fcCard(d, cardX, cardY, cardW, cardH, FC_DEPTH_CARD, &d->eye);

	fcText(d, "Settings", FC_TOP_W / 2.0f, cardY + FC_GRID * 1.5f,
	         FC_DEPTH_CONTENT, 0.7f, p->ink, FC_ALIGN_CENTER);

	char line[128];
	const FcFontSlotInfo *slot = fcFontSlotInfo(app->config->slot);
	snprintf(line, sizeof line, "Installs target the %s font",
	         slot ? slot->label : "?");
	fcText(d, line, FC_TOP_W / 2.0f, cardY + FC_GRID * 6.0f,
	         FC_DEPTH_CONTENT, 0.42f, p->accent, FC_ALIGN_CENTER);

	const FcClock *clock = fcClockState();
	if (clock->synced) {
		fcFormatTime(app, fcAppLocalNow(app), line, sizeof line);
		fcText(d, line, FC_TOP_W / 2.0f, cardY + FC_GRID * 10.0f,
		         FC_DEPTH_CONTENT, 0.5f, p->inkDim, FC_ALIGN_CENTER);
	} else {
		fcText(d, "clock not synced - downloads may fail", FC_TOP_W / 2.0f,
		         cardY + FC_GRID * 10.5f, FC_DEPTH_CONTENT, 0.34f, p->warn,
		         FC_ALIGN_CENTER);
	}

	fcText(d, "Changes are saved as you make them.", FC_TOP_W / 2.0f,
	         cardY + cardH + FC_GRID, FC_DEPTH_CONTENT, 0.36f, p->inkFaint,
	         FC_ALIGN_CENTER);
}

static bool toggleRow(FcApp *app, const FcDraw *d, float y,
                      const char *label, bool on)
{
	const FcPalette *p = &app->theme.pal;

	bool focused = false;
	const bool activated = fcFocusItem(&s_set.focus, y, ROW_H, &focused);

	if (focused)
		drawFocusRing(d, y, ROW_H);

	fcText(d, label, FC_GRID * 1.5f, y + 7.0f, 0.0f, 0.42f, p->ink,
	         FC_ALIGN_LEFT);
	fcToggle(d, FC_BOT_W - FC_GRID * 1.5f - 34.0f, y + 6.0f, on);

	C2D_DrawRectSolid(FC_GRID, y + ROW_H - 1.0f, 0.0f,
	                  FC_BOT_W - FC_GRID * 2, 1.0f, p->cardEdge);

	return activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H);
}

static bool settingRow(FcApp *app, const FcDraw *d, float y,
                       const char *label, const char *value)
{
	const FcPalette *p = &app->theme.pal;

	bool focused = false;
	const bool activated = fcFocusItem(&s_set.focus, y, ROW_H, &focused);

	if (focused)
		drawFocusRing(d, y, ROW_H);

	fcText(d, label, FC_GRID * 1.5f, y + 7.0f, 0.0f, 0.42f, p->ink,
	         FC_ALIGN_LEFT);
	fcText(d, value, FC_BOT_W - FC_GRID * 1.5f, y + 8.0f, 0.0f, 0.36f,
	         p->inkDim, FC_ALIGN_RIGHT);

	C2D_DrawRectSolid(FC_GRID, y + ROW_H - 1.0f, 0.0f,
	                  FC_BOT_W - FC_GRID * 2, 1.0f, p->cardEdge);

	return activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H);
}

static int stepperRow(FcApp *app, const FcDraw *d, float y,
                      const char *label, const char *value,
                      bool canDecrease, bool canIncrease)
{
	const FcPalette *p = &app->theme.pal;
	const float bw = 30.0f;
	const float minusX = FC_BOT_W - FC_GRID * 1.5f - bw * 2 - FC_GRID;
	const float plusX  = FC_BOT_W - FC_GRID * 1.5f - bw;

	fcText(d, label, FC_GRID * 1.5f, y + 7.0f, 0.0f, 0.42f, p->ink,
	         FC_ALIGN_LEFT);
	fcText(d, value, minusX - FC_GRID, y + 8.0f, 0.0f, 0.36f, p->inkDim,
	         FC_ALIGN_RIGHT);

	int delta = 0;

	bool focused = false;
	const bool minusHit = fcFocusItemInRow(&s_set.focus, s_set.focus.rowCursor,
	                                         y, ROW_H, &focused);
	fcButton(d, "-", minusX, y + 2.0f, bw, ROW_H - 5.0f,
	           canDecrease ? (app->touch.down &&
	                          fcTouchInRect(&app->touch, minusX, y, bw, ROW_H)
	                              ? FC_BTN_ACTIVE : FC_BTN_NORMAL)
	                       : FC_BTN_DISABLED);
	if (focused)
		fcFocusRing(d, minusX, y + 2.0f, bw, ROW_H - 5.0f);
	if (canDecrease && (minusHit ||
	                    fcTouchTapped(&app->touch, minusX, y, bw, ROW_H)))
		delta = -1;

	focused = false;
	const bool plusHit = fcFocusItemInRow(&s_set.focus, s_set.focus.rowCursor,
	                                        y, ROW_H, &focused);
	fcButton(d, "+", plusX, y + 2.0f, bw, ROW_H - 5.0f,
	           canIncrease ? (app->touch.down &&
	                          fcTouchInRect(&app->touch, plusX, y, bw, ROW_H)
	                              ? FC_BTN_ACTIVE : FC_BTN_NORMAL)
	                       : FC_BTN_DISABLED);
	if (focused)
		fcFocusRing(d, plusX, y + 2.0f, bw, ROW_H - 5.0f);
	if (canIncrease && (plusHit ||
	                    fcTouchTapped(&app->touch, plusX, y, bw, ROW_H)))
		delta = 1;

	C2D_DrawRectSolid(FC_GRID, y + ROW_H - 1.0f, 0.0f,
	                  FC_BOT_W - FC_GRID * 2, 1.0f, p->cardEdge);

	return delta;
}

static void formatSizeAdjust(int adjust, char *dst, size_t size)
{
	if (adjust == 0)
		snprintf(dst, size, "Default");
	else
		snprintf(dst, size, "%+d px", adjust);
}

static void drawMainPage(FcApp *app, const FcDraw *d, float top)
{
	FcConfig *cfg = app->config;

	float y = top - s_set.scroll.offset;

	char value[64];

	const FcFontSlotInfo *slot = fcFontSlotInfo(cfg->slot);
	if (settingRow(app, d, y, "Target font", slot ? slot->label : "?")) {
		s_set.page = PAGE_SLOT;
		s_set.scroll.offset = 0.0f;
		fcFocusReset(&s_set.focus);
	}
	y += ROW_H;

	{
		const char *host = strstr(cfg->catalogUrl, "://");
		host = host ? host + 3 : cfg->catalogUrl;
		snprintf(value, sizeof value, "%.28s%s", host,
		         strlen(host) > 28 ? "..." : "");

		if (settingRow(app, d, y, "Catalog", value))
			s_set.wantCatalogUrl = true;
		y += ROW_H;
	}

	formatSizeAdjust(cfg->fontSizeAdjust, value, sizeof value);
	{
		const int delta = stepperRow(app, d, y, "Font size", value,
		                               cfg->fontSizeAdjust > FC_SIZE_ADJUST_MIN,
		                               cfg->fontSizeAdjust < FC_SIZE_ADJUST_MAX);
		if (delta != 0) {
			cfg->fontSizeAdjust += delta;
			fcFontFeedSetSizeAdjust(cfg->fontSizeAdjust);
			saveNow(app);
		}
		y += ROW_H;
	}

	const int idx = fcTimezoneIndexFor(cfg->utcOffsetMinutes);
	int count = 0;
	const FcTimezoneChoice *tz = fcTimezoneChoices(&count);
	if (idx >= 0)
		snprintf(value, sizeof value, "%s", tz[idx].label);
	else
		fcFormatUtcOffset(cfg->utcOffsetMinutes, value, sizeof value);

	if (settingRow(app, d, y, "Time zone", value)) {
		s_set.page = PAGE_TIMEZONE;
		s_set.scroll.offset = 0.0f;
		fcFocusReset(&s_set.focus);
	}
	y += ROW_H;

	if (toggleRow(app, d, y, "24-hour clock", cfg->use24Hour)) {
		cfg->use24Hour = !cfg->use24Hour;
		app->use24Hour = cfg->use24Hour;
		saveNow(app);
	}
	y += ROW_H;

	if (toggleRow(app, d, y, "Dark theme", cfg->darkTheme)) {
		cfg->darkTheme = !cfg->darkTheme;
		fcThemeSetDark(&app->theme, cfg->darkTheme);
		saveNow(app);
	}
	y += ROW_H;

	if (toggleRow(app, d, y, "Swap 3D eyes", cfg->swapEyes)) {
		cfg->swapEyes = !cfg->swapEyes;
		app->stereo.swapEyes = cfg->swapEyes;
		saveNow(app);
	}
	y += ROW_H;

	/* Two choices, so the row cycles rather than opening a page of its own. */
	if (settingRow(app, d, y, "Keyboard", fcKeyboardLabel(cfg->keyboard))) {
		cfg->keyboard = (FcKeyboard)((cfg->keyboard + 1) % FC_KEYBOARD_COUNT);
		saveNow(app);
	}
	y += ROW_H;

	s_set.scroll.contentHeight = (y + s_set.scroll.offset) - top + FC_GRID;
}

static void drawSlotPage(FcApp *app, const FcDraw *d, float top)
{
	FcConfig *cfg = app->config;
	const FcPalette *p = &app->theme.pal;

	float y = top - s_set.scroll.offset;

	for (int i = 0; i < FC_SLOT_COUNT; i++) {
		const FcFontSlotInfo *info = fcFontSlotInfo(i);

		bool focused = false;
		const bool activated = fcFocusItem(&s_set.focus, y, ROW_H, &focused);
		const bool selected = cfg->slot == i;

		if (focused)
			drawFocusRing(d, y, ROW_H);
		if (selected)
			fcRoundedRect(FC_GRID * 0.5f, y + 1.0f, FC_BOT_W - FC_GRID,
			                ROW_H - 2.0f, FC_RADIUS, 0.0f, p->accentSoft,
			                &d->eye);

		fcText(d, info->label, FC_GRID * 1.5f, y + 7.0f, 0.0f, 0.36f,
		         selected ? p->accent : p->ink, FC_ALIGN_LEFT);
		fcText(d, info->regions, FC_BOT_W - FC_GRID * 1.5f, y + 8.0f, 0.0f,
		         0.30f, p->inkDim, FC_ALIGN_RIGHT);

		if (activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H)) {
			cfg->slot = i;
			saveNow(app);
			s_set.page = PAGE_MAIN;
			s_set.scroll.offset = 0.0f;
			fcFocusReset(&s_set.focus);
			return;
		}

		y += ROW_H;
	}

	s_set.scroll.contentHeight = (y + s_set.scroll.offset) - top + FC_GRID;
}

static void drawTimezonePage(FcApp *app, const FcDraw *d, float top,
                             float viewH)
{
	FcConfig *cfg = app->config;
	const FcPalette *p = &app->theme.pal;

	int count = 0;
	const FcTimezoneChoice *tz = fcTimezoneChoices(&count);

	float y = top - s_set.scroll.offset;

	for (int i = 0; i < count; i++) {
		bool focused = false;
		const bool activated = fcFocusItem(&s_set.focus, y, ROW_H, &focused);

		if (y + ROW_H >= top && y <= top + viewH) {
			const bool selected = cfg->utcOffsetMinutes == tz[i].minutes;

			if (focused)
				drawFocusRing(d, y, ROW_H);
			if (selected)
				fcRoundedRect(FC_GRID * 0.5f, y + 1.0f,
				                FC_BOT_W - FC_GRID, ROW_H - 2.0f,
				                FC_RADIUS, 0.0f, p->accentSoft, &d->eye);

			fcText(d, tz[i].label, FC_GRID * 1.5f, y + 7.0f, 0.0f, 0.36f,
			         selected ? p->accent : p->ink, FC_ALIGN_LEFT);

			char off[16];
			fcFormatUtcOffset(tz[i].minutes, off, sizeof off);
			fcText(d, off, FC_BOT_W - FC_GRID * 1.5f, y + 8.0f, 0.0f,
			         0.36f, p->inkDim, FC_ALIGN_RIGHT);
		}

		if (activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H)) {
			cfg->utcOffsetMinutes = tz[i].minutes;
			app->utcOffset = tz[i].minutes * 60;
			saveNow(app);
			s_set.page = PAGE_MAIN;
			s_set.scroll.offset = 0.0f;
			fcFocusReset(&s_set.focus);
			return;
		}

		y += ROW_H;
	}

	s_set.scroll.contentHeight = (y + s_set.scroll.offset) - top + FC_GRID;
}

static void drawBottom(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;
	const u32 down = hidKeysDown();

	if ((down & KEY_B) && s_set.page != PAGE_MAIN) {
		s_set.page = PAGE_MAIN;
		s_set.scroll.offset = 0.0f;
		fcFocusReset(&s_set.focus);
	}

	const float headerH = FC_GRID * 4.0f;
	const float listTop = headerH;
	const float listH   = FC_BOT_H - FC_TABBAR_H - listTop;

	const char *title = "Settings";
	if (s_set.page == PAGE_TIMEZONE)
		title = "Time zone";
	else if (s_set.page == PAGE_SLOT)
		title = "Target font";
	fcText(d, title, FC_GRID, FC_GRID, 0.0f, 0.5f, p->ink,
	         FC_ALIGN_LEFT);
	if (s_set.page != PAGE_MAIN)
		fcText(d, "B to go back", FC_BOT_W - FC_GRID, FC_GRID + 2.0f,
		         0.0f, 0.34f, p->inkFaint, FC_ALIGN_RIGHT);

	fcFocusBegin(&s_set.focus, app->buttonMode, down);

	fcClipBegin(d, 0.0f, listTop, FC_BOT_W, listH);

	switch (s_set.page) {
	case PAGE_MAIN:     drawMainPage(app, d, listTop);            break;
	case PAGE_SLOT:     drawSlotPage(app, d, listTop);            break;
	case PAGE_TIMEZONE: drawTimezonePage(app, d, listTop, listH); break;
	}

	fcClipEnd();

	fcFocusEnd(&s_set.focus);

	s_set.scroll.viewHeight = listH;
	fcScrollUpdate(&s_set.scroll, &app->touch,
	                 app->touch.y < FC_BOT_H - FC_TABBAR_H, app->frameDt);
	fcFocusScrollIntoView(&s_set.focus, &s_set.scroll, listTop, listH);

	fcScrollbar(d, FC_BOT_W - 4.0f, listTop, listH, s_set.scroll.offset,
	              s_set.scroll.contentHeight, listH);
}

const FcSceneVTable fcSceneSettings = {
	.tabLabel   = "Settings",
	.enter      = enter,
	.update     = update,
	.drawTop    = drawTop,
	.drawBottom = drawBottom,
};
