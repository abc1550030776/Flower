#include "Index.h"
#include "UniqueGenerator.h"
#include "IndexNode.h"
#include "common.h"

Index::Index()
{
	useType = USE_TYPE_SEARCH;
	rwLock.Ptr = 0;
}

Index::Index(unsigned char useType)
{
	this->useType = useType;
	rwLock.Ptr = 0;
}

IndexNode* Index::getIndexNode(unsigned long long indexId)
{
	acquireSRWLockShared(&rwLock);
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		releaseSRWLockShared(&rwLock);
		return nullptr;
	}

	IndexNode* ret = it->second;
	ret->increaseRef();
	releaseSRWLockShared(&rwLock);
	return ret;
}

bool Index::insert(unsigned long long indexId, IndexNode*& pIndexNode)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}

	acquireSRWLockExclusive(&rwLock);
	auto pair = indexNodeCache.insert({ indexId, pIndexNode });
	if (!pair.second)
	{
		//在搜索模式下面可能同时读取文件进行插入的操作这个时候其中一个已经插入了这边就直接返回就行了
		if (useType == USE_TYPE_SEARCH)
		{
			delete pIndexNode;
			pIndexNode = pair.first->second;
			pIndexNode->increaseRef();
			releaseSRWLockExclusive(&rwLock);
			return true;
		}
		releaseSRWLockExclusive(&rwLock);
		return false;
	}

	//添加了索引缓存的同时也要添加优先级缓存
	IndexIdPreority.insert({ pIndexNode->getPreCmpLen(), indexId });
	pIndexNode->increaseRef();
	releaseSRWLockExclusive(&rwLock);
	return true;
}

unsigned long Index::size()
{
	return indexNodeCache.size();
}

bool Index::getLastNodes(unsigned long num, std::vector<unsigned long long>& indexIdVec, std::vector<IndexNode*>& indexNodeVec)
{
	if (indexNodeCache.size() < num)
	{
		return false;
	}
	auto it = end(IndexIdPreority);
	for (unsigned int i = 0; i < num; ++i)
	{
		--it;
		auto cacheIt = indexNodeCache.find(it->second);
		if (cacheIt == end(indexNodeCache))
		{
			return false;
		}
		indexIdVec.push_back(it->second);
		indexNodeVec.push_back(cacheIt->second);
	}
	return true;
}

bool Index::reduceCache(unsigned long needReduceNum)
{
	if (indexNodeCache.size() < needReduceNum)
	{
		return false;
	}

	auto it = end(IndexIdPreority);
	--it;
	for (unsigned int i = 0; i < needReduceNum; ++i)
	{
		auto curIt = it--;
		auto cacheIt = indexNodeCache.find(curIt->second);
		if (cacheIt == end(indexNodeCache))
		{
			return false;
		}

		//删除之前先把内存的数据删除
		delete cacheIt->second;
		indexNodeCache.erase(cacheIt);
		IndexIdPreority.erase(curIt);
	}
	return true;
}

bool Index::reduceCache()
{
	if (getAvailableMemRate() >= 0.2)
	{
		return true;
	}
	acquireSRWLockExclusive(&rwLock);

	unsigned long needReduceNum = indexNodeCache.size() / 5;
	auto it = end(IndexIdPreority);
	--it;
	for (unsigned int i = 0; i < needReduceNum; ++i)
	{
		auto curIt = it--;
		auto cacheIt = indexNodeCache.find(curIt->second);
		if (cacheIt == end(indexNodeCache))
		{
			releaseSRWLockExclusive(&rwLock);
			return false;
		}

		//删除之前先看一下是否外面有引用这个节点
		if (cacheIt->second->isZeroRef())
		{
			delete cacheIt->second;
		}
		indexNodeCache.erase(cacheIt);
		IndexIdPreority.erase(curIt);
	}
	releaseSRWLockExclusive(&rwLock);
	return true;
}

bool Index::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	//根据旧的preCmpLen和indexId在优先级表当中找到对应项
	auto ret = IndexIdPreority.equal_range(orgPreCmpLen);
	auto it = ret.first;
	for (; it != ret.second; ++it)
	{
		if (it->second == indexId)
		{
			break;
		}
	}

	if (it == ret.second)
	{
		return false;
	}

	//从缓存当中获取那个节点
	auto cacheIt = indexNodeCache.find(indexId);
	if (cacheIt == end(indexNodeCache))
	{
		return false;
	}

	if (cacheIt->second == nullptr)
	{
		return false;
	}

	//从优先级表当中先删掉再修改
	IndexIdPreority.erase(it);
	IndexIdPreority.insert({ newPreCmpLen, indexId });
	//把缓存里面的preCmpId改掉
	cacheIt->second->setPreCmpLen(newPreCmpLen);
	return true;
}

bool Index::swapNode(unsigned long long indexId, IndexNode* newNode)
{
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		return false;
	}
	it->second = newNode;
	return true;
}

IndexNode* Index::newIndexNode(unsigned char nodeType, unsigned long long preCmpLen)
{
	//根据类型创建新的节点
	IndexNode* pNode = nullptr;
	switch (nodeType)
	{
	case NODE_TYPE_ONE:
		pNode = new IndexNodeTypeOne();
		break;
	case NODE_TYPE_TWO:
		pNode = new IndexNodeTypeTwo();
		break;
	case NODE_TYPE_THREE:
		pNode = new IndexNodeTypeThree();
		break;
	case NODE_TYPE_FOUR:
		pNode = new IndexNodeTypeFour();
		break;
	default:
		break;
	}

	if (pNode == nullptr)
	{
		return nullptr;
	}

	//获取新创建的节点的id
	unsigned long long indexId = generator.acquireNumber();

	//将新创建的节点插入到缓存当中
	bool ok = indexNodeCache.insert({ indexId, pNode }).second;
	if (!ok)
	{
		delete pNode;
		return nullptr;
	}

	//添加了索引缓存的同时也要添加优先级缓存
	IndexIdPreority.insert({ preCmpLen, indexId });

	//设置preCmdLen
	pNode->setPreCmpLen(preCmpLen);

	//设置indexId
	pNode->setIndexId(indexId);

	return pNode;
}

bool Index::deleteIndexNode(unsigned long long indexId)
{
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		return false;
	}

	auto range = IndexIdPreority.equal_range(it->second->getPreCmpLen());
	auto ipIt = range.first;
	for (; ipIt != range.second; ++ipIt)
	{
		if (ipIt->second == indexId)
		{
			break;
		}
	}

	if (ipIt == range.second)
	{
		return false;
	}

	delete it->second;
	indexNodeCache.erase(it);
	IndexIdPreority.erase(ipIt);
	generator.recycleNumber(indexId);
	return true;
}

void Index::clearCache()
{
	for (auto& value : indexNodeCache)
	{
		delete value.second;
	}

	indexNodeCache.clear();
	IndexIdPreority.clear();
}

unsigned char Index::getUseType()
{
	return useType;
}

bool Index::putIndexNode(IndexNode* indexNode)
{
	if (indexNode == nullptr)
	{
		return false;
	}
	acquireSRWLockShared(&rwLock);
	if (indexNode->decreaseAndTestZero())
	{
		//已经没有缓存或者是缓存清除从新添加了新的节点就删除
		auto it = indexNodeCache.find(indexNode->getIndexId());
		if (it == end(indexNodeCache) || it->second != indexNode)
		{
			delete indexNode;
		}
	}
	releaseSRWLockShared(&rwLock);
	return true;
}

unsigned long long Index::acquireTwoNumber()
{
	return generator.acquireTwoNumber();
}

void Index::recycleNumber(unsigned long long indexId)
{
	generator.recycleNumber(indexId);
}

Index::~Index()
{
	clearCache();
}
