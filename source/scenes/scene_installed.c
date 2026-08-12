#include "app/app.h"
#include "app/config.h"
#include "app/fontfeed.h"
#include "app/fontinstall.h"
#include "data/cia.h"
#include "data/fontslot.h"

#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define ROW_H    32.0f
#define BUTTON_H 32.0f

#define MAX_KEPT 32

typedef struct {
	char name[64];
	char path[256];
} KeptFont;

typedef struct {
	FcScroll scroll;
	FcFocus  focus;
	bool     initialised;

	KeptFont kept[MAX_KEPT];
	int      keptCount;

	const char *notice;
} InstalledState;

static InstalledState s_inst;

static void rescan(void)
{
	s_inst.keptCount = 0;

	DIR *dir = opendir(FC_FONT_DIR);
	if (!dir)
		return;

	const struct dirent *ent;
	while ((ent = readdir(dir)) != NULL && s_inst.keptCount < MAX_KEPT) {
		const size_t len = strlen(ent->d_name);
		if (len < 5 || strcmp(ent->d_name + len - 4, ".cia") != 0)
			continue;

		KeptFont *k = &s_inst.kept[s_inst.keptCount++];
		snprintf(k->path, sizeof k->path, "%s/%s", FC_FONT_DIR, ent->d_name);
		snprintf(k->name, sizeof k->name, "%.*s", (int)(len - 4), ent->d_name);
	}

	closedir(dir);
}

static void enter(FcApp *app)
{
	(void)app;

	if (!s_inst.initialised) {
		fcScrollInit(&s_inst.scroll, FC_BOT_H - FC_TABBAR_H - FC_GRID * 4);
		s_inst.initialised = true;
	}

	s_inst.scroll.offset = 0.0f;
	s_inst.notice = NULL;
	fcFocusReset(&s_inst.focus);
	rescan();
}

static void drawTop(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;
	const FcConfig *cfg = app->config;

	fcBackdrop(d, d->screenW, d->screenH);

	const float cardX = FC_GRID * 3.0f;
	const float cardY = FC_GRID * 2.0f;
	const float cardW = FC_TOP_W - FC_GRID * 6.0f;
	const float cardH = FC_TOP_H - FC_GRID * 4.0f;
	fcCard(d, cardX, cardY, cardW, cardH, FC_DEPTH_CARD, &d->eye);

	const float left = cardX + FC_GRID * 2.0f;
	float y = cardY + FC_GRID * 1.5f;

	fcText(d, "Installed", left, y, FC_DEPTH_CONTENT, 0.55f, p->ink,
	         FC_ALIGN_LEFT);
	y += FC_GRID * 4.0f;

	for (int slot = 0; slot < FC_SLOT_COUNT; slot++) {
		const FcFontSlotInfo *info = fcFontSlotInfo(slot);

		FcInstalledTitle title;
		const bool known = fcFontInstalledInfo(slot, &title);

		char tid[24];
		fcCiaFormatTitleId(info->titleId, tid, sizeof tid);

		const bool target = slot == cfg->slot;

		fcText(d, info->label, left, y, FC_DEPTH_CONTENT, 0.36f,
		         target ? p->accent : p->ink, FC_ALIGN_LEFT);

		fcText(d, tid, left + FC_GRID * 9.0f, y + 1.0f, FC_DEPTH_CONTENT, 0.30f,
		         p->inkFaint, FC_ALIGN_LEFT);

		char detail[64];
		u32 color = p->inkDim;

		if (!fcFontInstallReady()) {
			snprintf(detail, sizeof detail, "unknown");
			color = p->warn;
		} else if (known && title.present) {
			snprintf(detail, sizeof detail, "v%u - %u KB",
			         title.version, (unsigned)(title.size / 1024));
		} else {
			snprintf(detail, sizeof detail, "missing");
			color = p->bad;
		}

		fcText(d, detail, cardX + cardW - FC_GRID * 2.0f, y + 1.0f,
		         FC_DEPTH_CONTENT, 0.30f, color, FC_ALIGN_RIGHT);

		y += FC_GRID * 3.0f;
	}

	y += FC_GRID;

	if (cfg->installedName[0]) {
		char line[128];
		snprintf(line, sizeof line, "This app last installed: %s",
		         cfg->installedName);
		fcTextEllipsized(d, line, left, y, FC_DEPTH_CONTENT, 0.32f, p->good,
		                   cardW - FC_GRID * 4.0f);
	} else {
		fcText(d, "This app has not installed anything yet.", left, y,
		         FC_DEPTH_CONTENT, 0.32f, p->inkFaint, FC_ALIGN_LEFT);
	}
	y += FC_GRID * 2.5f;

	fcText(d, "To undo: GodMode9 can reinstall the stock font.", left, y,
	         FC_DEPTH_CONTENT, 0.30f, p->inkFaint, FC_ALIGN_LEFT);
}

static void drawBottom(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;
	const u32 down = hidKeysDown();

	fcText(d, "On the card", FC_GRID, FC_GRID, 0.0f, 0.46f, p->ink,
	         FC_ALIGN_LEFT);

	const float listTop = FC_GRID * 4.0f;
	const float listH   = FC_BOT_H - FC_TABBAR_H - listTop;

	fcFocusBegin(&s_inst.focus, app->buttonMode, down);
	fcClipBegin(d, 0.0f, listTop, FC_BOT_W, listH);

	FcJobStatus job;
	fcFontFeedStatus(&job);

	float y = listTop - s_inst.scroll.offset;

	for (int i = 0; i < s_inst.keptCount; i++) {
		const KeptFont *k = &s_inst.kept[i];

		bool focused = false;
		const bool activated = fcFocusItem(&s_inst.focus, y, ROW_H, &focused);

		if (y + ROW_H >= listTop && y <= listTop + listH) {
			if (focused)
				fcFocusRing(d, FC_GRID * 0.5f, y, FC_BOT_W - FC_GRID, ROW_H);

			fcTextEllipsized(d, k->name, FC_GRID * 1.5f, y + 5.0f, 0.0f, 0.36f,
			                   p->ink, FC_BOT_W - FC_GRID * 8.0f);
			fcText(d, "reinstall", FC_BOT_W - FC_GRID * 1.5f, y + 7.0f, 0.0f,
			         0.28f, p->inkFaint, FC_ALIGN_RIGHT);

			C2D_DrawRectSolid(FC_GRID, y + ROW_H - 1.0f, 0.0f,
			                  FC_BOT_W - FC_GRID * 2, 1.0f, p->cardEdge);
		}

		const bool ready = fcFontInstallReady() && !fcFontFeedBusy();
		if (ready && (activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H))) {
			fcFontFeedAcknowledge();
			if (!fcFontFeedInstallLocal(k->path, -1))
				s_inst.notice = "Busy -- try that again in a moment.";
			else
				s_inst.notice = NULL;
		}

		y += ROW_H;
	}

	if (s_inst.keptCount == 0) {
		fcText(d, "Nothing kept yet.", FC_BOT_W / 2.0f, y + FC_GRID * 2.0f,
		         0.0f, 0.34f, p->inkFaint, FC_ALIGN_CENTER);
		fcText(d, "Fonts you install are saved here.", FC_BOT_W / 2.0f,
		         y + FC_GRID * 4.5f, 0.0f, 0.30f, p->inkFaint, FC_ALIGN_CENTER);
		y += FC_GRID * 7.0f;
	}

	y += FC_GRID;

	{
		const float x = FC_GRID;
		const float w = FC_BOT_W - FC_GRID * 2.0f;

		bool focused = false;
		const bool activated = fcFocusItem(&s_inst.focus, y, BUTTON_H, &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, BUTTON_H);

		fcButton(d, "Rescan the card", x, y, w, BUTTON_H,
		           held ? FC_BTN_ACTIVE : FC_BTN_NORMAL);
		if (focused)
			fcFocusRing(d, x, y, w, BUTTON_H);

		if (activated || fcTouchTapped(&app->touch, x, y, w, BUTTON_H))
			rescan();

		y += BUTTON_H + FC_GRID;
	}

	if (s_inst.notice) {
		fcText(d, s_inst.notice, FC_GRID, y, 0.0f, 0.32f, p->warn,
		         FC_ALIGN_LEFT);
		y += FC_GRID * 3.0f;
	}

	if (job.kind == FC_JOB_INSTALL && job.message[0]) {
		if (job.state == FC_JOB_RUNNING && job.percent >= 0) {
			C2D_DrawRectSolid(FC_GRID, y, 0.0f, FC_BOT_W - FC_GRID * 2, 4.0f,
			                  p->cardEdge);
			C2D_DrawRectSolid(FC_GRID, y, 0.0f,
			                  (FC_BOT_W - FC_GRID * 2) * (float)job.percent / 100.0f,
			                  4.0f, p->accent);
			y += FC_GRID * 2.0f;
		}

		fcTextEllipsized(d, job.message, FC_GRID, y, 0.0f, 0.32f,
		                   job.state == FC_JOB_FAILED ? p->bad : p->good,
		                   FC_BOT_W - FC_GRID * 2.0f);
		y += FC_GRID * 3.0f;
	}

	fcClipEnd();
	fcFocusEnd(&s_inst.focus);

	s_inst.scroll.contentHeight = (y + s_inst.scroll.offset) - listTop + FC_GRID;
	s_inst.scroll.viewHeight    = listH;
	fcScrollUpdate(&s_inst.scroll, &app->touch,
	                 app->touch.y < FC_BOT_H - FC_TABBAR_H, app->frameDt);
	fcFocusScrollIntoView(&s_inst.focus, &s_inst.scroll, listTop, listH);

	fcScrollbar(d, FC_BOT_W - 4.0f, listTop, listH, s_inst.scroll.offset,
	              s_inst.scroll.contentHeight, listH);
}

const FcSceneVTable fcSceneInstalled = {
	.tabLabel   = "Installed",
	.enter      = enter,
	.drawTop    = drawTop,
	.drawBottom = drawBottom,
};
