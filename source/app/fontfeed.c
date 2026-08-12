#include "app/fontfeed.h"

#include "app/fontinstall.h"
#include "app/log.h"
#include "app/sysfont.h"
#include "app/ttf.h"
#include "data/bcfntedit.h"
#include "data/ciabuild.h"
#include "data/fontslot.h"
#include "data/gfonts.h"
#include "data/lz11.h"
#include "data/sha256.h"
#include "net/cache.h"
#include "net/http.h"
#include "net/worker.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static FcCatalog   s_catalog;
static FcJobStatus s_status;
static FcFontEntry s_entry;
static char        s_localPath[256];
static int         s_localSlot;

static uint8_t     *s_preview;
static size_t       s_previewLen;
static FcBcfntInfo  s_previewInfo;
static FcMergeReport s_merge;
static int s_sizeAdjust;

static uint8_t *s_ttf;
static size_t   s_ttfLen;
static char     s_ttfId[40];

void fcFontFeedInit(void)
{
	memset(&s_catalog, 0, sizeof s_catalog);
	memset(&s_status, 0, sizeof s_status);
	snprintf(s_catalog.name, sizeof s_catalog.name, "Fonts");

	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/fc3ds", 0777);
	mkdir(FC_FONT_DIR, 0777);
}

void fcFontFeedSetSizeAdjust(int pixels)
{
	if (pixels < -4)
		pixels = -4;
	if (pixels > 4)
		pixels = 4;
	s_sizeAdjust = pixels;
}

int fcFontFeedSizeAdjust(void)
{
	return s_sizeAdjust;
}

const FcCatalog *fcFontFeedCatalog(void)
{
	return &s_catalog;
}

void fcFontFeedMergeReport(FcMergeReport *out)
{
	if (out)
		*out = s_merge;
}

void fcFontFeedStatus(FcJobStatus *out)
{
	if (out)
		*out = s_status;
}

bool fcFontFeedBusy(void)
{
	return s_status.state == FC_JOB_RUNNING;
}

void fcFontFeedAcknowledge(void)
{
	if (s_status.state == FC_JOB_DONE || s_status.state == FC_JOB_FAILED) {
		s_status.state = FC_JOB_IDLE;
		s_status.kind  = FC_JOB_NONE;
	}
}

bool fcFontFeedPreviewReady(void)
{
	return s_preview != NULL;
}

void *fcFontFeedTakePreview(size_t *len, FcBcfntInfo *info)
{
	if (!s_preview)
		return NULL;

	void *data = s_preview;
	if (len)
		*len = s_previewLen;
	if (info)
		*info = s_previewInfo;

	s_preview    = NULL;
	s_previewLen = 0;
	return data;
}

static void finish(FcJobState state, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void finish(FcJobState state, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_status.message, sizeof s_status.message, fmt, ap);
	va_end(ap);

	FC_LOG("job %s: %s", state == FC_JOB_DONE ? "done" : "failed",
	       s_status.message);

	s_status.state = state;
}

static bool begin(FcJobKind kind, FcJobFn fn, const char *message)
{
	if (s_status.state == FC_JOB_RUNNING || fcWorkerBusy())
		return false;

	s_status.kind    = kind;
	s_status.percent = -1;
	snprintf(s_status.message, sizeof s_status.message, "%s", message);
	s_status.state   = FC_JOB_RUNNING;

	if (!fcWorkerSubmit(fn, NULL)) {
		s_status.state   = FC_JOB_IDLE;
		s_status.kind    = FC_JOB_NONE;
		s_status.message[0] = '\0';
		return false;
	}

	return true;
}

static bool fail_(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return false;
}

typedef struct {
	FcHttpBuf *buf;
	size_t      expected;
} ProgressSink;

static size_t progressSink(const void *data, size_t len, void *user)
{
	ProgressSink *ps = user;

	const size_t taken = fcHttpBufSink(data, len, ps->buf);

	if (ps->expected > 0) {
		size_t pct = ps->buf->len * 100 / ps->expected;
		if (pct > 99)
			pct = 99;
		s_status.percent = (int)pct;
	}

	return taken;
}

static bool download(const FcDownload *dl, FcHttpBuf *buf,
                     char *err, size_t errSize)
{
	fcHttpBufInit(buf, FC_DOWNLOAD_MAX_BYTES);

	ProgressSink ps = { .buf = buf, .expected = dl->size };

	FcHttpRequest req;
	memset(&req, 0, sizeof req);
	req.url  = dl->url;
	req.sink = progressSink;
	req.user = &ps;

	FcHttpResponse res;
	memset(&res, 0, sizeof res);

	if (!fcHttpFetch(&req, &res) || !res.ok) {
		snprintf(err, errSize, "%.100s",
		         res.error[0] ? res.error : "download failed");
		fcHttpBufFree(buf);
		return false;
	}

	if (buf->len == 0) {
		snprintf(err, errSize, "The server sent an empty file.");
		fcHttpBufFree(buf);
		return false;
	}

	if (dl->size > 0 && buf->len != dl->size) {
		snprintf(err, errSize, "Expected %u bytes, got %u.",
		         (unsigned)dl->size, (unsigned)buf->len);
		fcHttpBufFree(buf);
		return false;
	}

	if (dl->sha256[0]) {
		uint8_t digest[FC_SHA256_SIZE];
		fcSha256(buf->data, buf->len, digest);

		if (!fcSha256MatchesHex(digest, dl->sha256)) {
			snprintf(err, errSize, "Checksum does not match the catalog.");
			fcHttpBufFree(buf);
			return false;
		}
	}

	return true;
}

static char s_catalogUrl[256];

static size_t catalogSink(const void *data, size_t len, void *user)
{
	return fcHttpBufSink(data, len, user);
}

static void catalogJob(void *user)
{
	(void)user;

	FcHttpBuf buf;
	fcHttpBufInit(&buf, 512 * 1024);

	FcCacheResult res;
	memset(&res, 0, sizeof res);

	if (!fcCacheFetch(s_catalogUrl, catalogSink, &buf, &res)) {
		fcHttpBufFree(&buf);
		finish(FC_JOB_FAILED, "%.140s",
		       res.error[0] ? res.error : "Could not reach the catalog.");
		return;
	}

	FcCatalog parsed;
	const bool ok = fcCatalogParse(buf.data, buf.len, &parsed);
	fcHttpBufFree(&buf);

	if (!ok) {
		finish(FC_JOB_FAILED, "%.140s", parsed.error);
		return;
	}

	s_catalog = parsed;

	finish(FC_JOB_DONE, "%d font%s%s", parsed.count,
	       parsed.count == 1 ? "" : "s",
	       res.status == FC_CACHE_STALE ? " (offline copy)" : "");
}

bool fcFontFeedRefresh(const char *url)
{
	if (!url || !url[0])
		return false;

	snprintf(s_catalogUrl, sizeof s_catalogUrl, "%s", url);
	return begin(FC_JOB_CATALOG, catalogJob, "Fetching the catalog...");
}

static bool downloadGoogleTtf(const char *family, FcHttpBuf *buf,
                              char *err, size_t errSize)
{
	char cssUrl[512];
	if (!fcGFontsCssUrl(family, 400, cssUrl, sizeof cssUrl))
		return fail_(err, errSize, "That family name is too long to request.");

	FcHttpBuf css;
	fcHttpBufInit(&css, 256 * 1024);

	FcHttpRequest req;
	memset(&req, 0, sizeof req);
	req.url       = cssUrl;
	req.sink      = fcHttpBufSink;
	req.user      = &css;
	req.userAgent = FC_GFONTS_UA;

	FcHttpResponse res;
	memset(&res, 0, sizeof res);

	if (!fcHttpFetch(&req, &res) || !res.ok) {
		snprintf(err, errSize, "%.110s",
		         res.error[0] ? res.error : "could not reach Google Fonts");
		fcHttpBufFree(&css);
		return false;
	}

	char ttfUrl[512];
	const bool found = fcGFontsTtfUrlFromCss(css.data, ttfUrl, sizeof ttfUrl);
	fcHttpBufFree(&css);

	if (!found) {
		snprintf(err, errSize,
		         "Google offered no TrueType for that family.");
		return false;
	}

	FC_LOG("google font %s -> %s", family, ttfUrl);

	FcDownload dl;
	memset(&dl, 0, sizeof dl);

	const size_t urlLen = strlen(ttfUrl);
	if (urlLen >= sizeof dl.url)
		return fail_(err, errSize, "Google returned an unusably long address.");

	memcpy(dl.url, ttfUrl, urlLen);
	dl.url[urlLen] = '\0';

	return download(&dl, buf, err, errSize);
}

static bool entryUsesFace(const FcFontEntry *e)
{
	return e->googleFamily[0] != '\0' || e->ttf.url[0] != '\0';
}

static bool obtainFace(FcHttpBuf *buf, char *err, size_t errSize)
{
	if (s_entry.googleFamily[0])
		return downloadGoogleTtf(s_entry.googleFamily, buf, err, errSize);

	return download(&s_entry.ttf, buf, err, errSize);
}

static bool mergeIntoSystemFont(const FcHttpBuf *ttf, uint8_t **out,
                                size_t *outLen, uint32_t *outBase,
                                FcMergeReport *report,
                                char *err, size_t errSize)
{
	size_t fontLen = 0;
	uint32_t base = 0;
	uint8_t *font = fcSysFontClone(&fontLen, &base, err, errSize);
	if (!font)
		return false;

	FcBcfntEdit edit;
	if (!fcBcfntEditOpen(font, fontLen, base, &edit, err, errSize)) {
		free(font);
		return false;
	}

	int targetCap = 0;
	const int refIdx = fcBcfntEditFindGlyph(font, fontLen, &edit, 'H');
	int inkTop = 0;
	if (refIdx >= 0 &&
	    fcBcfntEditGlyphInk(font, fontLen, &edit, refIdx, &inkTop, NULL,
	                          NULL, NULL))
		targetCap = edit.baseline - inkTop;

	if (targetCap > 0) {
		targetCap += s_sizeAdjust;
		if (targetCap < 4)
			targetCap = 4;
		if (targetCap > edit.baseline)
			targetCap = edit.baseline;
	}

	FcTtfResult res;
	if (!fcTtfRasteriseForCell(ttf->data, ttf->len, edit.cellWidth,
	                             edit.cellHeight, edit.baseline, targetCap,
	                             FC_COVER_LATIN_EXT, &res)) {
		free(font);
		return fail_(err, errSize, res.error);
	}

	FC_LOG("stock cap height %d px; rasterised at %d px", targetCap,
	       res.pixelSize);

	int replaced = 0, missing = 0;
	for (int i = 0; i < res.count; i++) {
		const int idx = fcBcfntEditFindGlyph(font, fontLen, &edit,
		                                       res.glyphs[i].codepoint);
		if (idx < 0) {
			missing++;
			continue;
		}
		if (fcBcfntEditReplaceGlyph(font, fontLen, &edit, idx, &res.glyphs[i]))
			replaced++;
	}

	if (report) {
		snprintf(report->family, sizeof report->family, "%s", res.family);
		report->requested = res.requested;
		report->provided  = res.count + res.tooBig;
		report->replaced  = replaced;
		report->tooBig    = res.tooBig;
		report->unmapped  = missing;
		report->pixelSize = res.pixelSize;
	}

	fcTtfFreeGlyphs(&res);

	FC_LOG("merged %d glyphs into the system font (%d too big, %d unmapped)",
	       replaced, res.tooBig, missing);

	if (replaced == 0) {
		free(font);
		return fail_(err, errSize,
		             "None of that face's glyphs matched the system font.");
	}

	*out = font;
	*outLen = fontLen;
	if (outBase)
		*outBase = base;
	return true;
}

static void previewJob(void *user)
{
	(void)user;

	free(s_preview);
	s_preview    = NULL;
	s_previewLen = 0;

	FcHttpBuf buf;
	char err[128];

	if (entryUsesFace(&s_entry)) {
		const bool cached = s_ttf && strcmp(s_ttfId, s_entry.id) == 0;

		if (cached) {
			buf.data  = (char *)s_ttf;
			buf.len   = s_ttfLen;
			buf.cap   = s_ttfLen;
			buf.limit = 0;
		} else if (!obtainFace(&buf, err, sizeof err)) {
			finish(FC_JOB_FAILED, "%.140s", err);
			return;
		}

		snprintf(s_status.message, sizeof s_status.message, "Merging...");
		s_status.percent = -1;

		uint8_t *plain = NULL;
		size_t plainLen = 0;
		uint32_t plainBase = 0;
		memset(&s_merge, 0, sizeof s_merge);
		const bool ok = mergeIntoSystemFont(&buf, &plain, &plainLen, &plainBase,
		                                    &s_merge, err, sizeof err);

		if (!cached) {
			free(s_ttf);
			s_ttf    = (uint8_t *)buf.data;
			s_ttfLen = buf.len;
			snprintf(s_ttfId, sizeof s_ttfId, "%s", s_entry.id);
		}

		if (!ok) {
			finish(FC_JOB_FAILED, "%.140s", err);
			return;
		}

		if (!fcBcfntEditRebase(plain, plainLen, plainBase, 0)) {
			free(plain);
			finish(FC_JOB_FAILED, "Could not prepare the font for preview.");
			return;
		}

		if (!fcBcfntInspect(plain, plainLen, &s_previewInfo)) {
			free(plain);
			finish(FC_JOB_FAILED, "The converted font did not come out right.");
			return;
		}

		s_preview    = plain;
		s_previewLen = plainLen;

		finish(FC_JOB_DONE, "%d of %d characters replaced",
		       s_merge.replaced, s_merge.requested);
		return;
	}

	if (!download(&s_entry.preview, &buf, err, sizeof err)) {
		finish(FC_JOB_FAILED, "%.140s", err);
		return;
	}

	uint8_t *raw = (uint8_t *)buf.data;
	size_t   rawLen = buf.len;
	uint8_t *owned = NULL;

	if (fcLz11IsCompressed(raw, rawLen)) {
		size_t plain = 0;
		if (!fcLz11DecompressedSize(raw, rawLen, &plain) ||
		    plain == 0 || plain > FC_DOWNLOAD_MAX_BYTES) {
			fcHttpBufFree(&buf);
			finish(FC_JOB_FAILED, "Compressed font declares a bad size.");
			return;
		}

		owned = malloc(plain);
		if (!owned) {
			fcHttpBufFree(&buf);
			finish(FC_JOB_FAILED, "Not enough memory for the preview.");
			return;
		}

		size_t got = 0;
		if (!fcLz11Decompress(raw, rawLen, owned, plain, &got)) {
			free(owned);
			fcHttpBufFree(&buf);
			finish(FC_JOB_FAILED, "Could not decompress the font.");
			return;
		}

		fcHttpBufFree(&buf);
		rawLen = got;
	} else {
		owned = raw;
		buf.data = NULL;
		fcHttpBufFree(&buf);
	}

	if (!fcBcfntInspect(owned, rawLen, &s_previewInfo)) {
		free(owned);
		finish(FC_JOB_FAILED, "That file is not a 3DS font.");
		return;
	}

	s_preview    = owned;
	s_previewLen = rawLen;

	char desc[64];
	fcBcfntDescribe(&s_previewInfo, desc, sizeof desc);
	finish(FC_JOB_DONE, "%s", desc);
}

bool fcFontFeedFetchPreview(const FcFontEntry *entry)
{
	if (!entry || !fcFontEntryPreviewable(entry))
		return false;

	s_entry = *entry;
	return begin(FC_JOB_PREVIEW, previewJob, "Fetching the preview...");
}

bool fcFontFeedRepreview(void)
{
	if (!entryUsesFace(&s_entry) || !s_ttf)
		return false;

	return begin(FC_JOB_PREVIEW, previewJob, "Resizing...");
}

static void keepCopy(const FcFontEntry *entry, const void *data, size_t len)
{
	char path[320];
	snprintf(path, sizeof path, "%s/%s.cia", FC_FONT_DIR, entry->id);

	for (char *p = path + strlen(FC_FONT_DIR) + 1; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' ||
		    *p == '"' || *p == '<' || *p == '>' || *p == '|')
			*p = '_';
	}

	FILE *f = fopen(path, "wb");
	if (!f) {
		FC_LOG("could not keep a copy at %s", path);
		return;
	}

	const bool ok = fwrite(data, 1, len, f) == len;
	fclose(f);

	if (!ok) {
		remove(path);
		FC_LOG("short write keeping %s", path);
	}
}

static void installGoogleJob(void)
{
	FcHttpBuf ttf;
	char err[160];

	if (!obtainFace(&ttf, err, sizeof err)) {
		finish(FC_JOB_FAILED, "%.140s", err);
		return;
	}

	snprintf(s_status.message, sizeof s_status.message, "Merging...");
	s_status.percent = -1;

	uint8_t *merged = NULL;
	size_t mergedLen = 0;
	memset(&s_merge, 0, sizeof s_merge);
	const bool ok = mergeIntoSystemFont(&ttf, &merged, &mergedLen, NULL,
	                                    &s_merge, err, sizeof err);
	fcHttpBufFree(&ttf);

	if (!ok) {
		finish(FC_JOB_FAILED, "%.140s", err);
		return;
	}

	snprintf(s_status.message, sizeof s_status.message, "Compressing...");

	const size_t bound = fcLz11CompressBound(mergedLen);
	uint8_t *packed = malloc(bound);
	size_t packedLen = 0;

	if (!packed || !fcLz11Compress(merged, mergedLen, packed, bound, &packedLen)) {
		free(packed);
		free(merged);
		finish(FC_JOB_FAILED, "Could not compress the merged font.");
		return;
	}
	free(merged);

	if (packedLen > FC_FONT_MAX_PACKED) {
		free(packed);
		finish(FC_JOB_FAILED, "Converted font is %u KB; the limit is %u KB.",
		       (unsigned)(packedLen / 1024),
		       (unsigned)(FC_FONT_MAX_PACKED / 1024));
		return;
	}

	snprintf(s_status.message, sizeof s_status.message, "Building the title...");

	const FcFontSlotInfo *slot = fcFontSlotInfo(s_entry.slot);
	if (!slot) {
		free(packed);
		finish(FC_JOB_FAILED, "No such font slot.");
		return;
	}

	FcCiaTemplates templates;
	if (!fcCiaTemplatesLoad(FC_CIA_TEMPLATE_DIR, &templates, err, sizeof err)) {
		free(packed);
		finish(FC_JOB_FAILED, "%.140s", err);
		return;
	}

	uint8_t *cia = NULL;
	size_t ciaLen = 0;
	const bool built = fcCiaBuildFont(slot->titleId, packed, packedLen,
	                                    slot->fileName, &templates,
	                                    &cia, &ciaLen, err, sizeof err);
	fcCiaTemplatesFree(&templates);
	free(packed);

	if (!built) {
		finish(FC_JOB_FAILED, "%.140s", err);
		return;
	}

	keepCopy(&s_entry, cia, ciaLen);

	s_status.percent = 0;
	snprintf(s_status.message, sizeof s_status.message, "Installing...");

	char installErr[160];
	const FcInstallResult r =
		fcFontInstallFromMemory(cia, ciaLen, s_entry.slot, &s_status.percent,
		                          installErr, sizeof installErr);
	free(cia);

	if (r != FC_INSTALL_OK) {
		finish(FC_JOB_FAILED, "%.140s", installErr);
		return;
	}

	finish(FC_JOB_DONE, "%d of %d characters replaced. Reboot to see it.",
	       s_merge.replaced, s_merge.requested);
}

static void installJob(void *user)
{
	(void)user;

	if (entryUsesFace(&s_entry)) {
		installGoogleJob();
		return;
	}

	FcHttpBuf buf;
	char err[128];

	if (!download(&s_entry.cia, &buf, err, sizeof err)) {
		finish(FC_JOB_FAILED, "%.140s", err);
		return;
	}

	s_status.percent = 100;

	keepCopy(&s_entry, buf.data, buf.len);

	s_status.percent = 0;
	snprintf(s_status.message, sizeof s_status.message, "Installing...");

	char installErr[160];
	const FcInstallResult r =
		fcFontInstallFromMemory(buf.data, buf.len, s_entry.slot,
		                          &s_status.percent, installErr,
		                          sizeof installErr);

	fcHttpBufFree(&buf);

	if (r != FC_INSTALL_OK) {
		finish(FC_JOB_FAILED, "%.140s", installErr);
		return;
	}

	finish(FC_JOB_DONE, "%.60s installed. Reboot to see it everywhere.",
	       s_entry.name);
}

bool fcFontFeedInstall(const FcFontEntry *entry)
{
	if (!entry || !fcFontEntryInstallable(entry))
		return false;

	s_entry = *entry;
	return begin(FC_JOB_INSTALL, installJob, "Downloading...");
}

static void installLocalJob(void *user)
{
	(void)user;

	FILE *f = fopen(s_localPath, "rb");
	if (!f) {
		finish(FC_JOB_FAILED, "Could not open that file.");
		return;
	}

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > FC_DOWNLOAD_MAX_BYTES) {
		fclose(f);
		finish(FC_JOB_FAILED, "That file is not a plausible font CIA.");
		return;
	}

	uint8_t *data = malloc((size_t)size);
	if (!data) {
		fclose(f);
		finish(FC_JOB_FAILED, "Not enough memory to read it.");
		return;
	}

	const size_t got = fread(data, 1, (size_t)size, f);
	fclose(f);

	if (got != (size_t)size) {
		free(data);
		finish(FC_JOB_FAILED, "Could not read the whole file.");
		return;
	}

	char installErr[160];
	const FcInstallResult r =
		fcFontInstallFromMemory(data, got, s_localSlot, &s_status.percent,
		                          installErr, sizeof installErr);
	free(data);

	if (r != FC_INSTALL_OK) {
		finish(FC_JOB_FAILED, "%.140s", installErr);
		return;
	}

	finish(FC_JOB_DONE, "Installed. Reboot to see it everywhere.");
}

bool fcFontFeedInstallLocal(const char *path, int slot)
{
	if (!path || !path[0])
		return false;

	snprintf(s_localPath, sizeof s_localPath, "%s", path);
	s_localSlot = slot;

	return begin(FC_JOB_INSTALL, installLocalJob, "Reading from the card...");
}
