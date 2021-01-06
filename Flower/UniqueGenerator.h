#pragma once
#include <stack>
#include "common.h"

class UniqueGenerator {

	unsigned long long maxUniqueNum;
	std::stack<unsigned long long> everyRecycleNumber[MAX_SIZE_PER_INDEX_NODE / SIZE_PER_INDEX_FILE_GRID];

public:
	UniqueGenerator();
	void setInitMaxUniqueNum(unsigned long long initMaxUniqueNum);						//设置初始的时候的从哪个数开始
	unsigned long long acquireNumber(unsigned char numberCount);								//获取一定数量的连续number
	void recycleNumber(unsigned long long number, unsigned char numberCount);					//回收一定数量连续的number
};
