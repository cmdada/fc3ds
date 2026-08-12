#include "net/worker.h"

#include <3ds.h>
#include <string.h>

#define WORKER_STACK_SIZE (256 * 1024)

#define WORKER_CORE 2

static Thread      s_thread;
static LightEvent  s_wake;
static LightLock   s_lock;
static FcJobFn   s_job;
static void       *s_jobUser;
static volatile bool s_running;
static volatile bool s_busy;
static volatile bool s_paused;

static void workerMain(void *arg)
{
	(void)arg;

	while (true) {
		LightEvent_Wait(&s_wake);

		if (!s_running)
			break;

		LightLock_Lock(&s_lock);
		FcJobFn fn   = s_job;
		void     *user = s_jobUser;
		s_job          = NULL;
		s_jobUser      = NULL;
		LightLock_Unlock(&s_lock);

		if (fn)
			fn(user);

		s_busy = false;
	}
}

bool fcWorkerStart(void)
{
	if (s_running)
		return true;

	LightEvent_Init(&s_wake, RESET_ONESHOT);
	LightLock_Init(&s_lock);
	s_job     = NULL;
	s_jobUser = NULL;
	s_busy    = false;
	s_running = true;

	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

	s_thread = threadCreate(workerMain, NULL, WORKER_STACK_SIZE,
	                        prio + 1, WORKER_CORE, false);
	if (!s_thread) {
		s_running = false;
		return false;
	}

	return true;
}

void fcWorkerStop(void)
{
	if (!s_running)
		return;

	s_running = false;
	LightEvent_Signal(&s_wake);

	if (s_thread) {
		threadJoin(s_thread, U64_MAX);
		threadFree(s_thread);
		s_thread = NULL;
	}
	s_busy = false;
}

bool fcWorkerSubmit(FcJobFn fn, void *user)
{
	if (!s_running || s_busy || s_paused || !fn)
		return false;

	LightLock_Lock(&s_lock);
	s_job     = fn;
	s_jobUser = user;
	s_busy    = true;
	LightLock_Unlock(&s_lock);

	LightEvent_Signal(&s_wake);
	return true;
}

bool fcWorkerBusy(void)
{
	return s_busy;
}

void fcWorkerPause(bool paused)
{
	s_paused = paused;
}
