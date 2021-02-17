#pragma once
#include <set>
#include <map>
#include "SearchContext.h"
class LineAndColumn
{
public:
	LineAndColumn();
	LineAndColumn(unsigned long long lineNum, unsigned long long columnNum);
	unsigned long long GetLineNum();
	unsigned long long GetColumnNum();
private:
	unsigned long long lineNum;
	unsigned long long columnNum;
};

typedef std::map<unsigned long long, LineAndColumn> ResultMap;

bool BuildDstIndex(const char* fileName, bool needBuildLineIndex = false, char delimiter = '\n');

//bool SearchFile(const char* fileName, const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set);
