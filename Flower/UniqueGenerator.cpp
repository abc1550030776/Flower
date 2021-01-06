#include "UniqueGenerator.h"

UniqueGenerator::UniqueGenerator()
{
	maxUniqueNum = 1;
}

void UniqueGenerator::setInitMaxUniqueNum(unsigned long long initMaxUniqueNum)
{
	maxUniqueNum = initMaxUniqueNum;
}

unsigned long long UniqueGenerator::acquireNumber(unsigned char numberCount)
{
	//首先从已经回收了的数字里面获取数字
	for (unsigned char i = (unsigned char)(numberCount - 1); i < MAX_SIZE_PER_INDEX_NODE / SIZE_PER_INDEX_FILE_GRID; ++i)
	{
		if (!everyRecycleNumber[i].empty())
		{
			unsigned long long returnVal = everyRecycleNumber[i].top();
			everyRecycleNumber[i].pop();
			if (i > (numberCount - 1))
			{
				everyRecycleNumber[i - numberCount].push(returnVal + numberCount);
			}
			return returnVal;
		}
	}

	//回收的数字当中没有数字可以用从最大值当中分配
	unsigned long long ret = maxUniqueNum;
	maxUniqueNum += numberCount;
	return ret;
}

void UniqueGenerator::recycleNumber(unsigned long long number, unsigned char numberCount)
{
	if (numberCount > MAX_SIZE_PER_INDEX_NODE / SIZE_PER_INDEX_FILE_GRID)
	{
		return;
	}

	everyRecycleNumber[numberCount - 1].push(number);
}
