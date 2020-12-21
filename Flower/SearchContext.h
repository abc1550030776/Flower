#pragma once
#include <set>
class Index;
class SearchContext
{
public:
	SearchContext();
	bool init(const char* fileName);
	bool search(const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set);
	~SearchContext();
private:
	Index* index;
	char* dstFileName;
};
