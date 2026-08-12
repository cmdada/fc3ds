#pragma once

#include <stdbool.h>
#include <stddef.h>

bool fcTextInputAsk(const char *hint, const char *initial,
                      char *dst, size_t dstSize);
