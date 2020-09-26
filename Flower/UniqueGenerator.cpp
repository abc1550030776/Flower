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

void UniqueGenerator::recycleNumber(unsigned long long number)
{
	recycleNumbers.push(number);
}

UniqueGenerator& UniqueGenerator::getUGenerator()
{
	static UniqueGenerator uGeneratorInstance;
	return uGeneratorInstance;
}