#pragma once
#include "Myfile.h"
#include "Index.h"
#include <unordered_set>

class Index;
class IndexNode;
class IndexFile
{
public:
	IndexFile();
	bool init(const char* fileName, Index* index);

	IndexNode* getIndexNode(unsigned long long indexId);
	IndexNode* getTempIndexNode(unsigned long long indexId);
	bool writeFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool writeTempFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool reduceCache();
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//创建新的节点
	bool deleteIndexNode(unsigned long long indexId);										//删除节点
	void setRootIndexId(unsigned long long rootIndexId);									//设置根节点id
	unsigned long long getRootIndexId();													//获取根节点id
	bool writeEveryCache();																	//把缓存当中的数据全部写盘
	bool putIndexNode(IndexNode* indexNode);												//外部使用完了告诉说外部已经不再引用
private:
	Myfile indexFile;
	Index* pIndex;
	std::unordered_set<unsigned long long> tempIndexNodeId;
	unsigned long long rootIndexId;
};
