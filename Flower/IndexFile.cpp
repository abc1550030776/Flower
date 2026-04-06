#include "IndexNode.h"
#include <cstdlib>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include "BuildIndex.h"
#include "UniqueGenerator.h"
#include "common.h"
#include "MemoryPool.h"

IndexFile::IndexFile() : pBuildIndex(nullptr), buildType(0), deferRootWrite(false)
{
	pIndex = nullptr;
	rootIndexId = 0;
}

void IndexFile::setBuildIndex(BuildIndex* buildIndex, unsigned char type)
{
	pBuildIndex = buildIndex;
	buildType = type;
}

void IndexFile::setDeferRootWrite(bool deferRootWrite)
{
	this->deferRootWrite = deferRootWrite;
}

bool IndexFile::init(const char* fileName, Index* index)
{
	if (index == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	bool createIfNExist = true;
	if (index->getUseType() == USE_TYPE_SEARCH)
	{
		createIfNExist = false;
	}
	if (!indexFile.init(fileName, createIfNExist))
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	pIndex = index;
	return true;
}

IndexNode* IndexFile::getIndexNode(unsigned long long indexId, unsigned char buildType)
{
	//判断是否已经初始化
	if (pIndex == nullptr)
	{
		return nullptr;
	}

	//先从缓存当中查找然后返回
	IndexNode* pIndexNode = pIndex->getIndexNode(indexId);
	if (pIndexNode != nullptr)
	{
		return pIndexNode;
	}

	//从文件当中把数据读取出来
	char* buffer = (char*)malloc(MAX_SIZE_PER_INDEX_NODE);
	if (buffer == nullptr)
	{
		return nullptr;
	}

	//在文件当中的存储位置是用索引id * 4 * 1024来定的,有些存储的存储的比较大会大于4k
	unsigned long long pos;
	pos = indexId * SIZE_PER_INDEX_FILE_GRID;
	if (!indexFile.read(pos, buffer, 3))
	{
		free(buffer);
		return nullptr;
	}

	//根据不同的节点类型创建节点（使用内存池）
	IndexNodePoolManager& poolManager = pIndex->getPoolManager();
	char* p = buffer;
	switch (*((unsigned char*)p))
	{
	case NODE_TYPE_ONE:
		pIndexNode = poolManager.getPoolTypeOne().allocate();
		break;
	case NODE_TYPE_TWO:
		pIndexNode = poolManager.getPoolTypeTwo().allocate();
		break;
	case NODE_TYPE_THREE:
		pIndexNode = poolManager.getPoolTypeThree().allocate();
		break;
	case NODE_TYPE_FOUR:
		pIndexNode = poolManager.getPoolTypeFour().allocate();
		break;
	default:
		free(buffer);
		return nullptr;
		break;
	}
	p++;
	unsigned short len = *((unsigned short*)p);
	p += 2;

	if (len > MAX_SIZE_PER_INDEX_NODE - 3)
	{
		// 使用内存池释放
		switch (*((unsigned char*)buffer))
		{
		case NODE_TYPE_ONE:
			poolManager.getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(pIndexNode));
			break;
		case NODE_TYPE_TWO:
			poolManager.getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(pIndexNode));
			break;
		case NODE_TYPE_THREE:
			poolManager.getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(pIndexNode));
			break;
		case NODE_TYPE_FOUR:
			poolManager.getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(pIndexNode));
			break;
		}
		free(buffer);
		return nullptr;
	}

	//把剩下的字节给读取出来
	pos = indexId * SIZE_PER_INDEX_FILE_GRID + 3;
	if (!indexFile.read(pos, &buffer[3], len))
	{
		// 使用内存池释放
		switch (*((unsigned char*)buffer))
		{
		case NODE_TYPE_ONE:
			poolManager.getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(pIndexNode));
			break;
		case NODE_TYPE_TWO:
			poolManager.getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(pIndexNode));
			break;
		case NODE_TYPE_THREE:
			poolManager.getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(pIndexNode));
			break;
		case NODE_TYPE_FOUR:
			poolManager.getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(pIndexNode));
			break;
		}
		free(buffer);
		return nullptr;
	}
pIndexNode->setGridNum((unsigned char)((len + 3 + SIZE_PER_INDEX_FILE_GRID - 1) / SIZE_PER_INDEX_FILE_GRID));

	//把二进制转成节点的里面的数据
	if (!pIndexNode->toObject(p, len, buildType))
	{
		// 使用内存池释放
		switch (*((unsigned char*)buffer))
		{
		case NODE_TYPE_ONE:
			poolManager.getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(pIndexNode));
			break;
		case NODE_TYPE_TWO:
			poolManager.getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(pIndexNode));
			break;
		case NODE_TYPE_THREE:
			poolManager.getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(pIndexNode));
			break;
		case NODE_TYPE_FOUR:
			poolManager.getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(pIndexNode));
			break;
		}
		free(buffer);
		return nullptr;
	}

	free(buffer);

	pIndexNode->setIndexId(indexId);
	//加载完成了以后加入到索引节点里面
	if (!pIndex->insert(indexId, pIndexNode))
	{
		// insert失败，需要释放节点内存（在非搜索模式下pIndex->insert不会自动释放）
		switch (*((unsigned char*)buffer))
		{
		case NODE_TYPE_ONE:
			poolManager.getPoolTypeOne().deallocate(static_cast<IndexNodeTypeOne*>(pIndexNode));
			break;
		case NODE_TYPE_TWO:
			poolManager.getPoolTypeTwo().deallocate(static_cast<IndexNodeTypeTwo*>(pIndexNode));
			break;
		case NODE_TYPE_THREE:
			poolManager.getPoolTypeThree().deallocate(static_cast<IndexNodeTypeThree*>(pIndexNode));
			break;
		case NODE_TYPE_FOUR:
			poolManager.getPoolTypeFour().deallocate(static_cast<IndexNodeTypeFour*>(pIndexNode));
			break;
		}
		return nullptr;
	}
	//加入到缓存里面了以后再把索引返回
	return pIndexNode;
}

bool IndexFile::prepareForWrite(unsigned long long& indexId, IndexNode*& pIndexNode, char writeFileType)
{
	if (pBuildIndex != nullptr)
	{
		if (!pBuildIndex->cutNodeSize(indexId, pIndexNode, buildType))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
	}
	if (pIndexNode == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	size_t payloadSize = pIndexNode->getExactPayloadSize();
	size_t onDiskSize = payloadSize + 3;
	if (onDiskSize <= pIndexNode->getGridNum() * SIZE_PER_INDEX_FILE_GRID)
	{
		//写入的时候发现只需要更小的存储空间就够了,多余的格子回收
		if (onDiskSize <= (pIndexNode->getGridNum() - 1) * SIZE_PER_INDEX_FILE_GRID)
		{
			unsigned char newGridNum = (unsigned char)((onDiskSize + SIZE_PER_INDEX_FILE_GRID - 1) / SIZE_PER_INDEX_FILE_GRID);
			pIndex->recycleNumber(indexId + newGridNum, (unsigned char)(pIndexNode->getGridNum() - newGridNum));
			pIndexNode->setGridNum(newGridNum);
		}
		return true;
	}

	unsigned char requiredGridNum = (unsigned char)((onDiskSize + SIZE_PER_INDEX_FILE_GRID - 1) / SIZE_PER_INDEX_FILE_GRID);
	unsigned long long newIndexId = pIndex->acquireNumber(requiredGridNum);
	std::vector<IndexNode*> acquiredNodes;
	auto releaseAcquiredNodes = [&]() {
		for (IndexNode* node : acquiredNodes)
		{
			if (node != nullptr)
			{
				putIndexNode(node);
			}
		}
		acquiredNodes.clear();
	};

	//由于节点的id已经改变了所以也要把父节点对应的孩子节点id和孩子节点对应的父节点id修改
	unsigned long long parentIndexId = pIndexNode->getParentId();
	if (parentIndexId != 0)
	{
		IndexNode* parentNode = getIndexNode(parentIndexId, buildType);
		if (parentNode == nullptr)
		{
			releaseAcquiredNodes();
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}

		acquiredNodes.push_back(parentNode);

		//修改父节点对应的子节点的id
		if (!parentNode->changeChildIndexId(indexId, newIndexId))
		{
			printf("Failed to changeChildIndexId! parentId=%llu, child_to_find=%llu, new_child=%llu\n", parentIndexId, indexId, newIndexId);
			// Print all children of the parent to see what it actually has
			std::vector<unsigned long long> childIds;
			parentNode->getAllChildNodeId(childIds);
			printf("Parent has %lu child nodes: ", childIds.size());
			for (auto cid : childIds) { printf("%llu ", cid); }
			printf("\n");
			printf("Parent node type=%d, len=%llu, start=%llu\n", parentNode->getType(), parentNode->getLen(), parentNode->getStart());

			releaseAcquiredNodes();
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
		parentNode->setIsModified(true);
	}

	//修改所有子节点的父节点id
	std::vector<unsigned long long> childIndexId;
	pIndexNode->getAllChildNodeId(childIndexId);

	//把所有的孩子节点的数据读取出来
	std::vector<IndexNode*> childIndexNode;
	for (auto& value : childIndexId)
	{
		IndexNode* childNode = getIndexNode(value, buildType);
		if (childNode == nullptr)
		{
			releaseAcquiredNodes();
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}

		acquiredNodes.push_back(childNode);
		childIndexNode.push_back(childNode);
	}

	//把所有的孩子节点的父节点id改掉
	for (auto& value : childIndexNode)
	{
		value->setParentID(newIndexId);
		value->setIsModified(true);
	}

	releaseAcquiredNodes();

	//写文件的时候改变了节点的id,可能改变的是根节点的id这个时候把根节点id也改掉
	if (writeFileType == WRITE_FILE_CHECK_EVERY_ROOT)
	{
		if (rootIndexId == indexId)
		{
			rootIndexId = newIndexId;
		}
		else
		{
			if (!rootIndexIds.empty())
			{
				for (unsigned long i = 0; i < rootIndexIds.size(); ++i)
				{
					if (rootIndexIds[i] == indexId)
					{
						rootIndexIds[i] = newIndexId;
						break;
					}
				}
			}
		}
	}
	else
	{
		//构建文件索引的时候把文件分成一块一块,构建完一块生成新的根节点先放到节点列表的最后面然后再写入所以最后那个节点是最新的
		if (!rootIndexIds.empty())
		{
			if (rootIndexIds.back() == indexId)
			{
				rootIndexIds.back() = newIndexId;
			}
		}
	}

	//父节点还有所有的孩子节点的父节点id都改变了以后这个节点就是用新节点id了。
	//创建了新的节点的id所以旧的节点的id就无效了放回去
	pIndex->recycleNumber(indexId, pIndexNode->getGridNum());

	//更新缓存中的键：旧id -> 新id
	pIndex->rekeyNode(indexId, newIndexId);

	indexId = newIndexId;
	pIndexNode->setIndexId(indexId);
	pIndexNode->setGridNum(requiredGridNum);
	pIndexNode->setIsModified(true);

	return true;
}

bool IndexFile::flushNodeToDisk(unsigned long long indexId, IndexNode* pIndexNode)
{
	char* buffer = (char*)malloc(MAX_SIZE_PER_INDEX_NODE);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, MAX_SIZE_PER_INDEX_NODE - 1);
	if (!ok)
	{
		free(buffer);
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	short len = *((short*)p);

	*((unsigned char*)buffer) = pIndexNode->getType();
	unsigned long long pos;
	pos = indexId * SIZE_PER_INDEX_FILE_GRID;
	if (!indexFile.write(pos, buffer, len + 3))
	{
		free(buffer);
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	free(buffer);
	pIndexNode->setIsModified(false);
	return true;
}

//把某个节点写入到文件当中
bool IndexFile::writeFile(unsigned long long& indexId, IndexNode* pIndexNode, char writeFileType)
{
	if (!prepareForWrite(indexId, pIndexNode, writeFileType))
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	if (!pIndexNode->getIsModified())
	{
		return true;
	}

	return flushNodeToDisk(indexId, pIndexNode);
}

//缓存维持一个大小不要太大
bool IndexFile::reduceCache()
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	//根据使用的方法减少内存搜索的时候是不需要写如硬盘的所以直接清除缓存就可以了
	if (pIndex->getUseType() == USE_TYPE_SEARCH)
	{
		if (!pIndex->reduceCache())
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
	}
	else
	{
		// BUILD模式：使用简单判断，如果内存充足直接返回
		// 这与backup分支保持一致，避免不必要的复杂计算和写盘操作
		if (getAvailableMemRate(pIndex->getPoolManager()) >= PARTIAL_CLEANUP_THRESHOLD_BUILD)
		{
			return true;
		}

		// BUILD模式：内存不足时，清理70%的缓存
		// 使用系统内存比例判断是否需要紧急清理
		float systemMemRate = getSystemMemRate();
		
		// 紧急清理：当系统内存极低时（< 10%），写盘后清空所有缓存和内存池
		// 使用系统内存而非组合内存，对应 getAvailableMemRate 中的重度惩罚阈值
		if (systemMemRate < EMERGENCY_CLEANUP_THRESHOLD)
		{
		// 先把所有缓存写盘
		if (!writeEveryCache())
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
		
		// 清空该实例的内存池，释放内存回系统
		pIndex->getPoolManager().clearAllPools();
			
			return true;
		}
		
		// 使用组合内存比例（系统 + 内存池）判断是否需要部分清理
		float memRate = getAvailableMemRate(pIndex->getPoolManager());
		
		// 正常情况：内存充足，不需要清理
		if (memRate >= PARTIAL_CLEANUP_THRESHOLD_BUILD)
		{
			return true;
		}

		// 部分清理：内存有点低（10% - 40%），清理70%的缓存
		unsigned long needReduceNum = (unsigned long)((double)pIndex->size() * PARTIAL_CLEANUP_RATIO_BUILD);

		//逐个从末尾取最低优先级节点，写盘后驱逐，每次迭代基于当前缓存状态
		unsigned long long indexId;
		IndexNode* pIndexNode;
		unsigned long count = 0;
		while (count < needReduceNum && pIndex->getLastNodeIdAndNode(indexId, pIndexNode))
		{
			if (pIndexNode->getIsModified())
			{
				if (!writeFile(indexId, pIndexNode))
				{
					printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
				}
			}

			pIndex->evictIndexNode(indexId);
			++count;
		}
	}
	return true;
}

bool IndexFile::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	return pIndex->changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
}

bool IndexFile::swapNode(unsigned long long indexId, IndexNode* newNode)
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	if (newNode == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	return pIndex->swapNode(indexId, newNode);
}

IndexNode* IndexFile::newIndexNode(unsigned char nodeType, unsigned long long preCmpLen)
{
	if (pIndex == nullptr)
	{
		return nullptr;
	}

	return pIndex->newIndexNode(nodeType, preCmpLen);
}

bool IndexFile::deleteIndexNode(unsigned long long indexId)
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	return pIndex->deleteIndexNode(indexId);
}

void IndexFile::setRootIndexId(unsigned long long rootIndexId)
{
	this->rootIndexId = rootIndexId;
}

unsigned long long IndexFile::getRootIndexId()
{
	//刚打开文件根节点没读进来
	if (rootIndexId == 0 && pIndex->getUseType() == USE_TYPE_SEARCH)
	{
		unsigned long long pos;
		pos = 0;
		if (!indexFile.read(pos, &rootIndexId, 8))
		{
			return 0;
		}
	}
	return rootIndexId;
}

bool IndexFile::writeEveryCache()
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	std::vector<unsigned long long> batchIds;
	unsigned long long currentPreCmpLen = 0;
	unsigned long long cursor = 0;
	std::unordered_set<unsigned long long> modifiedIds;
	bool isKv = (buildType == BUILD_TYPE_KV);

	while (pIndex->getModifiedNodeIdsWithSamePreCmpLen(batchIds, currentPreCmpLen, cursor))
	{
		cursor = currentPreCmpLen + 1;

		std::vector<unsigned long long> processedIds;
		processedIds.reserve(batchIds.size());
		std::unordered_set<unsigned long long> parentsToEvict;
		std::unordered_set<unsigned long long> leavesToEvict;
		for (unsigned long long indexId : batchIds)
		{
			IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
			if (pIndexNode != nullptr && pIndexNode->getIsModified())
			{
				unsigned long long originalIndexId = indexId;
				if (!prepareForWrite(indexId, pIndexNode, WRITE_FILE_CHECK_EVERY_ROOT))
				{
					printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
				}
				modifiedIds.insert(indexId);
				if (indexId != originalIndexId)
				{
					unsigned long long parentId = pIndexNode->getParentId();
					if (parentId != 0)
					{
						modifiedIds.insert(parentId);
					}
				}
			}
			processedIds.push_back(indexId);
		}

		if (isKv)
		{
			std::vector<unsigned long long> childNodeIds;
			for (unsigned long long indexId : processedIds)
			{
				IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
				if (pIndexNode == nullptr) continue;
				unsigned long long parentId = pIndexNode->getParentId();
				if (parentId != 0)
				{
					parentsToEvict.insert(parentId);
				}
				childNodeIds.clear();
				pIndexNode->getAllChildNodeId(childNodeIds);
				if (childNodeIds.empty())
				{
					leavesToEvict.insert(indexId);
				}
			}

			std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>> evictGroups;
			auto queueEvict = [&](unsigned long long indexId) -> bool {
				IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
				if (pIndexNode == nullptr) return true;
				auto& bucket = evictGroups[pIndexNode->getPreCmpLen()];
				if (!bucket.insert(indexId).second)
				{
					return true;
				}
				if (pIndexNode->getIsModified())
				{
					if (!flushNodeToDisk(indexId, pIndexNode))
					{
						printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
					}
				}
				return true;
			};

			for (unsigned long long parentId : parentsToEvict)
			{
				if (!queueEvict(parentId))
				{
					return false;
				}
			}

			for (unsigned long long indexId : leavesToEvict)
			{
				if (!queueEvict(indexId))
				{
					return false;
				}
			}

			for (auto& entry : evictGroups)
			{
				if (!pIndex->evictIndexNodesWithSamePreCmpLen(entry.first, entry.second))
				{
					printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
				}
				for (auto indexId : entry.second)
				{
					modifiedIds.erase(indexId);
				}
			}
		}
	}

	for (unsigned long long indexId : modifiedIds)
	{
		IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
		if (pIndexNode != nullptr && pIndexNode->getIsModified())
		{
			if (!flushNodeToDisk(indexId, pIndexNode))
			{
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}
	}

	pIndex->clearCache();

	if (!deferRootWrite)
	{
		unsigned long long pos;
		pos = 0;
		if (!indexFile.write(pos, &rootIndexId, 8))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
	}
	if (!indexFile.sync())
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	return true;
}

bool IndexFile::putIndexNode(IndexNode* indexNode)
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	return pIndex->putIndexNode(indexNode);
}

Index* IndexFile::getIndex()
{
	return pIndex;
}

size_t IndexFile::size()
{
	return pIndex->size();
}

bool IndexFile::writeCacheWithoutRootIndex()
{
	if (pIndex == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	std::vector<unsigned long long> batchIds;
	unsigned long long currentPreCmpLen = 0;
	unsigned long long cursor = 0;
	std::unordered_set<unsigned long long> modifiedIds;
	bool isKv = (buildType == BUILD_TYPE_KV);

	while (pIndex->getModifiedNodeIdsWithSamePreCmpLen(batchIds, currentPreCmpLen, cursor))
	{
		cursor = currentPreCmpLen + 1;

		std::vector<unsigned long long> processedIds;
		processedIds.reserve(batchIds.size());
		std::unordered_set<unsigned long long> parentsToEvict;
		std::unordered_set<unsigned long long> leavesToEvict;
		for (unsigned long long indexId : batchIds)
		{
			IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
			if (pIndexNode != nullptr && pIndexNode->getIsModified())
			{
				unsigned long long originalIndexId = indexId;
				if (!prepareForWrite(indexId, pIndexNode, WRITE_FILE_CHECK_NEW_ROOT))
				{
					printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
				}
				modifiedIds.insert(indexId);
				if (indexId != originalIndexId)
				{
					unsigned long long parentId = pIndexNode->getParentId();
					if (parentId != 0)
					{
						modifiedIds.insert(parentId);
					}
				}
			}
			processedIds.push_back(indexId);
		}

		if (isKv)
		{
			std::vector<unsigned long long> childNodeIds;
			for (unsigned long long indexId : processedIds)
			{
				IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
				if (pIndexNode == nullptr) continue;
				unsigned long long parentId = pIndexNode->getParentId();
				if (parentId != 0)
				{
					parentsToEvict.insert(parentId);
				}
				childNodeIds.clear();
				pIndexNode->getAllChildNodeId(childNodeIds);
				if (childNodeIds.empty())
				{
					leavesToEvict.insert(indexId);
				}
			}

			std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>> evictGroups;
			auto queueEvict = [&](unsigned long long indexId) -> bool {
				IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
				if (pIndexNode == nullptr) return true;
				auto& bucket = evictGroups[pIndexNode->getPreCmpLen()];
				if (!bucket.insert(indexId).second)
				{
					return true;
				}
				if (pIndexNode->getIsModified())
				{
					if (!flushNodeToDisk(indexId, pIndexNode))
					{
						printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
					}
				}
				return true;
			};

			for (unsigned long long parentId : parentsToEvict)
			{
				if (!queueEvict(parentId))
				{
					return false;
				}
			}

			for (unsigned long long indexId : leavesToEvict)
			{
				if (!queueEvict(indexId))
				{
					return false;
				}
			}

			for (auto& entry : evictGroups)
			{
				if (!pIndex->evictIndexNodesWithSamePreCmpLen(entry.first, entry.second))
				{
					printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
				}
				for (auto indexId : entry.second)
				{
					modifiedIds.erase(indexId);
				}
			}
		}
	}

	for (unsigned long long indexId : modifiedIds)
	{
		IndexNode* pIndexNode = pIndex->getCacheNode(indexId);
		if (pIndexNode != nullptr && pIndexNode->getIsModified())
		{
			if (!flushNodeToDisk(indexId, pIndexNode))
			{
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}
	}

	pIndex->clearCache();

	if (!indexFile.sync())
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	return true;
}

void IndexFile::pushRootIndexId(unsigned long long rootIndexId)
{
	rootIndexIds.push_back(rootIndexId);
}

void IndexFile::setInitMaxUniqueNum(unsigned long long initMaxUniqueNum)
{
	if (pIndex == nullptr)
	{
		return;
	}
	pIndex->setInitMaxUniqueNum(initMaxUniqueNum);
}

bool IndexFile::writeEveryRootIndexId()
{
	unsigned long long size = rootIndexIds.size();
	//先把那个根节点的id的数量写入文件当中
	unsigned long long pos;
	pos = 0;
	if (!indexFile.write(pos, &size, 8))
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	if (size > 0) {
		pos = 8;
		if (!indexFile.write(pos, &(rootIndexIds[0]), 8 * size))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
	}
	return true;
}

const std::vector<unsigned long long>& IndexFile::getRootIndexIds() const
{
	return rootIndexIds;
}

unsigned long long IndexFile::getRootIndexIdByOrder(unsigned long rootOrder)
{
	unsigned long long rootIndexId = 0;
	unsigned long long pos;
	pos = (rootOrder + 1) * 8;
	if (!indexFile.read(pos, &rootIndexId, 8))
	{
		return 0;
	}
	return rootIndexId;
}
