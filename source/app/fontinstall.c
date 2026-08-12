#include "app/fontinstall.h"

#include "app/log.h"
#include "data/cia.h"
#include "data/fontslot.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#define WRITE_CHUNK (64 * 1024)

static bool s_amReady;

bool fcFontInstallInit(void)
{
	if (s_amReady)
		return true;

	const Result r = amInit();
	if (R_FAILED(r)) {
		FC_LOG("amInit failed: %08lX", (unsigned long)r);
		return false;
	}

	s_amReady = true;
	return true;
}

void fcFontInstallExit(void)
{
	if (!s_amReady)
		return;

	amExit();
	s_amReady = false;
}

bool fcFontInstallReady(void)
{
	return s_amReady;
}

bool fcFontInstalledInfo(int slot, FcInstalledTitle *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof *out);

	const FcFontSlotInfo *info = fcFontSlotInfo(slot);
	if (!info || !s_amReady)
		return false;

	out->titleId = info->titleId;

	u64 titleId = info->titleId;
	AM_TitleInfo am;
	memset(&am, 0, sizeof am);

	if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_NAND, 1, &titleId, &am)))
		return false;

	out->present = true;
	out->size    = am.size;
	out->version = am.version;
	return true;
}

const char *fcFontInstallResultName(FcInstallResult result)
{
	switch (result) {
	case FC_INSTALL_OK:          return "installed";
	case FC_INSTALL_NOT_A_CIA:   return "not a CIA";
	case FC_INSTALL_WRONG_TITLE: return "not a system font";
	case FC_INSTALL_WRONG_SLOT:  return "wrong script";
	default:                     return "install refused";
	}
}

static const char *resultHint(Result r)
{
	const unsigned module = (unsigned)((r >> 10) & 0xFF);
	const unsigned desc   = (unsigned)(r & 0x3FF);

	if (module != 32 )
		return NULL;

	switch (desc) {
	case 1020: return "the title already exists";
	case 1018: return "the title is not installed";
	case 1011: return "the system is out of memory";
	case 1002: return "the system refused permission";
	case 1004: return "the CIA's size does not add up";
	default:   return NULL;
	}
}

static FcInstallResult failWith(FcInstallResult code, char *err, size_t errSize,
                                const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	FC_LOG("install refused: %s", msg);
	return code;
}

FcInstallResult fcFontInstallFromMemory(const void *data, size_t len,
                                          int expectSlot,
                                          volatile int *progress,
                                          char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';
	if (progress)
		*progress = 0;

	if (!s_amReady)
		return failWith(FC_INSTALL_AM_FAILED, err, errSize,
		                "No AM access. Install and run the CIA build.");

	FcCiaInfo cia;
	char parseErr[64];
	if (!fcCiaInspect(data, len, &cia, parseErr, sizeof parseErr))
		return failWith(FC_INSTALL_NOT_A_CIA, err, errSize, parseErr);

	char tid[24];
	fcCiaFormatTitleId(cia.titleId, tid, sizeof tid);

	const int slot = fcFontSlotForTitleId(cia.titleId);
	if (slot < 0) {
		char msg[96];
		snprintf(msg, sizeof msg, "%s is not a system font title", tid);
		return failWith(FC_INSTALL_WRONG_TITLE, err, errSize, msg);
	}

	if (expectSlot >= 0 && slot != expectSlot) {
		const FcFontSlotInfo *want = fcFontSlotInfo(expectSlot);
		const FcFontSlotInfo *got  = fcFontSlotInfo(slot);
		char msg[96];
		snprintf(msg, sizeof msg, "This is the %s font, not %s",
		         got ? got->label : "?", want ? want->label : "?");
		return failWith(FC_INSTALL_WRONG_SLOT, err, errSize, msg);
	}

	FC_LOG("installing %s (%u bytes) into slot %d", tid, (unsigned)len, slot);

	FcInstalledTitle existing;
	const bool present = fcFontInstalledInfo(slot, &existing) && existing.present;

	Handle handle = 0;
	Result r = present ? AM_StartCiaInstallOverwrite(&handle, MEDIATYPE_NAND)
	                   : AM_StartCiaInstall(MEDIATYPE_NAND, &handle);
	if (R_FAILED(r)) {
		char msg[96];
		snprintf(msg, sizeof msg, "AM refused the install (%08lX)",
		         (unsigned long)r);
		return failWith(FC_INSTALL_AM_FAILED, err, errSize, msg);
	}

	const uint8_t *bytes = data;
	size_t off = 0;

	while (off < len) {
		u32 chunk = (u32)(len - off);
		if (chunk > WRITE_CHUNK)
			chunk = WRITE_CHUNK;

		u32 wrote = 0;
		r = FSFILE_Write(handle, &wrote, off, bytes + off, chunk, 0);
		if (R_FAILED(r) || wrote == 0) {
			AM_CancelCIAInstall(handle);

			const char *hint = resultHint(r);
			char msg[128];
			if (hint)
				snprintf(msg, sizeof msg, "Write failed at %u%%: %s (%08lX)",
				         (unsigned)(off * 100 / len), hint, (unsigned long)r);
			else
				snprintf(msg, sizeof msg, "Write failed at %u%% (%08lX)",
				         (unsigned)(off * 100 / len), (unsigned long)r);
			return failWith(FC_INSTALL_AM_FAILED, err, errSize, msg);
		}

		off += wrote;
		if (progress)
			*progress = (int)(off * 100 / len);
	}

	r = AM_FinishCiaInstall(handle);
	if (R_FAILED(r)) {
		char msg[96];
		snprintf(msg, sizeof msg, "Install did not commit (%08lX)",
		         (unsigned long)r);
		return failWith(FC_INSTALL_AM_FAILED, err, errSize, msg);
	}

	if (progress)
		*progress = 100;

	FC_LOG("installed %s", tid);
	return FC_INSTALL_OK;
}
