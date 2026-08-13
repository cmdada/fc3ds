#include "app/app.h"
#include "app/config.h"
#include "app/textinput.h"
#include "data/catalog.h"
#include "qr/qrscan.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#define ROW_H 32.0f

typedef struct {
	FcQrScanner qr;
	FcFocus     focus;
	char        error[96];

	bool        wantKeyboard;
} ScanState;

static ScanState s_scan;

static void enter(FcApp *app)
{
	(void)app;
	fcFocusReset(&s_scan.focus);
	s_scan.error[0] = '\0';
}

static void leave(FcApp *app)
{
	(void)app;
	fcQrStop(&s_scan.qr);
}

static void acceptUrl(FcApp *app, const char *url)
{
	FcFontEntry entry;

	if (!fcFontEntryFromUrl(&entry, url, app->config->slot)) {
		snprintf(s_scan.error, sizeof s_scan.error,
		         "That is not an http(s) address.");
		return;
	}

	s_scan.error[0] = '\0';
	fcQrStop(&s_scan.qr);
	fcBrowseOpenEntry(app, &entry);
}

static void update(FcApp *app, float dt)
{
	(void)dt;

	if (s_scan.wantKeyboard) {
		s_scan.wantKeyboard = false;

		char url[256] = "https://";

		fcQrStop(&s_scan.qr);

		if (fcTextInputAsk(app, "Font or catalog address", url, url, sizeof url))
			acceptUrl(app, url);
		return;
	}

	if (s_scan.qr.state == FC_QR_SCANNING || s_scan.qr.state == FC_QR_STARTING)
		fcQrUpdate(&s_scan.qr);

	if (s_scan.qr.state == FC_QR_FOUND && s_scan.qr.result[0])
		acceptUrl(app, s_scan.qr.result);
}

static void drawTop(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;

	if (s_scan.qr.state == FC_QR_SCANNING || s_scan.qr.state == FC_QR_FOUND) {
		C2D_Image preview = fcQrPreview();
		if (preview.tex && preview.subtex)
			C2D_DrawImageAt(preview, 0.0f, 0.0f, 0.0f, NULL, 1.0f, 1.0f);

		if (s_scan.qr.sawCode)
			fcText(d, "code visible...", FC_TOP_W / 2.0f,
			         FC_TOP_H - FC_GRID * 3.0f, FC_DEPTH_FLOAT, 0.40f,
			         p->accent, FC_ALIGN_CENTER);
		return;
	}

	fcBackdrop(d, d->screenW, d->screenH);

	const float cardX = FC_GRID * 3.0f;
	const float cardY = FC_GRID * 4.0f;
	const float cardW = FC_TOP_W - FC_GRID * 6.0f;
	const float cardH = FC_TOP_H - FC_GRID * 8.0f;
	fcCard(d, cardX, cardY, cardW, cardH, FC_DEPTH_CARD, &d->eye);

	const float left = cardX + FC_GRID * 2.0f;
	float y = cardY + FC_GRID * 1.5f;

	fcText(d, "Add a font by address", left, y, FC_DEPTH_CONTENT, 0.50f,
	         p->ink, FC_ALIGN_LEFT);
	y += FC_GRID * 4.0f;

	static const char *const kLines[] = {
		"Scan a QR code holding a link, or",
		"type one in. A .cia address can be",
		"installed; anything else is treated",
		"as a font to preview only.",
	};

	for (size_t i = 0; i < sizeof kLines / sizeof kLines[0]; i++) {
		fcText(d, kLines[i], left, y, FC_DEPTH_CONTENT, 0.34f, p->inkDim,
		         FC_ALIGN_LEFT);
		y += FC_GRID * 2.2f;
	}

	y += FC_GRID;

	if (s_scan.error[0]) {
		fcTextEllipsized(d, s_scan.error, left, y, FC_DEPTH_CONTENT, 0.34f,
		                   p->bad, cardW - FC_GRID * 4.0f);
	} else if (s_scan.qr.state == FC_QR_FAILED) {
		fcTextEllipsized(d,
		                   s_scan.qr.error[0] ? s_scan.qr.error
		                                      : "Camera unavailable.",
		                   left, y, FC_DEPTH_CONTENT, 0.34f, p->bad,
		                   cardW - FC_GRID * 4.0f);
		y += FC_GRID * 2.2f;
		fcText(d, "A .3dsx inherits hbmenu's permissions;", left, y,
		         FC_DEPTH_CONTENT, 0.30f, p->inkFaint, FC_ALIGN_LEFT);
		y += FC_GRID * 1.8f;
		fcText(d, "the CIA build carries its own.", left, y, FC_DEPTH_CONTENT,
		         0.30f, p->inkFaint, FC_ALIGN_LEFT);
	}
}

static void drawBottom(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;

	fcFocusBegin(&s_scan.focus, app->buttonMode, hidKeysDown());

	fcText(d, "Add by address", FC_GRID, FC_GRID, 0.0f, 0.46f, p->ink,
	         FC_ALIGN_LEFT);

	const bool live = s_scan.qr.state == FC_QR_SCANNING ||
	                  s_scan.qr.state == FC_QR_STARTING;

	const float x = FC_GRID;
	const float w = FC_BOT_W - FC_GRID * 2.0f;
	float y = FC_GRID * 5.0f;

	{
		bool focused = false;
		const bool activated = fcFocusItem(&s_scan.focus, y, ROW_H, &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, ROW_H);

		fcButton(d, live ? "Stop the camera" : "Scan a QR code", x, y, w, ROW_H,
		           held ? FC_BTN_ACTIVE : FC_BTN_NORMAL);
		if (focused)
			fcFocusRing(d, x, y, w, ROW_H);

		if (activated || fcTouchTapped(&app->touch, x, y, w, ROW_H)) {
			s_scan.error[0] = '\0';
			if (live)
				fcQrStop(&s_scan.qr);
			else
				fcQrStart(&s_scan.qr);
		}

		y += ROW_H + FC_GRID;
	}

	{
		bool focused = false;
		const bool activated = fcFocusItem(&s_scan.focus, y, ROW_H, &focused);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, ROW_H);

		fcButton(d, "Type an address", x, y, w, ROW_H,
		           held ? FC_BTN_ACTIVE : FC_BTN_NORMAL);
		if (focused)
			fcFocusRing(d, x, y, w, ROW_H);

		if (activated || fcTouchTapped(&app->touch, x, y, w, ROW_H))
			s_scan.wantKeyboard = true;

		y += ROW_H + FC_GRID * 2.0f;
	}

	if (s_scan.qr.diag[0] && !live)
		fcTextEllipsized(d, s_scan.qr.diag, FC_GRID, y, 0.0f, 0.28f,
		                   p->inkFaint, FC_BOT_W - FC_GRID * 2.0f);

	fcFocusEnd(&s_scan.focus);
}

const FcSceneVTable fcSceneScan = {
	.tabLabel   = "Add",
	.enter      = enter,
	.leave      = leave,
	.update     = update,
	.drawTop    = drawTop,
	.drawBottom = drawBottom,
};
