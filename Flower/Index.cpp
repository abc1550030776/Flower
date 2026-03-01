#include "Index.h"
#include "UniqueGenerator.h"
#include "IndexNode.h"
#include "common.h"
#include "MemoryPool.h"

Index::Index()
{
	useType = USE_TYPE_SEARCH;
	externalGenerator = nullptr;
	rwLock.Ptr = 0;
	poolManager = new IndexNodePoolManager();
}

Index::Index(unsigned char useType)
{
	this->useType = useType;
	externalGenerator = nullptr;
	rwLock.Ptr = 0;
	poolManager = new IndexNodePoolManager();
}

Index::Index(unsigned char useType, UniqueGenerator* externalGenerator)
{
	this->useType = useType;
	this->externalGenerator = externalGenerator;
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

	// 优化：使用vector预分配空间，减少内存分配开销
	std::vector<IndexNode*> nodesToDelete;
	nodesToDelete.reserve(needReduceNum);

	// 创建一个vector来存储需要删除的indexId，避免重复查找
	std::vector<unsigned long long> indexIdsToRemove;
	indexIdsToRemove.reserve(needReduceNum);

	auto it = end(IndexIdPreority);

	for (unsigned int i = 0; i < needReduceNum; ++i)
	{
		if (it == begin(IndexIdPreority)) break;
		--it;
		auto cacheIt = indexNodeCache.find(it->second);

		if (cacheIt != end(indexNodeCache) && cacheIt->second->isZeroRef())
		{
			// 收集需要删除的节点和indexId
			nodesToDelete.push_back(cacheIt->second);
			indexIdsToRemove.push_back(it->second);
		}
	}

	// 从缓存中删除（使用indexId直接查找，避免嵌套循环）
	for (unsigned long long indexId : indexIdsToRemove)
	{
		auto cacheIt = indexNodeCache.find(indexId);
		if (cacheIt != end(indexNodeCache))
		{
			indexNodeCache.erase(cacheIt);
		}
	}

	// 从优先级容器中删除（需要重新查找，因为迭代器可能已失效）
	// 优化：直接清理旧的优先级项（可能有一些已经被删除的项）
	// 这是一个简化处理，性能权衡
	auto priorityIt = begin(IndexIdPreority);
	while (priorityIt != end(IndexIdPreority))
	{
		bool found = false;
		for (unsigned long long indexId : indexIdsToRemove)
		{
			if (priorityIt->second == indexId)
			{
				priorityIt = IndexIdPreority.erase(priorityIt);
				found = true;
				break;
			}
		}
		if (!found)
		{
			++priorityIt;
		}
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
	// 使用组合内存比例（系统 + 内存池）判断是否需要清理
	// 注意：getAvailableMemRate() 内部会读取系统内存（有缓存机制）
	float memRate = getAvailableMemRate(*poolManager);

	// 正常情况：内存充足，不需要清理
	if (memRate >= PARTIAL_CLEANUP_THRESHOLD_SEARCH)
	{
		return true;
	}

	// 需要清理时，再获取系统内存比例判断是否需要紧急清理
	// 由于有缓存机制，这次调用会直接返回缓存值，不会产生文件I/O
	float systemMemRate = getSystemMemRate();

	// 紧急清理：当系统内存极低时（< 10%），清空所有缓存和内存池
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

	// 部分清理：内存有点低（10% - 20%），清理1/5的缓存
	UniqueLock lock(&rwLock);

	unsigned long needReduceNum = indexNodeCache.size() / 5;
	if (needReduceNum == 0)
	{
		return true;
	}

	// 优化：使用unordered_map存储待删除的节点，避免重复查找
	// 直接在遍历时删除，因为已经持有锁，可以安全地修改容器
	auto it = end(IndexIdPreority);
	--it;
	unsigned int deletedCount = 0;

	// 创建一个vector来存储需要删除的节点，以便在最后统一释放内存
	std::vector<IndexNode*> nodesToDelete;
	nodesToDelete.reserve(needReduceNum);

	while (deletedCount < needReduceNum && it != begin(IndexIdPreority))
	{
		auto curIt = it--;
		auto cacheIt = indexNodeCache.find(curIt->second);

		if (cacheIt != end(indexNodeCache) && cacheIt->second->isZeroRef())
		{
			// 收集需要删除的节点
			nodesToDelete.push_back(cacheIt->second);

			// 直接从缓存和优先级容器中删除
			indexNodeCache.erase(cacheIt);
			IndexIdPreority.erase(curIt);

			deletedCount++;
		}
	}

	// 释放节点内存（在锁保护下）
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

	//获取新创建的节点的id（通过wrapper方法支持外部共享generator）
	unsigned long long indexId = acquireNumber(1);

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

	recycleNumber(indexId, it->second->getGridNum());

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

bool Index::rekeyNode(unsigned long long oldIndexId, unsigned long long newIndexId)
{
	auto it = indexNodeCache.find(oldIndexId);
	if (it == end(indexNodeCache))
	{
		return false;
	}

	IndexNode* node = it->second;

	//从旧的缓存键中移除
	indexNodeCache.erase(it);

	//插入到新的缓存键
	auto pair = indexNodeCache.insert({ newIndexId, node });
	if (!pair.second)
	{
		//新id已存在，恢复旧entry
		indexNodeCache.insert({ oldIndexId, node });
		return false;
	}

	//更新IndexIdPreority
	unsigned long long preCmpLen = node->getPreCmpLen();
	auto range = IndexIdPreority.equal_range(preCmpLen);
	for (auto ipIt = range.first; ipIt != range.second; ++ipIt)
	{
		if (ipIt->second == oldIndexId)
		{
			IndexIdPreority.erase(ipIt);
			IndexIdPreority.insert({ preCmpLen, newIndexId });
			break;
		}
	}

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
	if (externalGenerator != nullptr)
		return externalGenerator->acquireNumber(numCount);
	return generator.acquireNumber(numCount);
}

void Index::recycleNumber(unsigned long long indexId, unsigned char numCount)
{
	if (externalGenerator != nullptr)
		externalGenerator->recycleNumber(indexId, numCount);
	else
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
	if (externalGenerator != nullptr)
		externalGenerator->setInitMaxUniqueNum(initMaxUniqueNum);
	else
		generator.setInitMaxUniqueNum(initMaxUniqueNum);
}
