#include "app/app.h"
#include "app/config.h"
#include "app/fontfeed.h"
#include "app/fontinstall.h"
#include "app/log.h"
#include "app/textinput.h"
#include "data/catalog.h"
#include "data/fontslot.h"
#include "data/gfonts.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H     34.0f
#define BUTTON_H  32.0f

static const char *const kPreviewLines[] = {
	"Handgloves 0123",
	"quick brown fox;",
	"jumps over -- 48",
};

typedef enum {
	PAGE_LIST,
	PAGE_DETAIL,
	PAGE_CONFIRM,
} BrowsePage;

typedef enum {
	SOURCE_GOOGLE,
	SOURCE_CATALOG,
} BrowseSource;

#define MAX_RESULTS 60

typedef struct {
	BrowsePage   page;
	BrowseSource source;
	FcScroll     scroll;
	FcFocus      focus;
	bool         initialised;

	FcGFontList gfonts;
	bool        gfontsTried;

	char query[48];

	bool wantSearch;
	int  matches[MAX_RESULTS];
	int  matchCount;
	bool truncated;

	FcFontEntry entry;
	bool        haveEntry;

	C2D_Font    previewFont;
	void       *previewData;
	FcBcfntInfo previewInfo;

	bool installPending;

	bool wantRefresh;

	bool fromScan;

	const char *notice;
} BrowseState;

static BrowseState s_browse;

static void dropPreview(void)
{
	if (s_browse.previewFont) {
		C2D_FontFree(s_browse.previewFont);
		s_browse.previewFont = NULL;
	}

	free(s_browse.previewData);
	s_browse.previewData = NULL;

	memset(&s_browse.previewInfo, 0, sizeof s_browse.previewInfo);
}

static void collectPreview(void)
{
	if (!fcFontFeedPreviewReady())
		return;

	size_t len = 0;
	FcBcfntInfo info;
	void *data = fcFontFeedTakePreview(&len, &info);
	if (!data)
		return;

	dropPreview();

	C2D_Font font = C2D_FontLoadFromMem(data, len);
	if (!font) {
		FC_LOG("citro2d refused a validated font");
		free(data);
		return;
	}

	C2D_FontSetFilter(font, GPU_LINEAR, GPU_LINEAR);

	s_browse.previewFont = font;
	s_browse.previewData = data;
	s_browse.previewInfo = info;
}

static void openEntry(const FcFontEntry *entry)
{
	dropPreview();

	s_browse.entry     = *entry;
	s_browse.haveEntry = true;
	s_browse.page      = PAGE_DETAIL;
	s_browse.scroll.offset = 0.0f;
	fcFocusReset(&s_browse.focus);

	if (fcFontEntryPreviewable(entry) && !fcFontFeedBusy()) {
		fcFontFeedAcknowledge();
		fcFontFeedFetchPreview(entry);
	}
}

static void beginInstall(const FcFontEntry *entry)
{
	if (fcFontFeedInstall(entry)) {
		s_browse.installPending = true;
		s_browse.notice = NULL;
	} else {
		s_browse.notice = "Busy -- try that again in a moment.";
	}
}

static void recordInstall(FcApp *app)
{
	if (!s_browse.installPending)
		return;

	FcJobStatus job;
	fcFontFeedStatus(&job);

	if (job.kind != FC_JOB_INSTALL || job.state == FC_JOB_RUNNING)
		return;

	s_browse.installPending = false;

	if (job.state != FC_JOB_DONE)
		return;

	snprintf(app->config->installedName, sizeof app->config->installedName,
	         "%s", s_browse.entry.name);
	snprintf(app->config->installedId, sizeof app->config->installedId,
	         "%s", s_browse.entry.id);
	fcConfigSave(app->config);
}

void fcBrowseOpenEntry(FcApp *app, const struct FcFontEntry *entry)
{
	if (!entry)
		return;

	openEntry(entry);
	s_browse.fromScan = true;
	fcAppSwitchTab(app, FC_TAB_BROWSE);
}

static void refilter(void)
{
	s_browse.matchCount = 0;
	s_browse.truncated  = false;

	if (s_browse.gfonts.count == 0)
		return;

	int probe[MAX_RESULTS + 1];
	const int n = fcGFontsSearch(&s_browse.gfonts, s_browse.query, probe,
	                               MAX_RESULTS + 1);

	s_browse.truncated  = n > MAX_RESULTS;
	s_browse.matchCount = n > MAX_RESULTS ? MAX_RESULTS : n;

	for (int i = 0; i < s_browse.matchCount; i++)
		s_browse.matches[i] = probe[i];
}

static void ensureGoogleList(void)
{
	if (s_browse.gfontsTried)
		return;

	s_browse.gfontsTried = true;

	if (!fcGFontsLoad(FC_GFONTS_PATH, &s_browse.gfonts))
		FC_LOG("google font list unavailable: %s", s_browse.gfonts.error);

	refilter();
}

static void enter(FcApp *app)
{
	if (!s_browse.initialised) {
		fcScrollInit(&s_browse.scroll, FC_BOT_H - FC_TABBAR_H - FC_GRID * 4);
		s_browse.initialised = true;
	}

	ensureGoogleList();

	fcFocusReset(&s_browse.focus);

	if (!s_browse.fromScan)
		s_browse.page = s_browse.haveEntry ? s_browse.page : PAGE_LIST;
	s_browse.fromScan = false;

	const FcCatalog *cat = fcFontFeedCatalog();
	if (s_browse.source == SOURCE_CATALOG && cat->count == 0 && app->networkUp)
		s_browse.wantRefresh = true;
}

static void update(FcApp *app, float dt)
{
	(void)dt;

	if (s_browse.wantSearch) {
		s_browse.wantSearch = false;

		char q[sizeof s_browse.query];
		snprintf(q, sizeof q, "%s", s_browse.query);

		if (fcTextInputAsk(app, "Family name", q, q, sizeof q)) {
			snprintf(s_browse.query, sizeof s_browse.query, "%s", q);
			refilter();
			s_browse.scroll.offset = 0.0f;
			fcFocusReset(&s_browse.focus);
		}
	}

	collectPreview();
	recordInstall(app);

	if (s_browse.wantRefresh && app->networkUp &&
	    fcFontFeedRefresh(app->config->catalogUrl))
		s_browse.wantRefresh = false;
}

static void drawPreviewCard(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;

	const float cardX = FC_GRID * 2.0f;
	const float cardY = FC_GRID * 2.0f;
	const float cardW = FC_TOP_W - FC_GRID * 4.0f;
	const float cardH = FC_TOP_H - FC_GRID * 4.0f;
	fcCard(d, cardX, cardY, cardW, cardH, FC_DEPTH_CARD, &d->eye);

	const float left = cardX + FC_GRID * 2.0f;

	fcTextEllipsized(d, s_browse.entry.name, left, cardY + FC_GRID,
	                   FC_DEPTH_CONTENT, 0.5f, p->ink, cardW - FC_GRID * 4.0f);

	if (s_browse.previewFont) {
		float y = cardY + FC_GRID * 4.0f;
		for (size_t i = 0; i < sizeof kPreviewLines / sizeof kPreviewLines[0]; i++) {
			fcTextInFont(d, s_browse.previewFont, kPreviewLines[i], left, y,
			               FC_DEPTH_CONTENT, 1.0f, p->ink, FC_ALIGN_LEFT);
			y += (float)(s_browse.previewInfo.cellHeight + 6);
		}

		FcMergeReport m;
		fcFontFeedMergeReport(&m);

		char line[96];
		if (m.requested > 0) {
			snprintf(line, sizeof line, "%d of %d characters, at %dpx",
			         m.replaced, m.requested, m.pixelSize);
			fcText(d, line, left, cardY + cardH - FC_GRID * 4.5f,
			         FC_DEPTH_CONTENT, 0.34f,
			         m.replaced * 4 >= m.requested * 3 ? p->good : p->warn,
			         FC_ALIGN_LEFT);

			if (m.replaced * 4 < m.requested * 3)
				fcText(d, "the rest keep the font already installed", left,
				         cardY + cardH - FC_GRID * 2.5f, FC_DEPTH_CONTENT,
				         0.30f, p->inkFaint, FC_ALIGN_LEFT);
		}
		return;
	}

	FcJobStatus job;
	fcFontFeedStatus(&job);

	const char *msg;
	u32 color = p->inkDim;

	if (job.kind == FC_JOB_PREVIEW && job.state == FC_JOB_RUNNING) {
		msg = "Fetching the preview...";
		fcSpinner(d, cardX + cardW - FC_GRID * 2.5f, cardY + FC_GRID * 2.0f,
		            7.0f, app->timeSec, p->accent);
	} else if (job.kind == FC_JOB_PREVIEW && job.state == FC_JOB_FAILED) {
		msg = job.message;
		color = p->bad;
	} else if (!fcFontEntryPreviewable(&s_browse.entry)) {
		msg = "This entry ships no preview, so there is nothing to show "
		      "until it is installed.";
		color = p->warn;
	} else if (!app->networkUp) {
		msg = "No network, so no preview.";
		color = p->warn;
	} else {
		msg = "Press Preview to see this font.";
	}

	fcTextEllipsized(d, msg, left, cardY + FC_GRID * 6.0f, FC_DEPTH_CONTENT,
	                   0.38f, color, cardW - FC_GRID * 4.0f);
}

static void drawListTop(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;
	const FcCatalog *cat = fcFontFeedCatalog();

	const float cardX = FC_GRID * 3.0f;
	const float cardY = FC_GRID * 4.0f;
	const float cardW = FC_TOP_W - FC_GRID * 6.0f;
	const float cardH = FC_TOP_H - FC_GRID * 9.0f;
	fcCard(d, cardX, cardY, cardW, cardH, FC_DEPTH_CARD, &d->eye);

	const float left = cardX + FC_GRID * 2.0f;

	fcText(d, "Font Changer", left, cardY + FC_GRID * 1.5f, FC_DEPTH_CONTENT,
	         0.62f, p->ink, FC_ALIGN_LEFT);

	const FcFontSlotInfo *slot = fcFontSlotInfo(app->config->slot);
	char line[128];
	snprintf(line, sizeof line, "Target: %s font (%s)",
	         slot ? slot->label : "?", slot ? slot->regions : "?");
	fcText(d, line, left, cardY + FC_GRID * 5.5f, FC_DEPTH_CONTENT, 0.36f,
	         p->inkDim, FC_ALIGN_LEFT);

	if (s_browse.source == SOURCE_GOOGLE) {
		snprintf(line, sizeof line, "Google Fonts - %d famil%s available",
		         s_browse.gfonts.count, s_browse.gfonts.count == 1 ? "y" : "ies");
	} else {
		snprintf(line, sizeof line, "%s - %d font%s", cat->name, cat->count,
		         cat->count == 1 ? "" : "s");
	}
	fcTextEllipsized(d, line, left, cardY + FC_GRID * 8.0f, FC_DEPTH_CONTENT,
	                   0.36f, p->inkFaint, cardW - FC_GRID * 4.0f);

	FcJobStatus job;
	fcFontFeedStatus(&job);

	if (job.state == FC_JOB_RUNNING) {
		fcSpinner(d, cardX + cardW - FC_GRID * 2.5f, cardY + FC_GRID * 2.0f,
		            7.0f, app->timeSec, p->accent);
	}

	if (job.message[0])
		fcTextEllipsized(d, job.message, left, cardY + FC_GRID * 11.0f,
		                   FC_DEPTH_CONTENT, 0.36f,
		                   job.state == FC_JOB_FAILED ? p->bad : p->accent,
		                   cardW - FC_GRID * 4.0f);

	if (!fcFontInstallReady())
		fcText(d,
		         "No AM access: this build can browse but not install.",
		         FC_TOP_W / 2.0f, cardY + cardH + FC_GRID, FC_DEPTH_CONTENT,
		         0.32f, p->warn, FC_ALIGN_CENTER);
}

static void drawTop(FcApp *app, const FcDraw *d)
{
	fcBackdrop(d, d->screenW, d->screenH);

	if (s_browse.page == PAGE_LIST || !s_browse.haveEntry)
		drawListTop(app, d);
	else
		drawPreviewCard(app, d);
}

static float drawSourceSwitch(FcApp *app, const FcDraw *d, float y)
{
	const float w = (FC_BOT_W - FC_GRID * 3.0f) / 2.0f;
	const float h = 26.0f;

	static const char *const kLabels[2] = { "Google Fonts", "Catalog" };

	for (int i = 0; i < 2; i++) {
		const float x = FC_GRID + i * (w + FC_GRID);
		const bool selected = (int)s_browse.source == i;

		bool focused = false;
		const bool activated = fcFocusItemInRow(&s_browse.focus, 0, y, h,
		                                          &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, h);

		fcButton(d, kLabels[i], x, y, w, h,
		           selected ? FC_BTN_SELECTED
		                    : (held ? FC_BTN_ACTIVE : FC_BTN_NORMAL));
		if (focused)
			fcFocusRing(d, x, y, w, h);

		if (!selected &&
		    (activated || fcTouchTapped(&app->touch, x, y, w, h))) {
			s_browse.source = (BrowseSource)i;
			s_browse.scroll.offset = 0.0f;
			fcFocusReset(&s_browse.focus);

			if (s_browse.source == SOURCE_CATALOG &&
			    fcFontFeedCatalog()->count == 0 && app->networkUp)
				s_browse.wantRefresh = true;
		}
	}

	return y + h + FC_GRID;
}

static float drawSearchRow(FcApp *app, const FcDraw *d, float y)
{
	const FcPalette *p = &app->theme.pal;

	const float x = FC_GRID;
	const float w = FC_BOT_W - FC_GRID * 2.0f;
	const float h = 26.0f;

	bool focused = false;
	const bool activated = fcFocusItem(&s_browse.focus, y, h, &focused);
	const bool held = app->touch.down && fcTouchInRect(&app->touch, x, y, w, h);

	fcRoundedRect(x, y, w, h, FC_RADIUS, 0.0f,
	                held ? p->accentSoft : p->hlLow, &d->eye);
	if (focused)
		fcFocusRing(d, x, y, w, h);

	const bool empty = s_browse.query[0] == '\0';
	fcTextEllipsized(d, empty ? "Search 1800+ families" : s_browse.query,
	                   x + FC_GRID, y + 6.0f, 0.0f, 0.34f,
	                   empty ? p->inkFaint : p->ink, w - FC_GRID * 4.0f);

	if (!empty)
		fcText(d, "x", x + w - FC_GRID * 1.5f, y + 5.0f, 0.0f, 0.34f,
		         p->inkDim, FC_ALIGN_LEFT);

	if (activated || fcTouchTapped(&app->touch, x, y, w, h))
		s_browse.wantSearch = true;

	return y + h + FC_GRID;
}

static float drawGoogleList(FcApp *app, const FcDraw *d, float y,
                            float top, float viewH)
{
	const FcPalette *p = &app->theme.pal;

	if (s_browse.gfonts.count == 0) {
		fcText(d, "The bundled font list is missing.", FC_BOT_W / 2.0f,
		         y + FC_GRID, 0.0f, 0.34f, p->bad, FC_ALIGN_CENTER);
		return y + FC_GRID * 4.0f;
	}

	for (int i = 0; i < s_browse.matchCount; i++) {
		const FcGFontFamily *fam = &s_browse.gfonts.items[s_browse.matches[i]];

		bool focused = false;
		const bool activated = fcFocusItem(&s_browse.focus, y, ROW_H, &focused);

		if (y + ROW_H >= top && y <= top + viewH) {
			if (focused)
				fcFocusRing(d, FC_GRID * 0.5f, y, FC_BOT_W - FC_GRID, ROW_H);

			fcTextEllipsized(d, fam->family, FC_GRID * 1.5f, y + 4.0f, 0.0f,
			                   0.40f, p->ink, FC_BOT_W - FC_GRID * 5.0f);
			fcTextEllipsized(d, fam->category, FC_GRID * 1.5f, y + 18.0f, 0.0f,
			                   0.30f, p->inkFaint, FC_BOT_W - FC_GRID * 5.0f);

			C2D_DrawRectSolid(FC_GRID, y + ROW_H - 1.0f, 0.0f,
			                  FC_BOT_W - FC_GRID * 2, 1.0f, p->cardEdge);
		}

		if (activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H)) {
			FcFontEntry entry;
			if (fcFontEntryFromGoogle(&entry, fam->family, app->config->slot)) {
				openEntry(&entry);
				return -1.0f;
			}
		}

		y += ROW_H;
	}

	if (s_browse.matchCount == 0) {
		fcText(d, "Nothing matches that.", FC_BOT_W / 2.0f, y + FC_GRID,
		         0.0f, 0.34f, p->inkFaint, FC_ALIGN_CENTER);
		y += FC_GRID * 4.0f;
	} else if (s_browse.truncated) {
		fcText(d, "More matches than fit. Narrow the search.", FC_BOT_W / 2.0f,
		         y + FC_GRID, 0.0f, 0.30f, p->inkFaint, FC_ALIGN_CENTER);
		y += FC_GRID * 4.0f;
	}

	return y;
}

static void drawListPage(FcApp *app, const FcDraw *d, float top, float viewH)
{
	const FcPalette *p = &app->theme.pal;
	const FcCatalog *cat = fcFontFeedCatalog();

	float y = top - s_browse.scroll.offset;

	y = drawSourceSwitch(app, d, y);

	if (s_browse.source == SOURCE_GOOGLE) {
		y = drawSearchRow(app, d, y);
		y = drawGoogleList(app, d, y, top, viewH);
		if (y < 0.0f)
			return;

		s_browse.scroll.contentHeight =
			(y + s_browse.scroll.offset) - top + FC_GRID;
		return;
	}

	for (int i = 0; i < cat->count; i++) {
		const FcFontEntry *e = &cat->items[i];

		bool focused = false;
		const bool activated = fcFocusItem(&s_browse.focus, y, ROW_H, &focused);

		if (y + ROW_H >= top && y <= top + viewH) {
			if (focused)
				fcFocusRing(d, FC_GRID * 0.5f, y, FC_BOT_W - FC_GRID, ROW_H);

			fcTextEllipsized(d, e->name, FC_GRID * 1.5f, y + 4.0f, 0.0f, 0.40f,
			                   p->ink, FC_BOT_W - FC_GRID * 5.0f);

			char sub[96];
			const FcFontSlotInfo *slot = fcFontSlotInfo(e->slot);
			snprintf(sub, sizeof sub, "%s%s%s",
			         e->author[0] ? e->author : "unknown",
			         slot && e->slot != FC_SLOT_STD ? " - " : "",
			         slot && e->slot != FC_SLOT_STD ? slot->label : "");
			fcTextEllipsized(d, sub, FC_GRID * 1.5f, y + 18.0f, 0.0f, 0.30f,
			                   p->inkFaint, FC_BOT_W - FC_GRID * 5.0f);

			if (!fcFontEntryInstallable(e))
				fcChip(d, "preview", FC_BOT_W - FC_GRID * 8.0f, y + 8.0f,
				         p->warn);

			C2D_DrawRectSolid(FC_GRID, y + ROW_H - 1.0f, 0.0f,
			                  FC_BOT_W - FC_GRID * 2, 1.0f, p->cardEdge);
		}

		if (activated || fcTouchTapped(&app->touch, 0, y, FC_BOT_W, ROW_H)) {
			openEntry(e);
			return;
		}

		y += ROW_H;
	}

	if (cat->count == 0) {
		const char *msg = app->networkUp
			? "No fonts yet. Refresh to fetch the catalog."
			: "No fonts, and no network to fetch them with.";
		fcText(d, msg, FC_BOT_W / 2.0f, y + FC_GRID * 2.0f, 0.0f, 0.34f,
		         p->inkFaint, FC_ALIGN_CENTER);
		y += FC_GRID * 5.0f;
	}

	y += FC_GRID;
	{
		const float x = FC_GRID;
		const float w = FC_BOT_W - FC_GRID * 2.0f;

		bool focused = false;
		const bool activated = fcFocusItem(&s_browse.focus, y, BUTTON_H, &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, BUTTON_H);
		const bool ready = app->networkUp && !fcFontFeedBusy();

		fcButton(d, fcFontFeedBusy() ? "Working..." : "Refresh catalog",
		           x, y, w, BUTTON_H,
		           !ready ? FC_BTN_DISABLED : (held ? FC_BTN_ACTIVE : FC_BTN_NORMAL));
		if (focused)
			fcFocusRing(d, x, y, w, BUTTON_H);

		if (ready && (activated || fcTouchTapped(&app->touch, x, y, w, BUTTON_H))) {
			fcFontFeedAcknowledge();
			if (!fcFontFeedRefresh(app->config->catalogUrl))
				s_browse.wantRefresh = true;
		}

		y += BUTTON_H;
	}

	s_browse.scroll.contentHeight = (y + s_browse.scroll.offset) - top + FC_GRID;
}

static void drawDetailPage(FcApp *app, const FcDraw *d, float top)
{
	const FcPalette *p = &app->theme.pal;
	const FcFontEntry *e = &s_browse.entry;

	float y = top - s_browse.scroll.offset;

	char line[192];
	const FcFontSlotInfo *slot = fcFontSlotInfo(e->slot);

	snprintf(line, sizeof line, "by %s", e->author[0] ? e->author : "unknown");
	fcTextEllipsized(d, line, FC_GRID, y, 0.0f, 0.34f, p->inkDim,
	                   FC_BOT_W - FC_GRID * 2.0f);
	y += FC_GRID * 2.5f;

	if (e->license[0]) {
		snprintf(line, sizeof line, "licence: %s", e->license);
		fcTextEllipsized(d, line, FC_GRID, y, 0.0f, 0.32f, p->inkFaint,
		                   FC_BOT_W - FC_GRID * 2.0f);
		y += FC_GRID * 2.5f;
	}

	snprintf(line, sizeof line, "replaces the %s font", slot ? slot->label : "?");
	fcText(d, line, FC_GRID, y, 0.0f, 0.32f, p->inkFaint, FC_ALIGN_LEFT);
	y += FC_GRID * 2.5f;

	if (e->cia.size > 0) {
		snprintf(line, sizeof line, "%u KB%s", (unsigned)(e->cia.size / 1024),
		         e->cia.sha256[0] ? ", checksummed" : ", no checksum");
		fcText(d, line, FC_GRID, y, 0.0f, 0.32f,
		         e->cia.sha256[0] ? p->inkFaint : p->warn, FC_ALIGN_LEFT);
		y += FC_GRID * 2.5f;
	}

	if (e->notes[0]) {
		y += FC_GRID * 0.5f;
		fcTextEllipsized(d, e->notes, FC_GRID, y, 0.0f, 0.32f, p->inkDim,
		                   FC_BOT_W - FC_GRID * 2.0f);
		y += FC_GRID * 3.0f;
	}

	{
		FcMergeReport m;
		fcFontFeedMergeReport(&m);

		if (s_browse.previewFont && m.requested > 0) {
			snprintf(line, sizeof line, "replaces %d of %d characters",
			         m.replaced, m.requested);
			fcText(d, line, FC_GRID, y, 0.0f, 0.32f,
			         m.replaced * 4 >= m.requested * 3 ? p->good : p->warn,
			         FC_ALIGN_LEFT);
			y += FC_GRID * 3.0f;
		}
	}

	FcJobStatus job;
	fcFontFeedStatus(&job);

	const float x = FC_GRID;
	const float w = FC_BOT_W - FC_GRID * 2.0f;

	if (fcFontEntryPreviewable(e) && !s_browse.previewFont) {
		bool focused = false;
		const bool activated = fcFocusItem(&s_browse.focus, y, BUTTON_H, &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, BUTTON_H);
		const bool ready = app->networkUp && !fcFontFeedBusy();

		fcButton(d, "Preview", x, y, w, BUTTON_H,
		           !ready ? FC_BTN_DISABLED
		                  : (held ? FC_BTN_ACTIVE : FC_BTN_NORMAL));
		if (focused)
			fcFocusRing(d, x, y, w, BUTTON_H);

		if (ready && (activated || fcTouchTapped(&app->touch, x, y, w, BUTTON_H))) {
			fcFontFeedAcknowledge();
			fcFontFeedFetchPreview(e);
		}

		y += BUTTON_H + FC_GRID;
	}

	if (s_browse.previewFont &&
	    (e->googleFamily[0] || e->ttf.url[0])) {
		const float bw = 34.0f;
		const float minusX = FC_BOT_W - FC_GRID - bw * 2 - FC_GRID;
		const float plusX  = FC_BOT_W - FC_GRID - bw;
		const int adjust = fcFontFeedSizeAdjust();
		const bool ready = !fcFontFeedBusy();

		char sz[32];
		if (adjust == 0)
			snprintf(sz, sizeof sz, "Size: default");
		else
			snprintf(sz, sizeof sz, "Size: %+d px", adjust);
		fcText(d, sz, FC_GRID, y + 8.0f, 0.0f, 0.34f, p->inkDim, FC_ALIGN_LEFT);

		int delta = 0;

		bool f1 = false;
		const bool minusHit = fcFocusItemInRow(&s_browse.focus,
		                                         s_browse.focus.rowCursor,
		                                         y, BUTTON_H, &f1);
		const bool canDown = ready && adjust > FC_SIZE_ADJUST_MIN;
		fcButton(d, "-", minusX, y, bw, BUTTON_H,
		           canDown ? FC_BTN_NORMAL : FC_BTN_DISABLED);
		if (f1)
			fcFocusRing(d, minusX, y, bw, BUTTON_H);
		if (canDown && (minusHit ||
		                fcTouchTapped(&app->touch, minusX, y, bw, BUTTON_H)))
			delta = -1;

		bool f2 = false;
		const bool plusHit = fcFocusItemInRow(&s_browse.focus,
		                                        s_browse.focus.rowCursor,
		                                        y, BUTTON_H, &f2);
		const bool canUp = ready && adjust < FC_SIZE_ADJUST_MAX;
		fcButton(d, "+", plusX, y, bw, BUTTON_H,
		           canUp ? FC_BTN_NORMAL : FC_BTN_DISABLED);
		if (f2)
			fcFocusRing(d, plusX, y, bw, BUTTON_H);
		if (canUp && (plusHit ||
		              fcTouchTapped(&app->touch, plusX, y, bw, BUTTON_H)))
			delta = 1;

		if (delta != 0) {
			app->config->fontSizeAdjust += delta;
			fcFontFeedSetSizeAdjust(app->config->fontSizeAdjust);
			fcConfigSave(app->config);

			fcFontFeedAcknowledge();
			if (!fcFontFeedRepreview())
				s_browse.notice = "Busy -- try that again in a moment.";
		}

		y += BUTTON_H + FC_GRID;
	}

	{
		const bool installable = fcFontEntryInstallable(e);
		const bool ready = installable && app->networkUp && !fcFontFeedBusy() &&
		                   fcFontInstallReady();

		bool focused = false;
		const bool activated = fcFocusItem(&s_browse.focus, y, BUTTON_H, &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, BUTTON_H);

		const char *label = "Install";
		if (!installable)
			label = "No installer for this font";
		else if (!fcFontInstallReady())
			label = "Cannot install (no AM access)";
		else if (job.kind == FC_JOB_INSTALL && job.state == FC_JOB_RUNNING)
			label = "Installing...";

		fcButton(d, label, x, y, w, BUTTON_H,
		           !ready ? FC_BTN_DISABLED
		                  : (held ? FC_BTN_ACTIVE : FC_BTN_NORMAL));
		if (focused)
			fcFocusRing(d, x, y, w, BUTTON_H);

		if (ready && (activated || fcTouchTapped(&app->touch, x, y, w, BUTTON_H))) {
			fcFontFeedAcknowledge();

			if (!app->config->acknowledgedRisk) {
				s_browse.page = PAGE_CONFIRM;
				fcFocusReset(&s_browse.focus);
				return;
			}

			beginInstall(e);
		}

		y += BUTTON_H + FC_GRID;
	}

	if (job.state == FC_JOB_RUNNING && job.percent >= 0) {
		C2D_DrawRectSolid(x, y, 0.0f, w, 4.0f, p->cardEdge);
		C2D_DrawRectSolid(x, y, 0.0f, w * (float)job.percent / 100.0f, 4.0f,
		                  p->accent);
		y += FC_GRID * 2.0f;
	}

	if (s_browse.notice) {
		fcText(d, s_browse.notice, x, y, 0.0f, 0.32f, p->warn, FC_ALIGN_LEFT);
		y += FC_GRID * 3.0f;
	}

	if (job.message[0] && job.kind != FC_JOB_CATALOG) {
		fcTextEllipsized(d, job.message, x, y, 0.0f, 0.32f,
		                   job.state == FC_JOB_FAILED ? p->bad : p->good, w);
		y += FC_GRID * 3.0f;
	}

	s_browse.scroll.contentHeight = (y + s_browse.scroll.offset) - top + FC_GRID;
}

static void drawConfirmPage(FcApp *app, const FcDraw *d, float top)
{
	const FcPalette *p = &app->theme.pal;

	float y = top;

	fcText(d, "This writes to NAND.", FC_GRID, y, 0.0f, 0.42f, p->warn,
	         FC_ALIGN_LEFT);
	y += FC_GRID * 3.0f;

	static const char *const kWarning[] = {
		"Installing a font replaces a system",
		"title, not a file on the SD card.",
		"",
		"boot9strap keeps this recoverable:",
		"GodMode9 can put the original back.",
		"Nothing here can undo it for you.",
	};

	for (size_t i = 0; i < sizeof kWarning / sizeof kWarning[0]; i++) {
		fcText(d, kWarning[i], FC_GRID, y, 0.0f, 0.32f, p->inkDim,
		         FC_ALIGN_LEFT);
		y += FC_GRID * 1.8f;
	}

	y += FC_GRID;

	const float w = (FC_BOT_W - FC_GRID * 3.0f) / 2.0f;

	bool cancelFocused = false, goFocused = false;
	const bool cancelHit = fcFocusItemInRow(&s_browse.focus, 0, y, BUTTON_H,
	                                          &cancelFocused);
	const bool goHit = fcFocusItemInRow(&s_browse.focus, 0, y, BUTTON_H,
	                                      &goFocused);

	const float cancelX = FC_GRID;
	const float goX     = FC_GRID * 2.0f + w;

	fcButton(d, "Back", cancelX, y, w, BUTTON_H,
	           app->touch.down && fcTouchInRect(&app->touch, cancelX, y, w, BUTTON_H)
	               ? FC_BTN_ACTIVE : FC_BTN_NORMAL);
	if (cancelFocused)
		fcFocusRing(d, cancelX, y, w, BUTTON_H);

	fcButton(d, "I understand", goX, y, w, BUTTON_H,
	           app->touch.down && fcTouchInRect(&app->touch, goX, y, w, BUTTON_H)
	               ? FC_BTN_ACTIVE : FC_BTN_NORMAL);
	if (goFocused)
		fcFocusRing(d, goX, y, w, BUTTON_H);

	if (cancelHit || fcTouchTapped(&app->touch, cancelX, y, w, BUTTON_H)) {
		s_browse.page = PAGE_DETAIL;
		fcFocusReset(&s_browse.focus);
		return;
	}

	if (goHit || fcTouchTapped(&app->touch, goX, y, w, BUTTON_H)) {
		app->config->acknowledgedRisk = true;
		fcConfigSave(app->config);

		s_browse.page = PAGE_DETAIL;
		fcFocusReset(&s_browse.focus);
		beginInstall(&s_browse.entry);
		return;
	}

	s_browse.scroll.contentHeight = 0.0f;
}

static void drawBottom(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;
	const u32 down = hidKeysDown();

	if ((down & KEY_B) && s_browse.page != PAGE_LIST) {
		s_browse.page = s_browse.page == PAGE_CONFIRM ? PAGE_DETAIL : PAGE_LIST;
		s_browse.scroll.offset = 0.0f;
		fcFocusReset(&s_browse.focus);
	}

	const float listTop = FC_GRID * 4.0f;
	const float listH   = FC_BOT_H - FC_TABBAR_H - listTop;

	const char *title = "Fonts";
	if (s_browse.page == PAGE_DETAIL)
		title = s_browse.entry.name;
	else if (s_browse.page == PAGE_CONFIRM)
		title = "Before you install";

	fcTextEllipsized(d, title, FC_GRID, FC_GRID, 0.0f, 0.46f, p->ink,
	                   FC_BOT_W - FC_GRID * 10.0f);
	if (s_browse.page != PAGE_LIST)
		fcText(d, "B to go back", FC_BOT_W - FC_GRID, FC_GRID + 2.0f, 0.0f,
		         0.30f, p->inkFaint, FC_ALIGN_RIGHT);

	fcFocusBegin(&s_browse.focus, app->buttonMode, down);

	fcClipBegin(d, 0.0f, listTop, FC_BOT_W, listH);

	switch (s_browse.page) {
	case PAGE_LIST:    drawListPage(app, d, listTop, listH); break;
	case PAGE_DETAIL:  drawDetailPage(app, d, listTop);      break;
	case PAGE_CONFIRM: drawConfirmPage(app, d, listTop);     break;
	}

	fcClipEnd();

	fcFocusEnd(&s_browse.focus);

	s_browse.scroll.viewHeight = listH;
	fcScrollUpdate(&s_browse.scroll, &app->touch,
	                 app->touch.y < FC_BOT_H - FC_TABBAR_H, app->frameDt);
	fcFocusScrollIntoView(&s_browse.focus, &s_browse.scroll, listTop, listH);

	fcScrollbar(d, FC_BOT_W - 4.0f, listTop, listH, s_browse.scroll.offset,
	              s_browse.scroll.contentHeight, listH);
}

const FcSceneVTable fcSceneBrowse = {
	.tabLabel   = "Fonts",
	.enter      = enter,
	.update     = update,
	.drawTop    = drawTop,
	.drawBottom = drawBottom,
};
