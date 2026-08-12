#pragma once

#include <stdbool.h>

typedef void (*FcJobFn)(void *user);

bool fcWorkerStart(void);

void fcWorkerStop(void);

bool fcWorkerSubmit(FcJobFn fn, void *user);

bool fcWorkerBusy(void);

void fcWorkerPause(bool paused);
