#include "Index.h"
#include "UniqueGenerator.h"
#include "IndexNode.h"
#include "common.h"
#include "MemoryPool.h"

Index::Index()
{
	useType = USE_TYPE_SEARCH;
	rwLock.Ptr = 0;
	poolManager = new IndexNodePoolManager();
}

Index::Index(unsigned char useType)
{
	this->useType = useType;
	rwLock.Ptr = 0;
	poolManager = new IndexNodePoolManager();
}

IndexNode* Index::getIndexNode(unsigned long long indexId)
{
	ShareLock lock(&rwLock);
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		return nullptr;
	}

	IndexNode* ret = it->second;
	ret->increaseRef();
	return ret;
}

bool Index::insert(unsigned long long indexId, IndexNode*& pIndexNode)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}

	UniqueLock lock(&rwLock);
	auto pair = indexNodeCache.insert({ indexId, pIndexNode });
	if (!pair.second)
	{
		//在搜索模式下面可能同时读取文件进行插入的操作这个时候其中一个已经插入了这边就直接返回就行了
		if (useType == USE_TYPE_SEARCH)
		{
		// 使用内存池释放
		switch (pIndexNode->getType())
		{
		case NODE_TYPE_ONE:
			poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(pIndexNode));
			break;
		case NODE_TYPE_TWO:
			poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(pIndexNode));
			break;
		case NODE_TYPE_THREE:
			poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(pIndexNode));
			break;
		case NODE_TYPE_FOUR:
			poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(pIndexNode));
			break;
		}
			pIndexNode = pair.first->second;
			pIndexNode->increaseRef();
			return true;
		}
		return false;
	}

	//添加了索引缓存的同时也要添加优先级缓存
	IndexIdPreority.insert({ pIndexNode->getPreCmpLen(), indexId });
	pIndexNode->increaseRef();
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
	// 获取锁，确保线程安全
	UniqueLock lock(&rwLock);

	if (indexNodeCache.size() < needReduceNum)
	{
		return false;
	}

	// 收集需要删除的节点，避免在遍历过程中修改容器
	std::vector<IndexNode*> nodesToDelete;
	std::vector<std::multimap<unsigned long long, unsigned long long>::iterator> priorityIterators;

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

		// 只释放引用计数为0的节点
		if (cacheIt->second->isZeroRef())
		{
			nodesToDelete.push_back(cacheIt->second);
			priorityIterators.push_back(curIt);
		}
	}

	// 从缓存中删除（使用索引而不是迭代器，避免迭代器失效）
	for (IndexNode* node : nodesToDelete)
	{
		for (auto cit = indexNodeCache.begin(); cit != indexNodeCache.end(); ++cit)
		{
			if (cit->second == node)
			{
				indexNodeCache.erase(cit);
				break;
			}
		}
	}

	// 从优先级容器中删除
	for (auto priorityIt = priorityIterators.begin(); priorityIt != priorityIterators.end(); ++priorityIt)
	{
		IndexIdPreority.erase(*priorityIt);
	}

	// 释放节点内存（在锁保护下进行，避免竞争）
	for (IndexNode* node : nodesToDelete)
	{
		switch (node->getType())
		{
		case NODE_TYPE_ONE:
			poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(node));
			break;
		case NODE_TYPE_TWO:
			poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(node));
			break;
		case NODE_TYPE_THREE:
			poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(node));
			break;
		case NODE_TYPE_FOUR:
			poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(node));
			break;
		}
	}

	return true;
}

bool Index::reduceCache()
{
	// 使用系统内存比例判断是否需要紧急清理
	float systemMemRate = getSystemMemRate();

	// 紧急清理：当系统内存极低时（< 10%），清空所有缓存和内存池
	// 使用系统内存而非组合内存，对应 getAvailableMemRate 中的重度惩罚阈值
	if (systemMemRate < EMERGENCY_CLEANUP_THRESHOLD)
	{
		UniqueLock lock(&rwLock);

		// 紧急清理时，调用 clearCache 来正确释放节点（包括调用析构函数）
		// 然后调用 clearAllPools 来释放所有内存块
		clearCache();

		// 清空该实例的内存池，释放内存回系统
		poolManager->clearAllPools();

		return true;
	}

	// 使用组合内存比例（系统 + 内存池）判断是否需要部分清理
	float memRate = getAvailableMemRate(*poolManager);

	// 正常情况：内存充足，不需要清理
	if (memRate >= PARTIAL_CLEANUP_THRESHOLD_SEARCH)
	{
		return true;
	}

	// 部分清理：内存有点低（10% - 20%），清理1/5的缓存
	UniqueLock lock(&rwLock);

	unsigned long needReduceNum = indexNodeCache.size() / 5;
	if (needReduceNum == 0)
	{
		return true;
	}

	// 收集需要删除的节点，避免在遍历过程中修改容器
	std::vector<IndexNode*> nodesToDelete;
	std::vector<std::multimap<unsigned long long, unsigned long long>::iterator> priorityIterators;
	std::vector<unsigned long long> indexIdsToRemove;

	auto it = end(IndexIdPreority);
	--it;
	for (unsigned int i = 0; i < needReduceNum; ++i)
	{
		auto curIt = it--;
		auto cacheIt = indexNodeCache.find(curIt->second);
		if (cacheIt == end(indexNodeCache))
		{
			continue;
		}

		//删除之前先看一下是否外面有引用这个节点（使用内存池）
		// 在持有锁的情况下检查引用计数，这是安全的
		if (cacheIt->second->isZeroRef())
		{
			// 收集需要删除的节点（只有引用计数为 0 的节点才能被删除）
			nodesToDelete.push_back(cacheIt->second);
			priorityIterators.push_back(curIt);
			indexIdsToRemove.push_back(curIt->second);
		}
		// 注意：如果节点还有引用，我们不删除它，也不从缓存中移除
		// 这样可以避免重复释放的问题
	}

	// 从缓存中删除（在锁保护下）
	for (unsigned long long indexId : indexIdsToRemove)
	{
		auto cacheIt = indexNodeCache.find(indexId);
		if (cacheIt != end(indexNodeCache))
		{
			indexNodeCache.erase(cacheIt);
		}
	}
	for (auto pit = priorityIterators.begin(); pit != priorityIterators.end(); ++pit)
	{
		IndexIdPreority.erase(*pit);
	}

	// 释放节点内存（仍然在锁保护下，确保没有其他线程能访问这些节点）
	for (IndexNode* node : nodesToDelete)
	{
		switch (node->getType())
		{
		case NODE_TYPE_ONE:
			poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(node));
			break;
		case NODE_TYPE_TWO:
			poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(node));
			break;
		case NODE_TYPE_THREE:
			poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(node));
			break;
		case NODE_TYPE_FOUR:
			poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(node));
			break;
		}
	}

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
	//根据类型创建新的节点（使用内存池）
	IndexNode* pNode = nullptr;
	
	switch (nodeType)
	{
	case NODE_TYPE_ONE:
		pNode = poolManager->getPoolTypeOne().allocate();
		break;
	case NODE_TYPE_TWO:
		pNode = poolManager->getPoolTypeTwo().allocate();
		break;
	case NODE_TYPE_THREE:
		pNode = poolManager->getPoolTypeThree().allocate();
		break;
	case NODE_TYPE_FOUR:
		pNode = poolManager->getPoolTypeFour().allocate();
		break;
	default:
		break;
	}

	if (pNode == nullptr)
	{
		return nullptr;
	}

	//获取新创建的节点的id
	unsigned long long indexId = generator.acquireNumber(1);

	//将新创建的节点插入到缓存当中
	bool ok = indexNodeCache.insert({ indexId, pNode }).second;
	if (!ok)
	{
		// 使用内存池释放
		switch (nodeType)
		{
		case NODE_TYPE_ONE:
			poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(pNode));
			break;
		case NODE_TYPE_TWO:
			poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(pNode));
			break;
		case NODE_TYPE_THREE:
			poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(pNode));
			break;
		case NODE_TYPE_FOUR:
			poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(pNode));
			break;
		}
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

	generator.recycleNumber(indexId, it->second->getGridNum());

	// 使用内存池释放
	IndexNode* node = it->second;
	switch (node->getType())
	{
	case NODE_TYPE_ONE:
		poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(node));
		break;
	case NODE_TYPE_TWO:
		poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(node));
		break;
	case NODE_TYPE_THREE:
		poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(node));
		break;
	case NODE_TYPE_FOUR:
		poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(node));
		break;
	}
	indexNodeCache.erase(it);
	IndexIdPreority.erase(ipIt);
	return true;
}

void Index::clearCache()
{
	// 收集需要删除的节点，避免在遍历过程中修改容器
	std::vector<IndexNode*> nodesToDelete;
	nodesToDelete.reserve(indexNodeCache.size());

	for (auto& value : indexNodeCache)
	{
		nodesToDelete.push_back(value.second);
	}

	// 清空容器
	indexNodeCache.clear();
	IndexIdPreority.clear();

	// 使用内存池释放（在锁保护下进行）
	for (IndexNode* node : nodesToDelete)
	{
		switch (node->getType())
		{
		case NODE_TYPE_ONE:
			poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(node));
			break;
		case NODE_TYPE_TWO:
			poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(node));
			break;
		case NODE_TYPE_THREE:
			poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(node));
			break;
		case NODE_TYPE_FOUR:
			poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(node));
			break;
		}
	}
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

	ShareLock lock(&rwLock);
	if (indexNode->decreaseAndTestZero())
	{
		//已经没有缓存或者是缓存清除从新添加了新的节点就删除
		auto it = indexNodeCache.find(indexNode->getIndexId());
		if (it == end(indexNodeCache) || it->second != indexNode)
		{
			// 使用内存池释放
			switch (indexNode->getType())
			{
			case NODE_TYPE_ONE:
				poolManager->getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(indexNode));
				break;
			case NODE_TYPE_TWO:
				poolManager->getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(indexNode));
				break;
			case NODE_TYPE_THREE:
				poolManager->getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(indexNode));
				break;
			case NODE_TYPE_FOUR:
				poolManager->getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(indexNode));
				break;
			}
		}
	}
	return true;
}

unsigned long long Index::acquireNumber(unsigned char numCount)
{
	return generator.acquireNumber(numCount);
}

void Index::recycleNumber(unsigned long long indexId, unsigned char numCount)
{
	generator.recycleNumber(indexId, numCount);
}

IndexNodePoolManager& Index::getPoolManager()
{
	return *poolManager;
}

Index::~Index()
{
	clearCache();
	delete poolManager;
}

void Index::setInitMaxUniqueNum(unsigned long long initMaxUniqueNum)
{
	generator.setInitMaxUniqueNum(initMaxUniqueNum);
}
