#include "app/app.h"

#include "data/timeutil.h"
#include "net/sntp.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

time_t fcAppLocalNow(const FcApp *app)
{
	return fcNow() + app->utcOffset;
}

void fcFormatTime(const FcApp *app, time_t local, char *dst, size_t size)
{
	FcDateTime d;
	fcCivilFromEpoch(local, &d);

	if (app->use24Hour) {
		snprintf(dst, size, "%02d:%02d", d.hour, d.minute);
		return;
	}

	int hour = d.hour % 12;
	if (hour == 0)
		hour = 12;
	snprintf(dst, size, "%d:%02d %s", hour, d.minute, d.hour < 12 ? "AM" : "PM");
}

void fcFormatDate(const FcApp *app, time_t local, char *dst, size_t size)
{
	(void)app;

	FcDateTime d;
	fcCivilFromEpoch(local, &d);

	const int wd = fcWeekday(d.year, d.month, d.day);
	snprintf(dst, size, "%s %d %s", fcWeekdayName(wd, true), d.day,
	         fcMonthName(d.month, true));
}

void fcAppSwitchTab(FcApp *app, FcTab tab)
{
	if ((unsigned)tab >= (unsigned)FC_TAB_COUNT)
		return;
	if (tab == app->tab || !app->scenes[tab])
		return;

	if (app->scenes[app->tab] && app->scenes[app->tab]->leave)
		app->scenes[app->tab]->leave(app);

	app->tab = tab;

	if (app->scenes[app->tab]->enter)
		app->scenes[app->tab]->enter(app);
}

bool fcDrawTabBar(FcApp *app, const FcDraw *d)
{
	const FcPalette *p = &app->theme.pal;
	const FcEye flat = fcEyeFlat();

	const float y = FC_BOT_H - FC_TABBAR_H;
	const float w = FC_BOT_W / FC_TAB_COUNT;

	C2D_DrawRectSolid(0, y, 0.0f, FC_BOT_W, FC_TABBAR_H, p->card);
	C2D_DrawRectSolid(0, y, 0.0f, FC_BOT_W, 1.0f, p->cardEdge);

	bool changed = false;

	for (int i = 0; i < FC_TAB_COUNT; i++) {
		const float x = i * w;
		const bool active = (app->tab == (FcTab)i);
		const bool held = app->touch.down &&
		                  fcTouchInRect(&app->touch, x, y, w, FC_TABBAR_H);

		if (active) {
			fcRoundedRect(x + 3.0f, y + 4.0f, w - 6.0f, FC_TABBAR_H - 8.0f,
			                FC_RADIUS, 0.0f, p->accentSoft, &flat);
			C2D_DrawRectSolid(x + w * 0.25f, y + FC_TABBAR_H - 4.0f, 0.0f,
			                  w * 0.5f, 2.0f, p->accent);
		} else if (held) {
			fcRoundedRect(x + 3.0f, y + 4.0f, w - 6.0f, FC_TABBAR_H - 8.0f,
			                FC_RADIUS, 0.0f, p->accentSoft, &flat);
		}

		const char *label = app->scenes[i] ? app->scenes[i]->tabLabel : "-";
		fcText(d, label, x + w / 2, y + 9.0f, 0.0f, 0.40f,
		         active ? p->accent : p->inkDim, FC_ALIGN_CENTER);

		if (fcTouchTapped(&app->touch, x, y, w, FC_TABBAR_H) &&
		    app->tab != (FcTab)i && app->scenes[i]) {
			if (app->scenes[app->tab] && app->scenes[app->tab]->leave)
				app->scenes[app->tab]->leave(app);

			app->tab = (FcTab)i;
			changed = true;

			if (app->scenes[app->tab]->enter)
				app->scenes[app->tab]->enter(app);
		}
	}

	return changed;
}
