#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_IVFC_BLOCK 0x1000

bool fcRomfsBuildSingle(const char *name, const void *data, size_t len,
                          uint8_t **out, size_t *outLen,
                          char *err, size_t errSize);

size_t fcRomfsHashRegionSize(void);
