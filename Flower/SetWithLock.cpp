#include "SetWithLock.h"

SetWithLock::SetWithLock(EXCLOCK& lock) : lock(lock)
{
}

void SetWithLock::insert(unsigned long long startPos)
{
	acquireExclusive(&lock);
	set.insert(startPos);
	releaseExclusive(&lock);
}