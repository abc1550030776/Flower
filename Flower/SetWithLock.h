#pragma once
#include <set>
#include "ExclusiveLock.h"

class SetWithLock
{
public:
	SetWithLock(std::set<unsigned long long>* set);
	void insert(unsigned long long startPos);
public:
	std::set<unsigned long long>* set;
	EXCLOCK lock;
};
