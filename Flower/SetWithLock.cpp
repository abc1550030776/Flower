#include "SetWithLock.h"

SetWithLock::SetWithLock(std::set<unsigned long long>* set) : set(set)
{
	lock.Ptr = 0;
}

void SetWithLock::insert(unsigned long long startPos)
{
	if (set == nullptr)
	{
		return;
	}
	acquireExclusive(&lock);
	set->insert(startPos);
	releaseExclusive(&lock);
}
