#pragma once
#include "Myfile.h"
#include "Index.h"

class IndexFile
{
public:
	IndexFile();
	void init(const Myfile& file, Index* index);

	IndexNode* getIndexNode(unsigned long long indexId);
	IndexNode* getTempIndexNode(unsigned long long indexId);
	bool writeFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool writeTempFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool reduceCache();
private:
	Myfile indexFile;
	Index* pIndex;
	std::unordered_set<unsigned long long> tempIndexNodeId;
};