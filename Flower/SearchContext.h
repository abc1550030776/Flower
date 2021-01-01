#pragma once
#include <set>
#include <map>
class LineAndColumn;
typedef std::map<unsigned long long, LineAndColumn> ResultMap;
class Index;
class SearchContext
{
public:
	SearchContext();
	bool init(const char* fileName, unsigned long threadNum = 0, bool searchLine = false);
	bool search(const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set);
	bool search(const char* searchTarget, unsigned int targetLen, ResultMap* map);
	~SearchContext();
private:
	Index* index;
	char* dstFileName;
	unsigned long threadNum;
	unsigned long long rootIndexNum;
	Index* kvIndex;
};
