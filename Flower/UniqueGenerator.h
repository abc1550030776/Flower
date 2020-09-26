#pragma once
#include <stack>

class UniqueGenerator {

	UniqueGenerator();
	std::stack<unsigned long long> recycleNumbers;
	unsigned long long maxUniqueNum;

public:
	unsigned long long acquireNumber();
	void recycleNumber(unsigned long long number);
	static UniqueGenerator& getUGenerator();
};