#pragma once

typedef struct _RTL_SRWLOCK {
	volatile unsigned long Ptr;
} volatile RTL_SRWLOCK, * PRTL_SRWLOCK;

void acquireSRWLockExclusive(PRTL_SRWLOCK SRWLock);
void releaseSRWLockExclusive(PRTL_SRWLOCK SRWLock);

void acquireSRWLockShared(PRTL_SRWLOCK SRWLock);
void releaseSRWLockShared(PRTL_SRWLOCK SRWLock);
