#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint64_t titleId;
	uint64_t ticketTitleId;
	uint16_t titleVersion;
	uint64_t contentSize;
	uint32_t certChainSize;
	uint32_t ticketSize;
	uint32_t tmdSize;
	uint32_t metaSize;
	size_t   expectedSize;
} FcCiaInfo;

bool fcCiaInspect(const uint8_t *data, size_t len, FcCiaInfo *out,
                    char *err, size_t errSize);

void fcCiaFormatTitleId(uint64_t titleId, char *dst, size_t size);
