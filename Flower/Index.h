#pragma once
#include "IndexNode.h"
#include <map>
class Index
{
public:
	IndexNode* getIndexNode(unsigned long long indexId);
private:
	std::unordered_map<unsigned long long, IndexNode*> indexNodeCache;						//这里保存一部分的索引节点缓存这样可以加快速度
	std::multimap<unsigned long long, unsigned long long> IndexIdPreority;					//这里保存索引的优先级key是索引的前面已经比较过的大小,越小优先级越大
};