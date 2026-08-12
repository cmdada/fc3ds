#include "data/fontslot.h"

#include <string.h>

static const FcFontSlotInfo kSlots[FC_SLOT_COUNT] = {
	{ "std", "Standard", "JPN / EUR / USA", "cbf_std.bcfnt.lz",
	  0x0004009B00014002ULL },
	{ "chn", "Chinese",  "CHN",             "cbf_zh-Hans-CN.bcfnt.lz",
	  0x0004009B00014102ULL },
	{ "kor", "Korean",   "KOR",             "cbf_ko-Hang-KR.bcfnt.lz",
	  0x0004009B00014202ULL },
	{ "twn", "Taiwanese","TWN",             "cbf_zh-Hant-TW.bcfnt.lz",
	  0x0004009B00014302ULL },
};

const FcFontSlotInfo *fcFontSlotInfo(int slot)
{
	if (slot < 0 || slot >= FC_SLOT_COUNT)
		return NULL;
	return &kSlots[slot];
}

int fcFontSlotFromId(const char *id)
{
	if (!id)
		return -1;

	for (int i = 0; i < FC_SLOT_COUNT; i++) {
		if (strcmp(kSlots[i].id, id) == 0)
			return i;
	}
	return -1;
}

int fcFontSlotForTitleId(uint64_t titleId)
{
	for (int i = 0; i < FC_SLOT_COUNT; i++) {
		if (kSlots[i].titleId == titleId)
			return i;
	}
	return -1;
}

bool fcFontSlotIsFontTitle(uint64_t titleId)
{
	return fcFontSlotForTitleId(titleId) >= 0;
}
