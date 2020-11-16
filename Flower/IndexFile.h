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
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//�����µĽڵ�
	bool deleteIndexNode(unsigned long long indexId);										//ɾ���ڵ�
private:
	Myfile indexFile;
	Index* pIndex;
	std::unordered_set<unsigned long long> tempIndexNodeId;
};