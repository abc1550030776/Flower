#pragma once
#include "Myfile.h"
#include "Index.h"

class IndexFile
{
public:
	IndexFile();
	void init(const Myfile& file, Index* index);

	IndexNode* getIndexNode(unsigned long long indexId);
private:
	Myfile indexFile;
	Index* pIndex;
};