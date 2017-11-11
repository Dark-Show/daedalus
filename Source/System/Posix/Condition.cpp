#include "stdafx.h"
#include "System/Condition.h"
#include "System/Mutex.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>

const double kTimeoutInfinity = 0.f;

// Condition wrapper derived from GLFW 2.7, see http://www.glfw.org/.

Condition* ConditionCreate()
{
	pthread_cond_t* cond = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
	if (!cond)
	{
		return NULL;
	}

	pthread_cond_init(cond, NULL);
	return (Condition*)cond;
}

void ConditionDestroy(Condition* cond)
{
	pthread_cond_destroy((pthread_cond_t*)cond);
	free(cond);
}

static void ComputeWait(double timeout, timespec* wait)
{
	timeval currenttime;
	gettimeofday(&currenttime, NULL);
	long dt_sec = (long)timeout;
	long dt_usec = (long)((timeout - (double)dt_sec) * 1000000.0);

	wait->tv_nsec = (currenttime.tv_usec + dt_usec) * 1000L;
	if (wait->tv_nsec > 1000000000L)
	{
		wait->tv_nsec -= 1000000000L;
		dt_sec++;
	}
	wait->tv_sec = currenttime.tv_sec + dt_sec;
}

void ConditionWait(Condition* cond, Mutex* mutex, double timeout)
{
	if (timeout <= 0)
	{
		pthread_cond_wait((pthread_cond_t*)cond, &mutex->mMutex);
	}
	else
	{
		timespec wait;
		ComputeWait(timeout, &wait);

		pthread_cond_timedwait((pthread_cond_t*)cond, &mutex->mMutex, &wait);
	}
}

void ConditionSignal(Condition* cond) { pthread_cond_signal((pthread_cond_t*)cond); }
