#pragma once

typedef struct _RTL_SRWLOCK {
	volatile unsigned long Ptr;
} volatile RTL_SRWLOCK, * PRTL_SRWLOCK;

void acquireSRWLockExclusive(PRTL_SRWLOCK SRWLock);
void releaseSRWLockExclusive(PRTL_SRWLOCK SRWLock);

void acquireSRWLockShared(PRTL_SRWLOCK SRWLock);
void releaseSRWLockShared(PRTL_SRWLOCK SRWLock);

class UniqueLock
{
public:
	UniqueLock(PRTL_SRWLOCK SRWLock);
	~UniqueLock();
	PRTL_SRWLOCK SRWLock;
};

class ShareLock
{
public:
	ShareLock(PRTL_SRWLOCK SRWLock);
	~ShareLock();
	PRTL_SRWLOCK SRWLock;
};