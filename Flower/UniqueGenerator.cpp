#include "UniqueGenerator.h"

UniqueGenerator::UniqueGenerator()
{
	maxUniqueNum = 1;
	memset(recycleBitmap, 0, sizeof(recycleBitmap));
}

void UniqueGenerator::setInitMaxUniqueNum(unsigned long long initMaxUniqueNum)
{
	std::lock_guard<std::mutex> lock(mutex_);
	maxUniqueNum = initMaxUniqueNum;
}

unsigned long long UniqueGenerator::acquireNumber(unsigned char numberCount)
{
	std::lock_guard<std::mutex> lock(mutex_);

	unsigned short startBit = (unsigned short)(numberCount - 1);
	unsigned short wordIdx = startBit / 64;
	unsigned short bitIdx = startBit % 64;

	uint64_t mask = recycleBitmap[wordIdx] >> bitIdx << bitIdx;

	for (unsigned short w = wordIdx; w < BITMAP_WORD_COUNT; ++w)
	{
		if (mask == 0)
		{
			if (w + 1 < BITMAP_WORD_COUNT)
			{
				mask = recycleBitmap[w + 1];
			}
			continue;
		}

		unsigned short bit = (unsigned short)(w * 64 + __builtin_ctzll(mask));
		if (bit >= RECYCLE_BUCKET_COUNT)
		{
			break;
		}

		unsigned long long returnVal = everyRecycleNumber[bit].top();
		everyRecycleNumber[bit].pop();

		if (everyRecycleNumber[bit].empty())
		{
			recycleBitmap[bit / 64] &= ~(1ULL << (bit % 64));
		}

		if (bit > (unsigned short)(numberCount - 1))
		{
			unsigned short remainBucket = (unsigned short)(bit - numberCount);
			everyRecycleNumber[remainBucket].push(returnVal + numberCount);
			recycleBitmap[remainBucket / 64] |= (1ULL << (remainBucket % 64));
		}

		return returnVal;
	}

	unsigned long long ret = maxUniqueNum;
	maxUniqueNum += numberCount;
	return ret;
}

void UniqueGenerator::recycleNumber(unsigned long long number, unsigned char numberCount)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (numberCount > RECYCLE_BUCKET_COUNT)
	{
		return;
	}

	unsigned short bucket = (unsigned short)(numberCount - 1);
	everyRecycleNumber[bucket].push(number);
	recycleBitmap[bucket / 64] |= (1ULL << (bucket % 64));
}
