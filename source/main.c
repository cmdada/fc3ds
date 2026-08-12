#include <3ds.h>
#include <citro2d.h>

#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/config.h"
#include "app/fontfeed.h"
#include "app/fontinstall.h"
#include "app/log.h"
#include "net/cache.h"
#include "net/http.h"
#include "net/sntp.h"
#include "net/worker.h"
#include "ui/stereo.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#define TEXT_BUF_GLYPHS 4096

static FcApp       s_app;
static FcConfig    s_config;
static aptHookCookie s_aptCookie;

static void fcAptHook(APT_HookType hook, void *param)
{
	FcApp *app = (FcApp *)param;
	(void)app;

	switch (hook) {
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		fcWorkerPause(true);
		break;

	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		osSetSpeedupEnable(true);
		fcWorkerPause(false);
		break;

	case APTHOOK_ONEXIT:
		fcWorkerPause(true);
		break;

	default:
		break;
	}
}

#define FC_RETRY_SECONDS 30

static volatile bool s_timeSyncRunning;

static void timeSyncJob(void *user)
{
	(void)user;

	FcClock clock;
	if (fcSntpSyncDefault(&clock))
		FC_LOG("clock synced via %s (%+lds)", clock.server, (long)clock.offsetSec);
	else
		FC_LOG("clock sync failed: %s", clock.error);

	s_timeSyncRunning = false;
}

static void retryTimeSyncIfNeeded(FcApp *app)
{
	static time_t lastAttempt;

	if (!app->networkUp || s_timeSyncRunning)
		return;
	if (fcClockState()->synced)
		return;

	const time_t now = time(NULL);
	if (lastAttempt != 0 && now - lastAttempt < FC_RETRY_SECONDS)
		return;

	lastAttempt = now;
	if (fcWorkerSubmit(timeSyncJob, NULL))
		s_timeSyncRunning = true;
}

static void runApp(void)
{
	FcApp *app = &s_app;

	app->scenes[FC_TAB_BROWSE]    = &fcSceneBrowse;
	app->scenes[FC_TAB_INSTALLED] = &fcSceneInstalled;
	app->scenes[FC_TAB_SCAN]      = &fcSceneScan;
	app->scenes[FC_TAB_SETTINGS]  = &fcSceneSettings;
	app->tab = FC_TAB_BROWSE;

	if (app->scenes[app->tab]->enter)
		app->scenes[app->tab]->enter(app);

	u64 lastTick = osGetTime();

	while (aptMainLoop()) {
		hidScanInput();
		const u32 down = hidKeysDown();

		if (down & KEY_START)
			break;

		if (down & (KEY_L | KEY_R)) {
			const int dir = (down & KEY_R) ? 1 : -1;
			int next = (int)app->tab;
			for (int i = 0; i < FC_TAB_COUNT; i++) {
				next = (next + dir + FC_TAB_COUNT) % FC_TAB_COUNT;
				if (app->scenes[next])
					break;
			}
			fcAppSwitchTab(app, (FcTab)next);
		}

		if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A | KEY_B |
		            KEY_L | KEY_R))
			app->buttonMode = true;

		const u64 nowTick = osGetTime();
		float dt = (float)(nowTick - lastTick) / 1000.0f;
		lastTick = nowTick;
		if (dt > 0.25f)
			dt = 0.25f;
		app->timeSec += dt;
		app->frameDt  = dt;

		retryTimeSyncIfNeeded(app);

		fcTouchUpdate(&app->touch);
		if (app->touch.pressed)
			app->buttonMode = false;
		fcStereoUpdate(&app->stereo);

		const FcSceneVTable *scene = app->scenes[app->tab];
		if (scene && scene->update)
			scene->update(app, dt);

		C2D_TextBufClear(app->textBuf);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		FcDraw d = { .theme = &app->theme, .textBuf = app->textBuf,
		               .screenW = FC_TOP_W, .screenH = FC_TOP_H };

		C2D_TargetClear(app->stereo.topLeft, app->theme.pal.ground);
		C2D_SceneBegin(app->stereo.topLeft);
		d.eye = fcEyeFor(&app->stereo, GFX_LEFT);
		if (scene && scene->drawTop)
			scene->drawTop(app, &d);

		if (app->stereo.stereo) {
			C2D_TargetClear(app->stereo.topRight, app->theme.pal.ground);
			C2D_SceneBegin(app->stereo.topRight);
			d.eye = fcEyeFor(&app->stereo, GFX_RIGHT);
			if (scene && scene->drawTop)
				scene->drawTop(app, &d);
		}

		C2D_TargetClear(app->stereo.bottom, app->theme.pal.ground);
		C2D_SceneBegin(app->stereo.bottom);
		d.eye     = fcEyeFlat();
		d.screenW = FC_BOT_W;
		d.screenH = FC_BOT_H;
		if (scene && scene->drawBottom)
			scene->drawBottom(app, &d);

		fcDrawTabBar(app, &d);

		C3D_FrameEnd(0);
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	romfsInit();
	cfguInit();
	gfxInitDefault();
	gspLcdInit();

	consoleDebugInit(debugDevice_SVC);
	FC_LOG("Font Changer starting");

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	memset(&s_app, 0, sizeof s_app);
	fcStereoInit(&s_app.stereo);
	s_app.textBuf = C2D_TextBufNew(TEXT_BUF_GLYPHS);
	fcThemeInit(&s_app.theme, true);
	fcTouchInit(&s_app.touch);

	fcConfigLoad(&s_config);

	s_app.config    = &s_config;
	s_app.utcOffset = s_config.utcOffsetMinutes * 60;
	fcFontFeedSetSizeAdjust(s_config.fontSizeAdjust);
	s_app.use24Hour = s_config.use24Hour;
	fcThemeSetDark(&s_app.theme, s_config.darkTheme);
	s_app.stereo.swapEyes = s_config.swapEyes;

		osSetSpeedupEnable(true);
		aptHook(&s_aptCookie, fcAptHook, &s_app);

		fcFontFeedInit();

		if (!fcFontInstallInit())
			FC_LOG("no AM access; installing is unavailable");

		s_app.networkUp = fcHttpInit();
		if (s_app.networkUp) {
			fcCacheInit();
			fcWorkerStart();

			if (fcWorkerSubmit(timeSyncJob, NULL))
				s_timeSyncRunning = true;
		} else {
			FC_LOG("network init failed; running offline");
		}

		runApp();

		aptUnhook(&s_aptCookie);

		if (s_app.networkUp) {
			fcWorkerStop();
			fcHttpExit();
		}

		fcFontInstallExit();

	fcThemeExit(&s_app.theme);
	C2D_TextBufDelete(s_app.textBuf);
	fcStereoExit(&s_app.stereo);

	C2D_Fini();
	C3D_Fini();
	gspLcdExit();
	gfxExit();
	cfguExit();
	romfsExit();
	return 0;
}
