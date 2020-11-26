#pragma once
typedef struct _EXCLOCK {
	volatile unsigned long Ptr;
} volatile EXCLOCK, * PEXCLOCK;

void acquireExclusive(PEXCLOCK EXCLock);
void releaseExclusive(PEXCLOCK EXCLock);
