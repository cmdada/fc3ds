#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	FC_SLOT_STD,
	FC_SLOT_CHN,
	FC_SLOT_KOR,
	FC_SLOT_TWN,
	FC_SLOT_COUNT,
} FcFontSlot;

typedef struct {
	const char *id;
	const char *label;
	const char *regions;
	const char *fileName;
	uint64_t    titleId;
} FcFontSlotInfo;

const FcFontSlotInfo *fcFontSlotInfo(int slot);

int fcFontSlotFromId(const char *id);

int fcFontSlotForTitleId(uint64_t titleId);

bool fcFontSlotIsFontTitle(uint64_t titleId);
