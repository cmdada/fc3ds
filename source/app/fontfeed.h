#pragma once

#include "data/bcfnt.h"
#include "data/catalog.h"

#include <stdbool.h>
#include <stddef.h>

#define FC_DOWNLOAD_MAX_BYTES (8 * 1024 * 1024)

#define FC_FONT_DIR "sdmc:/3ds/fc3ds/fonts"

#define FC_CIA_TEMPLATE_DIR "romfs:/cia"

#define FC_TTF_PIXEL_SIZE 26

#define FC_FONT_MAX_PACKED (1536 * 1024)

typedef struct {
	char family[64];
	int  requested;
	int  provided;
	int  replaced;
	int  tooBig;
	int  unmapped;
	int  pixelSize;
} FcMergeReport;

void fcFontFeedMergeReport(FcMergeReport *out);

typedef enum {
	FC_JOB_NONE,
	FC_JOB_CATALOG,
	FC_JOB_PREVIEW,
	FC_JOB_INSTALL,
} FcJobKind;

typedef enum {
	FC_JOB_IDLE,
	FC_JOB_RUNNING,
	FC_JOB_DONE,
	FC_JOB_FAILED,
} FcJobState;

typedef struct {
	FcJobKind  kind;
	FcJobState state;
	int        percent;
	char       message[160];
} FcJobStatus;

void fcFontFeedInit(void);

void fcFontFeedSetSizeAdjust(int pixels);
int  fcFontFeedSizeAdjust(void);

bool fcFontFeedRepreview(void);

const FcCatalog *fcFontFeedCatalog(void);

void fcFontFeedStatus(FcJobStatus *out);

bool fcFontFeedBusy(void);

void fcFontFeedAcknowledge(void);

bool fcFontFeedRefresh(const char *url);

bool fcFontFeedFetchPreview(const FcFontEntry *entry);

bool fcFontFeedInstall(const FcFontEntry *entry);

bool fcFontFeedInstallLocal(const char *path, int slot);

void *fcFontFeedTakePreview(size_t *len, FcBcfntInfo *info);

bool fcFontFeedPreviewReady(void);
