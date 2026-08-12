#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_LZ11_WINDOW 4096

bool fcLz11IsCompressed(const uint8_t *data, size_t len);

bool fcLz11DecompressedSize(const uint8_t *data, size_t len, size_t *out);

bool fcLz11Decompress(const uint8_t *src, size_t srcLen,
                        uint8_t *dst, size_t dstCap, size_t *written);

size_t fcLz11CompressBound(size_t srcLen);

bool fcLz11Compress(const uint8_t *src, size_t srcLen,
                      uint8_t *dst, size_t dstCap, size_t *written);
