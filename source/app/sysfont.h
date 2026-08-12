#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_SYSFONT_MAX (4 * 1024 * 1024)

uint8_t *fcSysFontClone(size_t *outLen, uint32_t *outBase,
                          char *err, size_t errSize);
