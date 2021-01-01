#pragma once
#include "ExclusiveLock.h"
#include <map>
class LineAndColumn;
typedef std::map<unsigned long long, LineAndColumn> ResultMap;
class ResultMapWithLock 
{
public:
	ResultMapWithLock(ResultMap& resultMap);
	void insert(unsigned long long filePos, unsigned long long lineNum, unsigned long long columnNum);
private:
	ResultMap& resultMap;
	EXCLOCK lock;
};
