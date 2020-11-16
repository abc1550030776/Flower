#pragma once
#include "IndexNode.h"
#include <map>
#include <vector>
class Index
{
public:
	IndexNode* getIndexNode(unsigned long long indexId);
	bool insert(unsigned long long indexId, IndexNode* pIndexNode);
	unsigned int size();
	bool getLastNodes(unsigned int num, std::vector<unsigned long long>& indexIdVec, std::vector<IndexNode*>& indexNodeVec);
	bool reduceCache(unsigned int needReduceNum);
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//创建新的节点
	bool deleteIndexNode(unsigned long long indexId);										//删除节点
private:
	std::unordered_map<unsigned long long, IndexNode*> indexNodeCache;						//这里保存一部分的索引节点缓存这样可以加快速度
	std::multimap<unsigned long long, unsigned long long> IndexIdPreority;					//这里保存索引的优先级key是索引的前面已经比较过的大小,越小优先级越大
};