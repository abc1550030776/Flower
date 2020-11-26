#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include "ReadWriteLock.h"

#define LINUX_SRWLOCK_OWNED_BIT   0
#define LINUX_SRWLOCK_CONTENDED_BIT   1
#define LINUX_SRWLOCK_SHARED_BIT  2
#define LINUX_SRWLOCK_CONTENTION_LOCK_BIT 3
#define LINUX_SRWLOCK_OWNED   (1 << LINUX_SRWLOCK_OWNED_BIT)
#define LINUX_SRWLOCK_CONTENDED   (1 << LINUX_SRWLOCK_CONTENDED_BIT)
#define LINUX_SRWLOCK_SHARED  (1 << LINUX_SRWLOCK_SHARED_BIT)
#define LINUX_SRWLOCK_CONTENTION_LOCK (1 << LINUX_SRWLOCK_CONTENTION_LOCK_BIT)
#define LINUX_SRWLOCK_MASK    (LINUX_SRWLOCK_OWNED | LINUX_SRWLOCK_CONTENDED | \
                             LINUX_SRWLOCK_SHARED | LINUX_SRWLOCK_CONTENTION_LOCK)
#define LINUX_SRWLOCK_BITS    4

typedef struct _LINUX_SRWLOCK_SHARED_WAKE {
	long Wake;
	volatile struct _LINUX_SRWLOCK_SHARED_WAKE* Next;
} volatile LINUX_SRWLOCK_SHARED_WAKE, * PLINUX_SRWLOCK_SHARED_WAKE;

typedef struct _LINUX_SRWLOCK_WAITBLOCK {
	/* SharedCount is the number of shared acquirers. */
	unsigned long SharedCount;

	/* Last points to the last wait block in the chain. The value
	is only valid when read from the first wait block. */
	volatile struct _LINUX_SRWLOCK_WAITBLOCK* Last;

	/* Next points to the next wait block in the chain. */
	volatile struct _LINUX_SRWLOCK_WAITBLOCK* Next;

	union {
		/* Wake is only valid for exclusive wait blocks */
		long Wake;
		/* The wake chain is only valid for shared wait blocks */
		struct {
			PLINUX_SRWLOCK_SHARED_WAKE SharedWakeChain;
			PLINUX_SRWLOCK_SHARED_WAKE LastSharedWake;
		};
	};

	bool Exclusive;
} volatile LINUX_SRWLOCK_WAITBLOCK, * PLINUX_SRWLOCK_WAITBLOCK;

static void releaseWaitBlockLockExclusiveImpl(PRTL_SRWLOCK SRWLock, PLINUX_SRWLOCK_WAITBLOCK FirstWaitBlock)
{
	PLINUX_SRWLOCK_WAITBLOCK Next;
	unsigned long NewValue;

	/* NOTE: We're currently in an exclusive lock in contended mode. */
	Next = FirstWaitBlock->Next;
	if (Next != NULL) {
		/* There's more blocks chained, we need to update the pointers
		in the next wait block and update the wait block pointer. */
		NewValue = (unsigned long)Next | LINUX_SRWLOCK_OWNED | LINUX_SRWLOCK_CONTENDED;
		if (!FirstWaitBlock->Exclusive) {
			/* The next wait block has to be an exclusive lock! */
			assert(!!(Next->Exclusive));

			/* Save the shared count */
			Next->SharedCount = FirstWaitBlock->SharedCount;

			NewValue |= LINUX_SRWLOCK_SHARED;
		}

		Next->Last = FirstWaitBlock->Last;
	}
	else {
		/* Convert the lock to a simple lock. */
		if (FirstWaitBlock->Exclusive)
			NewValue = LINUX_SRWLOCK_OWNED;
		else {
			assert(FirstWaitBlock->SharedCount > 0);

			NewValue = ((unsigned long)FirstWaitBlock->SharedCount << LINUX_SRWLOCK_BITS) | LINUX_SRWLOCK_SHARED | LINUX_SRWLOCK_OWNED;
		}
	}

	(void)__sync_lock_test_and_set((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)NewValue);

	if (FirstWaitBlock->Exclusive) {
		(void)__sync_fetch_and_or(&FirstWaitBlock->Wake, 1);
	}
	else {
		PLINUX_SRWLOCK_SHARED_WAKE WakeChain, NextWake;

		/* If we were the first one to acquire the shared
		lock, we now need to wake all others... */
		WakeChain = FirstWaitBlock->SharedWakeChain;
		do {
			NextWake = WakeChain->Next;

			(void)__sync_fetch_and_or((long*)& WakeChain->Wake, 1);

			WakeChain = NextWake;
		} while (WakeChain != NULL);
	}
}

static inline void releaseWaitBlockLockLastSharedImpl(PRTL_SRWLOCK SRWLock, PLINUX_SRWLOCK_WAITBLOCK FirstWaitBlock)
{
	PLINUX_SRWLOCK_WAITBLOCK Next;
	unsigned long NewValue;

	/* NOTE: We're currently in a shared lock in contended mode. */

	/* The next acquirer to be unwaited *must* be an exclusive lock! */
	assert(!!(FirstWaitBlock->Exclusive));

	Next = FirstWaitBlock->Next;
	if (Next != nullptr) {
		/* There's more blocks chained, we need to update the pointers
		in the next wait block and update the wait block pointer. */
		NewValue = (unsigned long)Next | LINUX_SRWLOCK_OWNED | LINUX_SRWLOCK_CONTENDED;

		Next->Last = FirstWaitBlock->Last;
	}
	else {
		/* Convert the lock to a simple exclusive lock. */
		NewValue = LINUX_SRWLOCK_OWNED;
	}

	(void)__sync_lock_test_and_set((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)NewValue);

	(void)__sync_fetch_and_or(&FirstWaitBlock->Wake, 1);
}

static inline void releaseWaitBlockLockImpl(PRTL_SRWLOCK SRWLock)
{
	__sync_fetch_and_and((volatile unsigned long*)& SRWLock->Ptr, ~LINUX_SRWLOCK_CONTENTION_LOCK);
}

static inline PLINUX_SRWLOCK_WAITBLOCK acquireWaitBlockLockImpl(PRTL_SRWLOCK SRWLock)
{
	unsigned long PrevValue;
	PLINUX_SRWLOCK_WAITBLOCK WaitBlock;

	while (1) {
		PrevValue = __sync_fetch_and_or((volatile unsigned long*)& SRWLock->Ptr, LINUX_SRWLOCK_CONTENTION_LOCK);

		if (!(PrevValue & LINUX_SRWLOCK_CONTENTION_LOCK))
			break;

		pthread_yield();
	}

	if (!(PrevValue & LINUX_SRWLOCK_CONTENDED) || (PrevValue & ~LINUX_SRWLOCK_MASK) == 0) {
		/* Too bad, looks like the wait block was removed in the
		meanwhile, unlock again */
		releaseWaitBlockLockImpl(SRWLock);
		return NULL;
	}

	WaitBlock = (PLINUX_SRWLOCK_WAITBLOCK)(PrevValue & ~LINUX_SRWLOCK_MASK);

	return WaitBlock;
}

static inline void acquireSRWLockExclusiveWaitImpl(PRTL_SRWLOCK SRWLock, PLINUX_SRWLOCK_WAITBLOCK WaitBlock)
{
	unsigned long CurrentValue;

	while (1) {
		CurrentValue = *(volatile unsigned long*)& SRWLock->Ptr;
		if (!(CurrentValue & LINUX_SRWLOCK_SHARED)) {
			if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
				if (WaitBlock->Wake != 0) {
					/* Our wait block became the first one
					in the chain, we own the lock now! */
					break;
				}
			}
			else {
				/* The last wait block was removed and/or we're
				finally a simple exclusive lock. This means we
				don't need to wait anymore, we acquired the lock! */
				break;
			}
		}

		pthread_yield();
	}
}

static inline void acquireSRWLockSharedWaitImpl(PRTL_SRWLOCK SRWLock, PLINUX_SRWLOCK_WAITBLOCK FirstWait, PLINUX_SRWLOCK_SHARED_WAKE WakeChain)
{
	if (FirstWait != NULL) {
		while (WakeChain->Wake == 0) {
			pthread_yield();
		}
	}
	else {
		unsigned long CurrentValue;

		while (1) {
			CurrentValue = *(volatile unsigned long*)& SRWLock->Ptr;
			if (CurrentValue & LINUX_SRWLOCK_SHARED) {
				/* The LINUX_SRWLOCK_OWNED bit always needs to be set when
				LINUX_SRWLOCK_SHARED is set! */
				assert(CurrentValue & LINUX_SRWLOCK_OWNED);

				if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
					if (WakeChain->Wake != 0) {
						/* Our wait block became the first one
						in the chain, we own the lock now! */
						break;
					}
				}
				else {
					/* The last wait block was removed and/or we're
					finally a simple shared lock. This means we
					don't need to wait anymore, we acquired the lock! */
					break;
				}
			}

			pthread_yield();
		}
	}
}

void acquireSRWLockShared(PRTL_SRWLOCK SRWLock)
{
	LINUX_SRWLOCK_WAITBLOCK StackWaitBlock __attribute__((aligned(16)));
	LINUX_SRWLOCK_SHARED_WAKE SharedWake;
	unsigned long CurrentValue, NewValue;
	PLINUX_SRWLOCK_WAITBLOCK First, Shared, FirstWait;

	unsigned long addr = (unsigned long)(&StackWaitBlock);
	assert((addr & 0xf) == 0);

	while (1) {
		CurrentValue = *(volatile unsigned long*)& SRWLock->Ptr;

		if (CurrentValue & LINUX_SRWLOCK_SHARED) {
			/* NOTE: It is possible that the LINUX_SRWLOCK_OWNED bit is set! */

			if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
				/* There's other waiters already, lock the wait blocks and
				increment the shared count */
				First = acquireWaitBlockLockImpl(SRWLock);
				if (First != NULL) {
					FirstWait = NULL;

					if (First->Exclusive) {
						/* We need to setup a new wait block! Although
						we're currently in a shared lock and we're acquiring
						a shared lock, there are exclusive locks queued. We need
						to wait until those are released. */
						Shared = First->Last;

						if (Shared->Exclusive) {
							StackWaitBlock.Exclusive = false;
							StackWaitBlock.SharedCount = 1;
							StackWaitBlock.Next = NULL;
							StackWaitBlock.Last = &StackWaitBlock;
							StackWaitBlock.SharedWakeChain = &SharedWake;

							Shared->Next = &StackWaitBlock;
							First->Last = &StackWaitBlock;

							Shared = &StackWaitBlock;
							FirstWait = &StackWaitBlock;
						}
						else {
							Shared->LastSharedWake->Next = &SharedWake;
							Shared->SharedCount++;
						}
					}
					else {
						Shared = First;
						Shared->LastSharedWake->Next = &SharedWake;
						Shared->SharedCount++;
					}

					SharedWake.Next = NULL;
					SharedWake.Wake = 0;

					Shared->LastSharedWake = &SharedWake;

					releaseWaitBlockLockImpl(SRWLock);

					acquireSRWLockSharedWaitImpl(SRWLock, FirstWait, &SharedWake);

					/* Successfully incremented the shared count, we acquired the lock */
					break;
				}
			}
			else {
				/* This is a fastest path, just increment the number of
				current shared locks */

				/* Since the LINUX_SRWLOCK_SHARED bit is set, the LINUX_SRWLOCK_OWNED bit also has
				to be set! */

				assert(CurrentValue & LINUX_SRWLOCK_OWNED);

				NewValue = (CurrentValue >> LINUX_SRWLOCK_BITS) + 1;
				NewValue = (NewValue << LINUX_SRWLOCK_BITS) | (CurrentValue & LINUX_SRWLOCK_MASK);

				if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
					/* Successfully incremented the shared count, we acquired the lock */
					break;
				}
			}
		}
		else {
			if (CurrentValue & LINUX_SRWLOCK_OWNED) {
				/* The resource is currently acquired exclusively */
				if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
					SharedWake.Next = NULL;
					SharedWake.Wake = 0;

					/* There's other waiters already, lock the wait blocks and
					increment the shared count. If the last block in the chain
					is an exclusive lock, add another block. */

					StackWaitBlock.Exclusive = false;
					StackWaitBlock.SharedCount = 0;
					StackWaitBlock.Next = NULL;
					StackWaitBlock.Last = &StackWaitBlock;
					StackWaitBlock.SharedWakeChain = &SharedWake;

					First = acquireWaitBlockLockImpl(SRWLock);
					if (First != NULL) {
						Shared = First->Last;
						if (Shared->Exclusive) {
							Shared->Next = &StackWaitBlock;
							First->Last = &StackWaitBlock;

							Shared = &StackWaitBlock;
							FirstWait = &StackWaitBlock;
						}
						else {
							FirstWait = NULL;
							Shared->LastSharedWake->Next = &SharedWake;
						}

						Shared->SharedCount++;
						Shared->LastSharedWake = &SharedWake;

						releaseWaitBlockLockImpl(SRWLock);

						acquireSRWLockSharedWaitImpl(SRWLock,
							FirstWait,
							&SharedWake);

						/* Successfully incremented the shared count, we acquired the lock */
						break;
					}
				}
				else {
					SharedWake.Next = NULL;
					SharedWake.Wake = 0;

					/* We need to setup the first wait block. Currently an exclusive lock is
					held, change the lock to contended mode. */
					StackWaitBlock.Exclusive = false;
					StackWaitBlock.SharedCount = 1;
					StackWaitBlock.Next = NULL;
					StackWaitBlock.Last = &StackWaitBlock;
					StackWaitBlock.SharedWakeChain = &SharedWake;
					StackWaitBlock.LastSharedWake = &SharedWake;

					NewValue = (unsigned long)& StackWaitBlock | LINUX_SRWLOCK_OWNED | LINUX_SRWLOCK_CONTENDED;
					if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
						acquireSRWLockSharedWaitImpl(SRWLock,
							&StackWaitBlock,
							&SharedWake);

						/* Successfully set the shared count, we acquired the lock */
						break;
					}
				}
			}
			else {
				/* This is a fast path, we can simply try to set the shared count to 1 */
				NewValue = (1 << LINUX_SRWLOCK_BITS) | LINUX_SRWLOCK_SHARED | LINUX_SRWLOCK_OWNED;

				/* The LINUX_SRWLOCK_CONTENDED bit should never be set if neither the
				LINUX_SRWLOCK_SHARED nor the LINUX_SRWLOCK_OWNED bit is set */
				assert(!(CurrentValue & LINUX_SRWLOCK_CONTENDED));

				if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
					/* Successfully set the shared count, we acquired the lock */
					break;
				}
			}
		}

		pthread_yield();
	}
}

void releaseSRWLockShared(PRTL_SRWLOCK SRWLock)
{
	unsigned long CurrentValue, NewValue;
	PLINUX_SRWLOCK_WAITBLOCK WaitBlock;
	bool LastShared;

	while (1) {
		CurrentValue = *(volatile unsigned long*)& SRWLock->Ptr;

		if (CurrentValue & LINUX_SRWLOCK_SHARED) {
			if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
				/* There's a wait block, we need to wake a pending
				exclusive acquirer if this is the last shared release */
				WaitBlock = acquireWaitBlockLockImpl(SRWLock);
				if (WaitBlock != NULL) {
					LastShared = (--WaitBlock->SharedCount == 0);

					if (LastShared)
						releaseWaitBlockLockLastSharedImpl(SRWLock, WaitBlock);
					else
						releaseWaitBlockLockImpl(SRWLock);

					/* We released the lock */
					break;
				}
			}
			else {
				/* This is a fast path, we can simply decrement the shared
				count and store the pointer */
				NewValue = CurrentValue >> LINUX_SRWLOCK_BITS;

				if (--NewValue != 0) {
					NewValue = (NewValue << LINUX_SRWLOCK_BITS) | LINUX_SRWLOCK_SHARED | LINUX_SRWLOCK_OWNED;
				}

				if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
					/* Successfully released the lock */
					break;
				}
			}
		}
		else {
			/* The LINUX_SRWLOCK_SHARED bit has to be present now,
			even in the contended case! */
			//RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
			assert(false);
		}

		pthread_yield();
	}
}

unsigned char InterlockedBitTestAndSet(volatile int* Base, int Offset)
{
	unsigned char old;
	__asm__ __volatile__("lock bts{l %[Offset],%[Base] | %[Base],%[Offset]}" "\n\tsetc %[old]"
		: [old] "=qm"(old), [Base] "+m" (*Base)
		: [Offset] "I" "r" (Offset)
		: "memory", "cc");
	return old;
}

void acquireSRWLockExclusive(PRTL_SRWLOCK SRWLock)
{
	LINUX_SRWLOCK_WAITBLOCK StackWaitBlock __attribute__((aligned(16)));
	PLINUX_SRWLOCK_WAITBLOCK First, Last;

	unsigned long addr = (unsigned long)(&StackWaitBlock);
	assert((addr & 0xf) == 0);


	if (InterlockedBitTestAndSet((int volatile*)& SRWLock->Ptr, LINUX_SRWLOCK_OWNED_BIT)) {
		unsigned long CurrentValue, NewValue;

		while (1) {
			CurrentValue = *(volatile unsigned long*)& SRWLock->Ptr;

			if (CurrentValue & LINUX_SRWLOCK_SHARED) {
				/* A shared lock is being held right now. We need to add a wait block! */

				if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
					goto AddWaitBlock;
				}
				else {
					/* There are no wait blocks so far, we need to add ourselves as the first
					wait block. We need to keep the shared count! */
					StackWaitBlock.Exclusive = true;
					StackWaitBlock.SharedCount = (unsigned long)(CurrentValue >> LINUX_SRWLOCK_BITS);
					StackWaitBlock.Next = NULL;
					StackWaitBlock.Last = &StackWaitBlock;
					StackWaitBlock.Wake = 0;

					NewValue = (unsigned long)& StackWaitBlock | LINUX_SRWLOCK_SHARED | LINUX_SRWLOCK_CONTENDED | LINUX_SRWLOCK_OWNED;

					if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
						acquireSRWLockExclusiveWaitImpl(SRWLock, &StackWaitBlock);

						/* Successfully acquired the exclusive lock */
						break;
					}
				}
			}
			else {
				if (CurrentValue & LINUX_SRWLOCK_OWNED) {
					/* An exclusive lock is being held right now. We need to add a wait block! */

					if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
					AddWaitBlock:
						StackWaitBlock.Exclusive = true;
						StackWaitBlock.SharedCount = 0;
						StackWaitBlock.Next = NULL;
						StackWaitBlock.Last = &StackWaitBlock;
						StackWaitBlock.Wake = 0;

						First = acquireWaitBlockLockImpl(SRWLock);
						if (First != NULL) {
							Last = First->Last;
							Last->Next = &StackWaitBlock;
							First->Last = &StackWaitBlock;

							releaseWaitBlockLockImpl(SRWLock);

							acquireSRWLockExclusiveWaitImpl(SRWLock, &StackWaitBlock);

							/* Successfully acquired the exclusive lock */
							break;
						}
					}
					else {
						/* There are no wait blocks so far, we need to add ourselves as the first
						wait block. We need to keep the shared count! */
						StackWaitBlock.Exclusive = true;
						StackWaitBlock.SharedCount = 0;
						StackWaitBlock.Next = NULL;
						StackWaitBlock.Last = &StackWaitBlock;
						StackWaitBlock.Wake = 0;

						NewValue = (unsigned long)& StackWaitBlock | LINUX_SRWLOCK_OWNED | LINUX_SRWLOCK_CONTENDED;
						if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
							acquireSRWLockExclusiveWaitImpl(SRWLock, &StackWaitBlock);

							/* Successfully acquired the exclusive lock */
							break;
						}
					}
				}
				else {
					if (!InterlockedBitTestAndSet((int volatile*)& SRWLock->Ptr, LINUX_SRWLOCK_OWNED_BIT)) {
						/* We managed to get hold of a simple exclusive lock! */
						break;
					}
				}
			}

			pthread_yield();
		}
	}
}

void releaseSRWLockExclusive(PRTL_SRWLOCK SRWLock)
{
	unsigned long CurrentValue, NewValue;
	PLINUX_SRWLOCK_WAITBLOCK WaitBlock;

	while (1) {
		CurrentValue = *(volatile unsigned long*)& SRWLock->Ptr;

		if (!(CurrentValue & LINUX_SRWLOCK_OWNED)) {
			//RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
			assert(false);
		}

		if (!(CurrentValue & LINUX_SRWLOCK_SHARED)) {
			if (CurrentValue & LINUX_SRWLOCK_CONTENDED) {
				/* There's a wait block, we need to wake the next pending
				acquirer (exclusive or shared) */
				WaitBlock = acquireWaitBlockLockImpl(SRWLock);
				if (WaitBlock != NULL) {
					releaseWaitBlockLockExclusiveImpl(SRWLock, WaitBlock);

					/* We released the lock */
					break;
				}
			}
			else {
				/* This is a fast path, we can simply clear the LINUX_SRWLOCK_OWNED
				bit. All other bits should be 0 now because this is a simple
				exclusive lock and no one is waiting. */

				assert(!(CurrentValue & ~LINUX_SRWLOCK_OWNED));

				NewValue = 0;
				if (__sync_bool_compare_and_swap((unsigned long volatile*)& SRWLock->Ptr, (unsigned long)CurrentValue, (unsigned long)NewValue)) {
					/* We released the lock */
					break;
				}
			}
		}
		else {
			/* The LINUX_SRWLOCK_SHARED bit must not be present now,
			not even in the contended case! */
			//RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
			assert(false);
		}

		pthread_yield();
	}
}
