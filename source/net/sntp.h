#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
	bool    synced;
	int64_t offsetSec;
	int32_t roundTripMs;
	char    server[64];
	char    error[96];
} FcClock;

bool fcSntpSync(const char *host, FcClock *clock);

bool fcSntpSyncDefault(FcClock *clock);

time_t fcNow(void);

const FcClock *fcClockState(void);

bool fcClockPlausible(void);
