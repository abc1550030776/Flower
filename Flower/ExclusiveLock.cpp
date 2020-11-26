#include "ExclusiveLock.h"
#include <pthread.h>

#define LINUX_EXCLOCK_LOCK_BIT 0

#define LINUX_EXCLOCK_LOCK (1 << LINUX_EXCLOCK_LOCK_BIT)


void acquireExclusive(PEXCLOCK EXCLock)
{
	unsigned long PrevValue;
	while (1)
	{
		PrevValue = __sync_fetch_and_or((volatile unsigned long*)& EXCLock->Ptr, LINUX_EXCLOCK_LOCK);
		if (!(PrevValue & LINUX_EXCLOCK_LOCK))
		{
			break;
		}

		pthread_yield();
	}
}

void releaseExclusive(PEXCLOCK EXCLock)
{
	__sync_fetch_and_and((volatile unsigned long*)& EXCLock->Ptr, ~LINUX_EXCLOCK_LOCK);
}
