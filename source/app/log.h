#pragma once

#include <stdio.h>

#define FC_LOG(fmt, ...) \
	fprintf(stderr, "[fc] " fmt "\n", ##__VA_ARGS__)
