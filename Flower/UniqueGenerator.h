#pragma once
#include <stack>

class UniqueGenerator {

	std::stack<unsigned long long> recycleNumbers;
	unsigned long long maxUniqueNum;

public:
	UniqueGenerator();
	unsigned long long acquireNumber();
	unsigned long long acquireTwoNumber();
	void recycleNumber(unsigned long long number);
};
