#include "app/sysfont.h"

#include "app/log.h"
#include "data/bcfnt.h"

#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FINF_OFFSET 0x14

static uint8_t *fail(char *err, size_t errSize, const char *msg)
{
	if (err && errSize)
		snprintf(err, errSize, "%s", msg);
	return NULL;
}

uint8_t *fcSysFontClone(size_t *outLen, uint32_t *outBase,
                          char *err, size_t errSize)
{
	if (err && errSize)
		err[0] = '\0';

	if (R_FAILED(fontEnsureMapped()))
		return fail(err, errSize, "The system font is not mapped.");

	CFNT_s *cfnt = fontGetSystemFont();
	if (!cfnt)
		return fail(err, errSize, "No system font to read.");

	const size_t len = cfnt->fileSize;
	if (len < 0x40 || len > FC_SYSFONT_MAX)
		return fail(err, errSize, "The system font reports an odd size.");

	uint8_t *copy = malloc(len);
	if (!copy)
		return fail(err, errSize, "Not enough memory to copy the font.");

	memcpy(copy, cfnt, len);

	if (!fcBcfntLooksLikeFont(copy, len)) {
		free(copy);
		return fail(err, errSize, "The mapped font is not a BCFNT.");
	}

	if (outBase)
		*outBase = (uint32_t)(uintptr_t)cfnt;

	FC_LOG("cloned system font: %u bytes", (unsigned)len);

	if (outLen)
		*outLen = len;
	return copy;
}
