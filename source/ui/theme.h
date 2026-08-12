#pragma once

#include <citro2d.h>
#include <stdbool.h>

#define FC_TOP_W    400.0f
#define FC_TOP_H    240.0f
#define FC_BOT_W    320.0f
#define FC_BOT_H    240.0f

#define FC_GRID     8.0f
#define FC_RADIUS   6.0f

typedef struct {
	u32 base;
	u32 surface;
	u32 overlay;
	u32 muted;
	u32 subtle;
	u32 text;
	u32 hlLow;
	u32 hlMed;
	u32 hlHigh;
	u32 love;
	u32 gold;
	u32 rose;
	u32 pine;
	u32 foam;
	u32 iris;

	u32 ground;
	u32 groundEdge;
	u32 card;
	u32 cardEdge;
	u32 cardShadow;
	u32 highlight;
	u32 ink;
	u32 inkDim;
	u32 inkFaint;
	u32 accent;
	u32 accentSoft;
	u32 warn;
	u32 good;
	u32 bad;
	u32 skyPale;
	u32 skyShade;
} FcPalette;

typedef struct {
	FcPalette pal;
	C2D_Font font;
	bool dark;
} FcTheme;

bool fcThemeInit(FcTheme *t, bool dark);
void fcThemeExit(FcTheme *t);

void fcThemeSetDark(FcTheme *t, bool dark);

void fcPaletteInit(FcPalette *p, bool dark);

u32 fcColorFromHex(const char *hex, u32 fallback);
