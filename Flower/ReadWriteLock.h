#pragma once

typedef struct _RTL_SRWLOCK {
	volatile unsigned long Ptr;
} volatile RTL_SRWLOCK, * PRTL_SRWLOCK;

inline void acquireSRWLockExclusive(PRTL_SRWLOCK SRWLock);
inline void releaseSRWLockExclusive(PRTL_SRWLOCK SRWLock);

inline void acquireSRWLockShared(PRTL_SRWLOCK SRWLock);
inline void releaseSRWLockShared(PRTL_SRWLOCK SRWLock);