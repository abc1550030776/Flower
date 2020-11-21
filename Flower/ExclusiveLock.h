#pragma once
typedef struct _EXCLOCK {
	volatile unsigned long Ptr;
} volatile EXCLOCK, * PEXCLOCK;

inline void acquireExclusive(PEXCLOCK EXCLock);
inline void releaseExclusive(PEXCLOCK EXCLock);