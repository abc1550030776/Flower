#include "UniqueGenerator.h"

UniqueGenerator::UniqueGenerator()
{
	maxUniqueNum = 1;
}

unsigned long long UniqueGenerator::acquireNumber()
{
	if (recycleNumbers.empty())
	{
		return maxUniqueNum++;
	}

	unsigned long long returnVal = recycleNumbers.top();
	recycleNumbers.pop();
	return returnVal;
}

unsigned long long UniqueGenerator::acquireTwoNumber()
{
	unsigned long long ret = maxUniqueNum;
	maxUniqueNum += 2;
	return ret;
}

void UniqueGenerator::recycleNumber(unsigned long long number)
{
	recycleNumbers.push(number);
}

UniqueGenerator& UniqueGenerator::getUGenerator()
{
	static UniqueGenerator uGeneratorInstance;
	return uGeneratorInstance;
}