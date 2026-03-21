#include "ResultMapWithLock.h"
#include "interface.h"

ResultMapWithLock::ResultMapWithLock(ResultMap& resultMap) : resultMap(resultMap)
{
	lock.Ptr = 0;
}

void ResultMapWithLock::insert(unsigned long long filePos, unsigned long long lineNum, unsigned long long columnNum,
	unsigned long long endLineNum, unsigned long long endColumnNum)
{
	acquireExclusive(&lock);
	resultMap.insert({ filePos , {lineNum, columnNum, endLineNum, endColumnNum} });
	releaseExclusive(&lock);
}
