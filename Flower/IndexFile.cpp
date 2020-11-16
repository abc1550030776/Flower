#include "IndexFile.h"
#include "UniqueGenerator.h"

IndexFile::IndexFile()
{
	pIndex = nullptr;
}

void IndexFile::init(const Myfile& file, Index* index)
{
	indexFile = file;
	pIndex = index;
}

IndexNode* IndexFile::getIndexNode(unsigned long long indexId)
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
	char* buffer = (char*)malloc(8 * 1024);

	//在文件当中的存储位置是用索引id * 4 * 1024来定的,有些存储的存储的比较大会大于4k
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.read(pos, buffer, 8 * 1024);

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
	if ((len + 3) > 4 * 1024)
	{
		pIndexNode->setIsBig(true);
	}

	//把二进制转成节点的里面的数据
	if (!pIndexNode->toObject(p, len))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}

	free(buffer);

	//加载完成了以后加入到索引节点里面
	if (!pIndex->insert(indexId, pIndexNode))
	{
		delete pIndexNode;
		return nullptr;
	}

	pIndexNode->setIndexId(indexId);
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
	char* buffer = (char*)malloc(8 * 1024);

	//在文件当中的存储位置是用索引id * 4 * 1024来定的,有些存储的存储的比较大会大于4k
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.read(pos, buffer, 8 * 1024);

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
	if ((len + 3) > 4 * 1024)
	{
		pIndexNode->setIsBig(true);
	}

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
bool IndexFile::writeFile(unsigned long long indexId, IndexNode* pIndexNode)
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

	char* buffer = (char*)malloc(8 * 1024);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, 8 * 1024 - 1);
	if (!ok)
	{
		free(buffer);
		return false;
	}
	short len = *((short*)p);
	if ((len + 3) > 4 * 1024 && !pIndexNode->getIsBig())
	{
		std::vector<unsigned long long> indexIdVec;
		std::vector<IndexNode*> indexNodeVec;
		//节点的大小比默认的4k的大小还要大这个时候换一个可以保存8k大小的id
		unsigned long long newIndexId = UniqueGenerator::getUGenerator().acquireTwoNumber();

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

		//父节点还有所有的孩子节点的父节点id都改变了以后这个节点就是用新节点id了。
		//创建了新的节点的id所以旧的节点的id就无效了放回去
		UniqueGenerator::getUGenerator().recycleNumber(indexId);
		indexId = newIndexId;
		pIndexNode->setIndexId(indexId);
	}

	//把这个节点的数据写进磁盘里面
	*((unsigned char*)buffer) = pIndexNode->getType();
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
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
	char* buffer = (char*)malloc(8 * 1024);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, 8 * 1024 - 1);
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
	pos.__pos = indexId * 4 * 1024;
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

	unsigned size = pIndex->size();
	if (size <= 1024)
	{
		return true;
	}

	unsigned int needReduceNum = size - 1024;

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