#pragma once
#include <map>
#include <vector>
#include "ReadWriteLock.h"
#include "UniqueGenerator.h"
#include <unordered_map>

const unsigned char USE_TYPE_BUILD = 0;
const unsigned char USE_TYPE_SEARCH = 1;
class IndexNode;
class Index
{
public:
	Index();
	Index(unsigned char useType);
	IndexNode* getIndexNode(unsigned long long indexId);
	bool insert(unsigned long long indexId, IndexNode*& pIndexNode);
	unsigned long size();
	bool getLastNodes(unsigned long num, std::vector<unsigned long long>& indexIdVec, std::vector<IndexNode*>& indexNodeVec);
	bool reduceCache(unsigned long needReduceNum);
	bool reduceCache();
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//创建新的节点
	bool deleteIndexNode(unsigned long long indexId);										//删除节点
	void clearCache();																		//清除缓存
	unsigned char getUseType();																//获取使用方式
	bool putIndexNode(IndexNode* indexNode);												//外部使用完了告诉说外部已经不再引用
	unsigned long long acquireNumber(unsigned char numCount);								//获取连续的几个数
	void recycleNumber(unsigned long long indexId, unsigned char numCount);					//回收indexId
	void setInitMaxUniqueNum(unsigned long long initMaxUniqueNum);							//设置生成器的初始值
	~Index();																				//析构缓存
private:
	unsigned char useType;																	//使用的方式如果是用于查询的时候是多线程的
	std::unordered_map<unsigned long long, IndexNode*> indexNodeCache;						//这里保存一部分的索引节点缓存这样可以加快速度
	std::multimap<unsigned long long, unsigned long long> IndexIdPreority;					//这里保存索引的优先级key是索引的前面已经比较过的大小,越小优先级越大
	RTL_SRWLOCK rwLock;																		//缓存在搜索模式下会被多个线程使用到加个读写锁
	UniqueGenerator generator;																//唯一id生成器
};

//多线程读取的时候会用到的函数getIndexNode,insert,size,reduceCache,
