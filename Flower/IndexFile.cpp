#include "IndexNode.h"
#include "BuildIndex.h"
#include "UniqueGenerator.h"
#include "common.h"

IndexFile::IndexFile()
{
	pIndex = nullptr;
	rootIndexId = 0;
}

bool IndexFile::init(const char* fileName, Index* index)
{
	if (index == nullptr)
	{
		return false;
	}

	bool createIfNExist = true;
	if (index->getUseType() == USE_TYPE_SEARCH)
	{
		createIfNExist = false;
	}
	if (!indexFile.init(fileName, createIfNExist))
	{
		return false;
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
	fpos_t pos;
	pos.__pos = indexId * SIZE_PER_INDEX_FILE_GRID;
	if (!indexFile.read(pos, buffer, 3))
	{
		free(buffer);
		return nullptr;
	}

	//根据不同的节点类型创建节点
	char* p = buffer;
	switch (*((unsigned char*)p))
	{
	case NODE_TYPE_ONE:
		pIndexNode = new IndexNodeTypeOne();
		break;
	case NODE_TYPE_TWO:
		pIndexNode = new IndexNodeTypeTwo();
		break;
	case NODE_TYPE_THREE:
		pIndexNode = new IndexNodeTypeThree();
		break;
	case NODE_TYPE_FOUR:
		pIndexNode = new IndexNodeTypeFour();
		break;
	default:
		free(buffer);
		return nullptr;
		break;
	}
	p++;
	unsigned short len = *((unsigned short*)p);
	p += 2;
	//把剩下的字节给读取出来
	pos.__pos = indexId * SIZE_PER_INDEX_FILE_GRID + 3;
	if (!indexFile.read(pos, &buffer[3], len))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}
	pIndexNode->setGridNum((unsigned char)((len + 2 + SIZE_PER_INDEX_FILE_GRID) / SIZE_PER_INDEX_FILE_GRID));

	//把二进制转成节点的里面的数据
	if (!pIndexNode->toObject(p, len, buildType))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}

	free(buffer);

	pIndexNode->setIndexId(indexId);
	//加载完成了以后加入到索引节点里面
	if (!pIndex->insert(indexId, pIndexNode))
	{
		delete pIndexNode;
		return nullptr;
	}
	//加入到缓存里面了以后再把索引返回
	return pIndexNode;
}

//获取临时节点
IndexNode* IndexFile::getTempIndexNode(unsigned long long indexId)
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
		//当前缓存当中存在的节点所以打一个标记
		tempIndexNodeId.insert(indexId);
		return pIndexNode;
	}

	//从文件当中读取但是不放入缓存

	//从文件当中把数据读取出来
	char* buffer = (char*)malloc(MAX_SIZE_PER_INDEX_NODE);
	if (buffer == nullptr)
	{
		return nullptr;
	}

	//在文件当中的存储位置是用索引id * 4 * 1024来定的,有些存储的存储的比较大会大于4k
	fpos_t pos;
	pos.__pos = indexId * SIZE_PER_INDEX_FILE_GRID;
	if (!indexFile.read(pos, buffer, 3))
	{
		free(buffer);
		return nullptr;
	}

	//根据不同的节点类型创建节点
	char* p = buffer;
	switch (*((unsigned char*)p))
	{
	case NODE_TYPE_ONE:
		pIndexNode = new IndexNodeTypeOne();
		break;
	case NODE_TYPE_TWO:
		pIndexNode = new IndexNodeTypeTwo();
		break;
	case NODE_TYPE_THREE:
		pIndexNode = new IndexNodeTypeThree();
		break;
	case NODE_TYPE_FOUR:
		pIndexNode = new IndexNodeTypeFour();
		break;
	default:
		free(buffer);
		return nullptr;
	}
	p++;
	unsigned short len = *((unsigned short*)p);
	p += 2;
	//把剩下的字节给读取出来
	pos.__pos = indexId * SIZE_PER_INDEX_FILE_GRID + 3;
	if (!indexFile.read(pos, &buffer[3], len))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}
	pIndexNode->setGridNum((unsigned char)((len + 2 + SIZE_PER_INDEX_FILE_GRID) / SIZE_PER_INDEX_FILE_GRID));

	//把二进制转成节点的里面的数据
	if (!pIndexNode->toObject(p, len))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}

	free(buffer);

	pIndexNode->setIndexId(indexId);

	return pIndexNode;
}

//把某个节点写入到文件当中
bool IndexFile::writeFile(unsigned long long indexId, IndexNode* pIndexNode, char writeFileType)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}
	//判断节点是否是已经修改过了的
	if (!pIndexNode->getIsModified())
	{
		return true;
	}

	char* buffer = (char*)malloc(MAX_SIZE_PER_INDEX_NODE);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, MAX_SIZE_PER_INDEX_NODE - 1);
	if (!ok)
	{
		free(buffer);
		return false;
	}
	short len = *((short*)p);
	if ((len + 3) > (pIndexNode->getGridNum() * SIZE_PER_INDEX_FILE_GRID))
	{
		std::vector<unsigned long long> indexIdVec;
		std::vector<IndexNode*> indexNodeVec;
		//节点的大小比本来要存储使用的格子的大小还要大这个时候换一个可以保存相应大小的id
		unsigned long long newIndexId = pIndex->acquireNumber((unsigned char)((len + 2 + SIZE_PER_INDEX_FILE_GRID) / SIZE_PER_INDEX_FILE_GRID));

		//由于节点的id已经改变了所以也要把父节点对应的孩子节点id和孩子节点对应的父节点id修改
		
		//由于这里是临时拿数据的也就是说拿出来了以后要立马放回去的
		unsigned long long parentIndexId = pIndexNode->getParentId();
		if (parentIndexId != 0)
		{
			IndexNode* pTempIndexNode = getTempIndexNode(parentIndexId);
			if (pTempIndexNode == nullptr)
			{
				for (unsigned int i = 0; i < indexIdVec.size(); ++i)
				{
					writeTempFile(indexIdVec[i], indexNodeVec[i]);
				}
				free(buffer);
				return false;
			}

			indexIdVec.push_back(parentIndexId);
			indexNodeVec.push_back(pTempIndexNode);
			
			//修改父节点对应的子节点的id
			if (!pTempIndexNode->changeChildIndexId(indexId, newIndexId))
			{
				for (unsigned int i = 0; i < indexIdVec.size(); ++i)
				{
					writeTempFile(indexIdVec[i], indexNodeVec[i]);
				}
				free(buffer);
				return false;
			}
		}

		//修改所有子节点的父节点id

		//先把所有的子节点id找出来
		std::vector<unsigned long long> childIndexId;
		pIndexNode->getAllChildNodeId(childIndexId);

		//把所有的孩子节点的数据读取出来
		std::vector< IndexNode*> childIndexNode;
		for (auto& value : childIndexId)
		{
			IndexNode* childNode = getTempIndexNode(value);
			if (childNode == nullptr)
			{
				for (unsigned int i = 0; i < indexIdVec.size(); ++i)
				{
					writeTempFile(indexIdVec[i], indexNodeVec[i]);
				}
				free(buffer);
				return false;
			}

			indexIdVec.push_back(value);
			indexNodeVec.push_back(childNode);
			childIndexNode.push_back(childNode);
		}

		//把所有的孩子节点的父节点id改掉
		for (auto& value : childIndexNode)
		{
			value->setParentID(newIndexId);
		}

		//把所有临时打开的文件保存回去
		for (unsigned int i = 0; i < indexIdVec.size(); ++i)
		{
			writeTempFile(indexIdVec[i], indexNodeVec[i]);
		}

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
		indexId = newIndexId;
		pIndexNode->setIndexId(indexId);
	}
	else if ((len + 3) <= ((pIndexNode->getGridNum() - 1) * SIZE_PER_INDEX_FILE_GRID))
	{
		//写入的时候发现只需要更小的存储空间就够了,但是从硬盘里面读出来的时候是超过当前的大小,大于原本大小的部分已经不需要了把一个id回收
		unsigned char newGridNum = (unsigned char)((len + 2 + SIZE_PER_INDEX_FILE_GRID) / SIZE_PER_INDEX_FILE_GRID);
		pIndex->recycleNumber(indexId + newGridNum, (unsigned char)(pIndexNode->getGridNum() - newGridNum));
	}

	//把这个节点的数据写进磁盘里面
	*((unsigned char*)buffer) = pIndexNode->getType();
	fpos_t pos;
	pos.__pos = indexId * SIZE_PER_INDEX_FILE_GRID;
	indexFile.write(pos, buffer, len + 3);

	free(buffer);
	return true;
}

bool IndexFile::writeTempFile(unsigned long long indexId, IndexNode* pIndexNode)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}
	//首先检查下缓存是否已经有了
	auto it = tempIndexNodeId.find(indexId);
	if (it != end(tempIndexNodeId))
	{
		pIndexNode->setIsModified(true);
		tempIndexNodeId.erase(it);
		return true;
	}

	//把数据写入文件当中
	char* buffer = (char*)malloc(MAX_SIZE_PER_INDEX_NODE);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, MAX_SIZE_PER_INDEX_NODE - 1);
	if (!ok)
	{
		free(buffer);
		delete pIndexNode;
		return false;
	}

	//由于读取临时文件的时候只是对里面的id字段进行了改动所以大小是不会有改变的直接存入到文件里面就可以了

	//根据类型填写相应类型的字段
	*((unsigned char*)buffer) = pIndexNode->getType();
	short len = *((short*)p);
	fpos_t pos;
	pos.__pos = indexId * SIZE_PER_INDEX_FILE_GRID;
	indexFile.write(pos, buffer, len + 3);
	
	//写入完成了以后堆内存进行释放
	free(buffer);
	delete pIndexNode;
	return true;
}

//缓存维持一个大小不要太大
bool IndexFile::reduceCache()
{
	if (pIndex == nullptr)
	{
		return false;
	}

	//根据使用的方法减少内存搜索的时候是不需要写如硬盘的所以直接清除缓存就可以了
	if (pIndex->getUseType() == USE_TYPE_SEARCH)
	{
		if (!pIndex->reduceCache())
		{
			return false;
		}
	}
	else
	{
		if (getAvailableMemRate() >= 0.4)
		{
			return true;
		}

		unsigned long needReduceNum = (unsigned long)((double)pIndex->size() * 0.9);

		//把优先级最低的那些节点取出来。
		std::vector<unsigned long long> indexIdVec;
		std::vector<IndexNode*> indexNodeVec;
		indexIdVec.reserve(needReduceNum);
		indexNodeVec.reserve(needReduceNum);

		if (!pIndex->getLastNodes(needReduceNum, indexIdVec, indexNodeVec))
		{
			return false;
		}

		//把所有需要减少的节点全部写盘
		for (unsigned int i = 0; i < needReduceNum; ++i)
		{
			if (!writeFile(indexIdVec[i], indexNodeVec[i]))
			{
				return false;
			}
		}

		//减少索引里面的相应数量的数据
		if (!pIndex->reduceCache(needReduceNum))
		{
			return false;
		}
	}
	return true;
}

bool IndexFile::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	if (pIndex == nullptr)
	{
		return false;
	}

	return pIndex->changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
}

bool IndexFile::swapNode(unsigned long long indexId, IndexNode* newNode)
{
	if (pIndex == nullptr)
	{
		return false;
	}

	if (newNode == nullptr)
	{
		return false;
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
		return false;
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
		fpos_t pos;
		pos.__pos = 0;
		if (!indexFile.read(pos, &rootIndexId, 8))
		{
			return 0;
		}
	}
	return rootIndexId;
}

bool IndexFile::writeEveryCache()																	//把缓存当中的数据全部写盘
{
	if (pIndex == nullptr)
	{
		return false;
	}

	unsigned long size = pIndex->size();

	std::vector<unsigned long long> indexIdVec;
	std::vector<IndexNode*> indexNodeVec;
	indexIdVec.reserve(size);
	indexNodeVec.reserve(size);

	if (!pIndex->getLastNodes(size, indexIdVec, indexNodeVec))
	{
		return false;
	}

	//把所有需要减少的节点全部写盘
	for (unsigned long i = 0; i < size; ++i)
	{
		if (!writeFile(indexIdVec[i], indexNodeVec[i]))
		{
			return false;
		}
	}

	pIndex->clearCache();

	//把根节点的id写入到文件开头
	fpos_t pos;
	pos.__pos = 0;
	if (!indexFile.write(pos, &rootIndexId, 8))
	{
		return false;
	}
	return true;
}

bool IndexFile::putIndexNode(IndexNode* indexNode)
{
	if (pIndex == nullptr)
	{
		return false;
	}

	return pIndex->putIndexNode(indexNode);
}

size_t IndexFile::size()
{
	return pIndex->size();
}

bool IndexFile::writeCacheWithoutRootIndex()
{
	if (pIndex == nullptr)
	{
		return false;
	}

	unsigned long size = pIndex->size();

	std::vector<unsigned long long> indexIdVec;
	std::vector<IndexNode*> indexNodeVec;
	indexIdVec.reserve(size);
	indexNodeVec.reserve(size);

	if (!pIndex->getLastNodes(size, indexIdVec, indexNodeVec))
	{
		return false;
	}

	//把所有需要减少的节点全部写盘
	for (unsigned long i = 0; i < size; ++i)
	{
		if (!writeFile(indexIdVec[i], indexNodeVec[i], WRITE_FILE_CHECK_NEW_ROOT))
		{
			return false;
		}
	}

	pIndex->clearCache();

	//后面创建的节点不会和前面节点有关系所以这里同步一次磁盘减少缓存
	if (!indexFile.sync())
	{
		return false;
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
	fpos_t pos;
	pos.__pos = 0;
	if (!indexFile.write(pos, &size, 8))
	{
		return false;
	}
	pos.__pos = 8;
	if (!indexFile.write(pos, &(rootIndexIds[0]), 8 * size))
	{
		return false;
	}
	return true;
}

unsigned long long IndexFile::getRootIndexIdByOrder(unsigned long rootOrder)
{
	unsigned long long rootIndexId = 0;
	fpos_t pos;
	pos.__pos = (rootOrder + 1) * 8;
	if (!indexFile.read(pos, &rootIndexId, 8))
	{
		return 0;
	}
	return rootIndexId;
}
