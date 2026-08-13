#include "app/textinput.h"

#include "app/app.h"
#include "app/config.h"
#include "ui/widgets.h"

#include <3ds.h>
#include <ctrosk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------- system keyboard */

static bool askSystem(const char *hint, const char *initial,
                      char *dst, size_t dstSize)
{
	SwkbdState swkbd;
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)dstSize - 1);
	swkbdSetHintText(&swkbd, hint ? hint : "");
	swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);

	if (initial && *initial)
		swkbdSetInitialText(&swkbd, initial);

	/*
	 * The applet owns both screens until it returns, so anything animating in
	 * our own loop is paused for the duration. That is fine -- the user is
	 * looking at the keyboard, not at us -- but it does mean the frame after
	 * this call sees a large dt.
	 */
	const SwkbdButton button = swkbdInputText(&swkbd, dst, dstSize);

	if (button != SWKBD_BUTTON_CONFIRM) {
		dst[0] = '\0';
		return false;
	}

	return true;
}

/* -------------------------------------------------------- touch keyboard */

/* citro2d packs colours as 0xAABBGGRR; ctr-osk-rt wants the three bytes. */
static CtrOskColor oskColor(u32 c)
{
	const CtrOskColor out = { (u8)(c & 0xFF), (u8)((c >> 8) & 0xFF),
	                          (u8)((c >> 16) & 0xFF) };
	return out;
}

static u8 clamp8(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : (u8)v);
}

static CtrOskColor oskShade(u32 c, int step)
{
	const CtrOskColor out = { clamp8((int)(c & 0xFF) + step),
	                          clamp8((int)((c >> 8) & 0xFF) + step),
	                          clamp8((int)((c >> 16) & 0xFF) + step) };
	return out;
}

/// @p c mixed down onto @p onto, with @p t of the original left.
static CtrOskColor oskMix(u32 c, u32 onto, float t)
{
	const CtrOskColor out = {
		clamp8((int)((float)(c & 0xFF) * t + (float)(onto & 0xFF) * (1.0f - t))),
		clamp8((int)((float)((c >> 8) & 0xFF) * t + (float)((onto >> 8) & 0xFF) * (1.0f - t))),
		clamp8((int)((float)((c >> 16) & 0xFF) * t + (float)((onto >> 16) & 0xFF) * (1.0f - t))),
	};
	return out;
}

static int oskLuma(u32 c)
{
	return (299 * (int)(c & 0xFF) + 587 * (int)((c >> 8) & 0xFF) +
	        114 * (int)((c >> 16) & 0xFF)) / 1000;
}

static void oskThemeFrom(const FcTheme *theme, CtrOskTheme *out)
{
	const FcPalette *p = &theme->pal;

	/*
	 * ctr-osk-rt spaces its three tiers -- background, function key, letter key
	 * -- evenly and widely, because the touch panel is dim and sits under a
	 * resistive overlay: tiers that separate cleanly on a monitor collapse into
	 * each other on hardware. Rose Pine's base, surface and overlay are much
	 * closer together than that, so the key faces are stepped off the ground by
	 * a fixed amount rather than taken from the palette.
	 */
	const int step = theme->dark ? 17 : -17;

	out->bg         = oskColor(p->ground);
	out->status_bar = oskColor(p->ground);
	out->mod        = oskShade(p->ground, step);
	out->key        = oskShade(p->ground, step * 2);

	out->accent     = oskColor(p->accent);
	out->accent_dim = oskMix(p->accent, p->ground, 0.28f);
	out->warn       = oskColor(p->warn);
	out->warn_dim   = oskMix(p->warn, p->ground, 0.28f);

	out->text       = oskColor(p->ink);
	out->text_dim   = oskColor(p->inkDim);
	out->title      = oskColor(p->ink);

	/* Enter is a solid accent fill, so its label follows the accent rather than
	 * the theme -- the light palette's accent is no lighter than the dark one's. */
	const CtrOskColor onLight = { 0x14, 0x16, 0x1c };
	const CtrOskColor onDark  = { 0xF2, 0xF2, 0xF6 };
	out->text_on = oskLuma(p->accent) > 140 ? onLight : onDark;
}

/*
 * Set when the app comes back from the HOME menu or from sleep. Both leave the
 * bottom screen holding someone else's pixels, and the keyboard only repaints
 * what it believes changed, so it has to be told the whole thing is stale.
 */
static volatile bool s_oskStale;

static void oskAptHook(APT_HookType hook, void *user)
{
	(void)user;

	if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP)
		s_oskStale = true;
}

/**
 * Push the keyboard's framebuffer writes to the panel.
 *
 * The keyboard composes with the CPU, so the data cache has to reach memory
 * before GSP scans it out. gfxFlushBuffers() would flush the top screen's
 * buffer too, which citro3d owns and has already transferred this frame.
 */
static void presentBottom(void)
{
	u16 w = 0, h = 0;
	u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
	if (!fb)
		return;

	GSPGPU_FlushDataCache(fb, (u32)w * (u32)h * 3);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

/**
 * Longest tail of @p s that fits in @p maxW, converged by ratio.
 *
 * The tail rather than the head because the caret is at the end: what a person
 * needs to see is what they just typed. Ratio rather than a character at a time
 * because fcTextWidth parses into a text buffer that is only reclaimed on the
 * next clear -- trimming a 512-character URL one character at a time would
 * exhaust the glyph budget, after which every later draw silently renders
 * nothing.
 */
static const char *tailToFit(const FcDraw *d, const char *s, float scale,
                             float maxW)
{
	for (int i = 0; i < 4; i++) {
		const float w = fcTextWidth(d, s, scale);
		if (w <= maxW)
			break;

		const size_t len = strlen(s);
		size_t drop = (size_t)((float)len * (1.0f - maxW / w)) + 1;
		if (drop > len)
			drop = len;
		s += drop;
	}

	return s;
}

static void drawEntry(const FcDraw *d, const char *hint, const char *text,
                      size_t max, float blink)
{
	const FcPalette *p = &d->theme->pal;

	fcBackdrop(d, d->screenW, d->screenH);

	const float cardX = FC_GRID * 3.0f;
	const float cardY = FC_GRID * 6.0f;
	const float cardW = FC_TOP_W - FC_GRID * 6.0f;
	const float cardH = FC_GRID * 13.0f;
	fcCard(d, cardX, cardY, cardW, cardH, FC_DEPTH_CARD, &d->eye);

	fcText(d, hint ? hint : "Text entry", cardX + FC_GRID * 1.5f,
	         cardY + FC_GRID * 1.5f, FC_DEPTH_CONTENT, 0.46f, p->inkDim,
	         FC_ALIGN_LEFT);

	char count[24];
	snprintf(count, sizeof count, "%d/%d", (int)strlen(text), (int)max);
	fcText(d, count, cardX + cardW - FC_GRID * 1.5f, cardY + FC_GRID * 1.75f,
	         FC_DEPTH_CONTENT, 0.36f, p->inkFaint, FC_ALIGN_RIGHT);

	const float fieldX = cardX + FC_GRID * 1.5f;
	const float fieldY = cardY + FC_GRID * 4.5f;
	const float fieldW = cardW - FC_GRID * 3.0f;
	const float fieldH = FC_GRID * 4.0f;
	fcRoundedRect(fieldX, fieldY, fieldW, fieldH, FC_RADIUS,
	                FC_DEPTH_CONTENT, p->hlLow, &d->eye);

	const float pad   = FC_GRID;
	const float scale = 0.5f;

	/* Room for the caret is reserved, so it never sits outside the field. */
	const char *shown = tailToFit(d, text, scale, fieldW - pad * 2.0f - 4.0f);

	if (*shown) {
		fcText(d, shown, fieldX + pad, fieldY + FC_GRID, FC_DEPTH_CONTENT,
		         scale, p->ink, FC_ALIGN_LEFT);
	} else {
		/* Indented clear of the caret, which sits at the left edge of an
		 * empty field and would otherwise be drawn over the first letter. */
		fcText(d, "empty", fieldX + pad + FC_GRID, fieldY + FC_GRID,
		         FC_DEPTH_CONTENT, scale, p->inkFaint, FC_ALIGN_LEFT);
	}

	if (fmodf(blink, 1.0f) < 0.55f) {
		const float caretX = fieldX + pad + fcTextWidth(d, shown, scale) + 2.0f;
		fcRoundedRect(caretX, fieldY + FC_GRID * 0.75f, 2.0f,
		                FC_GRID * 2.5f, 1.0f, FC_DEPTH_CONTENT, p->accent,
		                &d->eye);
	}

	fcText(d, "ENT or A accepts. ESC or B cancels.", FC_TOP_W / 2.0f,
	         cardY + cardH + FC_GRID, FC_DEPTH_CONTENT, 0.36f, p->inkFaint,
	         FC_ALIGN_CENTER);
}

static bool askTouch(FcApp *app, const char *hint, const char *initial,
                     char *dst, size_t dstSize)
{
	const size_t max = dstSize - 1;

	if (initial && *initial)
		snprintf(dst, dstSize, "%s", initial);
	size_t len = strlen(dst);

	CtrOskTheme theme;
	oskThemeFrom(&app->theme, &theme);

	CtrOsk osk;
	ctrOskInit(&osk);   /* Also turns off double buffering on the bottom screen. */
	osk.theme    = &theme;
	osk.title    = hint ? hint : "Text entry";
	osk.subtitle = "ENT accepts   ESC cancels";
	/* A text field, not a terminal: backspace is BS, not DEL. */
	osk.backspace_as_del = false;

	s_oskStale = false;
	aptHookCookie cookie;
	aptHook(&cookie, oskAptHook, NULL);

	bool done = false, confirmed = false;
	float blink = 0.0f;
	u64 lastTick = osGetTime();

	while (!done && aptMainLoop()) {
		hidScanInput();
		const u32 down = hidKeysDown();

		CtrOskEvent ev;
		if (ctrOskUpdate(&osk, &ev)) {
			switch (ev.key) {
			case CTR_OSK_KEY_ENTER:
				/* The same rule the applet is given by SWKBD_NOTBLANK_NOTEMPTY:
				 * an empty field is not an answer, so Enter does nothing. */
				if (len > 0)
					confirmed = done = true;
				break;

			case CTR_OSK_KEY_ESC:
				done = true;
				break;

			case CTR_OSK_KEY_BKSP:
				if (len > 0)
					dst[--len] = '\0';
				break;

			default:
				/* Ctrl combinations are control codes; there is nothing for a
				 * text field to do with one. */
				if (!ev.ctrl && ev.byte >= 32 && ev.byte <= 126 && len < max) {
					dst[len++] = (char)ev.byte;
					dst[len]   = '\0';
				}
				break;
			}

			blink = 0.0f;   /* Hold the caret solid while keys are landing. */
		}

		/*
		 * This keyboard is touch-only by nature, but leaving no button path at
		 * all strands anyone driving the app from the D-pad with the console on
		 * a stand -- which is the case the rest of the UI is built for.
		 */
		if (down & KEY_B)
			done = true;
		if ((down & KEY_A) && len > 0)
			confirmed = done = true;

		const u64 nowTick = osGetTime();
		float dt = (float)(nowTick - lastTick) / 1000.0f;
		lastTick = nowTick;
		if (dt > 0.25f)
			dt = 0.25f;
		blink += dt;

		fcStereoUpdate(&app->stereo);

		C2D_TextBufClear(app->textBuf);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		FcDraw d = { .theme = &app->theme, .textBuf = app->textBuf,
		               .screenW = FC_TOP_W, .screenH = FC_TOP_H };

		/*
		 * Only the top screen goes through citro2d here. The bottom one belongs
		 * to the keyboard, which writes into its framebuffer directly, and
		 * citro3d transfers only the targets that were drawn on this frame --
		 * so leaving the bottom target alone is exactly what keeps C3D_FrameEnd
		 * from overwriting the keyboard.
		 */
		C2D_TargetClear(app->stereo.topLeft, app->theme.pal.ground);
		C2D_SceneBegin(app->stereo.topLeft);
		d.eye = fcEyeFor(&app->stereo, GFX_LEFT);
		drawEntry(&d, hint, dst, max, blink);

		if (app->stereo.stereo) {
			C2D_TargetClear(app->stereo.topRight, app->theme.pal.ground);
			C2D_SceneBegin(app->stereo.topRight);
			d.eye = fcEyeFor(&app->stereo, GFX_RIGHT);
			drawEntry(&d, hint, dst, max, blink);
		}

		C3D_FrameEnd(0);

		if (s_oskStale) {
			ctrOskInvalidate(&osk);
			s_oskStale = false;
		}
		ctrOskDraw(&osk);
		presentBottom();
	}

	aptUnhook(&cookie);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);

	/*
	 * The keyboard fires on touch-down, so the finger that pressed ENT is
	 * usually still on the panel. Sample it and mark the touch as a drag: left
	 * alone, that release lands as a tap on whatever the scene underneath has
	 * where the key was, and the button that opened the keyboard is a common
	 * thing to find there.
	 */
	fcTouchUpdate(&app->touch);
	app->touch.travel = 1000.0f;

	if (!confirmed)
		dst[0] = '\0';

	return confirmed;
}

/* ------------------------------------------------------------- dispatch */

bool fcTextInputAsk(struct FcApp *appOpaque, const char *hint,
                      const char *initial, char *dst, size_t dstSize)
{
	FcApp *app = (FcApp *)appOpaque;

	if (!dst || dstSize < 2)
		return false;

	dst[0] = '\0';

	if (app && app->config && app->config->keyboard == FC_KEYBOARD_TOUCH)
		return askTouch(app, hint, initial, dst, dstSize);

	return askSystem(hint, initial, dst, dstSize);
}
