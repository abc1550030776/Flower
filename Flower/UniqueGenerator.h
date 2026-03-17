#pragma once
#include <stack>
#include <mutex>
#include <cstdint>
#include <cstring>
#include "common.h"

static const unsigned short RECYCLE_BUCKET_COUNT = MAX_SIZE_PER_INDEX_NODE / SIZE_PER_INDEX_FILE_GRID;
static const unsigned short BITMAP_WORD_COUNT = (RECYCLE_BUCKET_COUNT + 63) / 64;

class UniqueGenerator {

	unsigned long long maxUniqueNum;
	std::stack<unsigned long long> everyRecycleNumber[RECYCLE_BUCKET_COUNT];
	uint64_t recycleBitmap[BITMAP_WORD_COUNT];
	mutable std::mutex mutex_;

public:
	UniqueGenerator();
	void setInitMaxUniqueNum(unsigned long long initMaxUniqueNum);						//设置初始的时候的从哪个数开始
	unsigned long long acquireNumber(unsigned char numberCount);								//获取一定数量的连续number
	void recycleNumber(unsigned long long number, unsigned char numberCount);					//回收一定数量连续的number
};
