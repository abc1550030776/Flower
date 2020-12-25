#pragma once
#include <set>
class Index;
class SearchContext
{
public:
	SearchContext();
	bool init(const char* fileName, unsigned long threadNum = 0);
	bool search(const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set);
	~SearchContext();
private:
	Index* index;
	char* dstFileName;
	unsigned long threadNum;
	unsigned long long rootIndexNum;
};
