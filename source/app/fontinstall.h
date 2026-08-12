#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	FC_INSTALL_OK,
	FC_INSTALL_NOT_A_CIA,
	FC_INSTALL_WRONG_TITLE,
	FC_INSTALL_WRONG_SLOT,
	FC_INSTALL_AM_FAILED,
} FcInstallResult;

typedef struct {
	bool     present;
	uint64_t titleId;
	uint64_t size;
	uint16_t version;
} FcInstalledTitle;

bool fcFontInstallInit(void);
void fcFontInstallExit(void);

bool fcFontInstallReady(void);

bool fcFontInstalledInfo(int slot, FcInstalledTitle *out);

FcInstallResult fcFontInstallFromMemory(const void *data, size_t len,
                                          int expectSlot,
                                          volatile int *progress,
                                          char *err, size_t errSize);

const char *fcFontInstallResultName(FcInstallResult result);
