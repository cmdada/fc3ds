#include "ui/theme.h"

#include <3ds.h>
#include <string.h>

static void fcTokensDark(FcPalette *p)
{
	p->base    = C2D_Color32(0x0D, 0x18, 0x1F, 0xFF);
	p->surface = C2D_Color32(0x1F, 0x1D, 0x2E, 0xFF);
	p->overlay = C2D_Color32(0x26, 0x23, 0x3A, 0xFF);
	p->muted   = C2D_Color32(0x86, 0x83, 0xA3, 0xFF);
	p->subtle  = C2D_Color32(0x90, 0x8C, 0xAA, 0xFF);
	p->text    = C2D_Color32(0xE0, 0xDE, 0xF4, 0xFF);
	p->hlLow   = C2D_Color32(0x21, 0x20, 0x2E, 0xFF);
	p->hlMed   = C2D_Color32(0x40, 0x3D, 0x52, 0xFF);
	p->hlHigh  = C2D_Color32(0x52, 0x4F, 0x67, 0xFF);
	p->love    = C2D_Color32(0xEB, 0x6F, 0x92, 0xFF);
	p->gold    = C2D_Color32(0xF6, 0xC1, 0x77, 0xFF);
	p->rose    = C2D_Color32(0xEB, 0xBC, 0xBA, 0xFF);
	p->pine    = C2D_Color32(0x3E, 0x8F, 0xB0, 0xFF);
	p->foam    = C2D_Color32(0x9C, 0xCF, 0xD8, 0xFF);
	p->iris    = C2D_Color32(0xC4, 0xA7, 0xE7, 0xFF);
}

static void fcTokensLight(FcPalette *p)
{
	p->base    = C2D_Color32(0xEB, 0xE0, 0xF2, 0xFF);
	p->surface = C2D_Color32(0xDB, 0xD1, 0xE2, 0xFF);
	p->overlay = C2D_Color32(0xD2, 0xC5, 0xDC, 0xFF);
	p->muted   = C2D_Color32(0x59, 0x4A, 0x63, 0xFF);
	p->subtle  = C2D_Color32(0x4D, 0x40, 0x56, 0xFF);
	p->text    = C2D_Color32(0x18, 0x0B, 0x21, 0xFF);
	p->hlLow   = C2D_Color32(0xD9, 0xD1, 0xDF, 0xFF);
	p->hlMed   = C2D_Color32(0xB9, 0xAD, 0xC2, 0xFF);
	p->hlHigh  = C2D_Color32(0xA6, 0x98, 0xB0, 0xFF);
	p->love    = C2D_Color32(0x94, 0x43, 0x5C, 0xFF);
	p->gold    = C2D_Color32(0x8A, 0x5A, 0x08, 0xFF);
	p->rose    = C2D_Color32(0x9D, 0x48, 0x44, 0xFF);
	p->pine    = C2D_Color32(0x28, 0x69, 0x83, 0xFF);
	p->foam    = C2D_Color32(0x35, 0x68, 0x71, 0xFF);
	p->iris    = C2D_Color32(0x6D, 0x54, 0x8A, 0xFF);
}

static void fcRolesFromTokens(FcPalette *p)
{
	p->ground   = p->base;
	p->card     = p->surface;
	p->cardEdge = p->hlMed;
	p->ink      = p->text;
	p->inkDim   = p->subtle;
	p->inkFaint = p->hlHigh;
	p->accent   = p->iris;
	p->warn     = p->gold;
	p->good     = p->foam;
	p->bad      = p->love;

	p->accentSoft = (p->iris & 0x00FFFFFF) | ((u32)0x30 << 24);
}

static void fcPaletteDark(FcPalette *p)
{
	fcTokensDark(p);
	fcRolesFromTokens(p);

	p->groundEdge = C2D_Color32(0x09, 0x11, 0x16, 0xFF);

	p->cardShadow = C2D_Color32(0x00, 0x00, 0x00, 0x38);
	p->highlight  = (p->text & 0x00FFFFFF) | ((u32)0x18 << 24);

	p->skyPale    = p->text;
	p->skyShade   = p->subtle;
}

static void fcPaletteLight(FcPalette *p)
{
	fcTokensLight(p);
	fcRolesFromTokens(p);

	p->groundEdge = C2D_Color32(0xDE, 0xCC, 0xEA, 0xFF);

	p->cardShadow = C2D_Color32(0x00, 0x00, 0x00, 0x1F);
	p->highlight  = C2D_Color32(0xFF, 0xFF, 0xFF, 0x90);

	p->skyPale    = p->base;
	p->skyShade   = p->hlLow;

	p->accentSoft = (p->iris & 0x00FFFFFF) | ((u32)0x28 << 24);
}

void fcPaletteInit(FcPalette *p, bool dark)
{
	if (dark)
		fcPaletteDark(p);
	else
		fcPaletteLight(p);
}

void fcThemeSetDark(FcTheme *t, bool dark)
{
	t->dark = dark;
	fcPaletteInit(&t->pal, dark);
}

bool fcThemeInit(FcTheme *t, bool dark)
{
	memset(t, 0, sizeof(*t));
	fcThemeSetDark(t, dark);

	u8 region = CFG_REGION_USA;
	if (R_FAILED(CFGU_SecureInfoGetRegion(&region)))
		region = CFG_REGION_USA;

	t->font = C2D_FontLoadSystem((CFG_Region)region);

	return t->font != NULL;
}

void fcThemeExit(FcTheme *t)
{
	if (t->font) {
		C2D_FontFree(t->font);
		t->font = NULL;
	}
}

static int fcHexNibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

u32 fcColorFromHex(const char *hex, u32 fallback)
{
	if (!hex)
		return fallback;
	if (*hex == '#')
		hex++;
	if (strlen(hex) < 6)
		return fallback;

	int v[6];
	for (int i = 0; i < 6; i++) {
		v[i] = fcHexNibble(hex[i]);
		if (v[i] < 0)
			return fallback;
	}

	return C2D_Color32((u8)(v[0] << 4 | v[1]),
	                   (u8)(v[2] << 4 | v[3]),
	                   (u8)(v[4] << 4 | v[5]),
	                   0xFF);
}
