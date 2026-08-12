#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	const uint8_t *certs;
	size_t         certsLen;
	const uint8_t *ticket;
	size_t         ticketLen;
	const uint8_t *tmd;
	size_t         tmdLen;
} FcCiaTemplates;

bool fcCiaTemplatesLoad(const char *dir, FcCiaTemplates *out,
                          char *err, size_t errSize);
void fcCiaTemplatesFree(FcCiaTemplates *t);

bool fcCiaBuildFont(uint64_t titleId, const void *font, size_t fontLen,
                      const char *fileName, const FcCiaTemplates *templates,
                      uint8_t **out, size_t *outLen, char *err, size_t errSize);
