#include "Index.h"
#include "UniqueGenerator.h"

IndexNode* Index::getIndexNode(unsigned long long indexId)
{
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		return nullptr;
	}

	return it->second;
}

bool Index::insert(unsigned long long indexId, IndexNode* pIndexNode)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}
	bool ok = indexNodeCache.insert({ indexId, pIndexNode }).second;
	if (!ok)
	{
		return false;
	}

	//��������������ͬʱҲҪ������ȼ�����
	IndexIdPreority.insert({ pIndexNode->getPreCmpLen(), indexId });
	return true;
}

unsigned int Index::size()
{
	return indexNodeCache.size();
}

bool Index::getLastNodes(unsigned int num, std::vector<unsigned long long>& indexIdVec, std::vector<IndexNode*>& indexNodeVec)
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

bool Index::reduceCache(unsigned int needReduceNum)
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

		//ɾ��֮ǰ�Ȱ��ڴ������ɾ��
		delete cacheIt->second;
		indexNodeCache.erase(cacheIt);
		IndexIdPreority.erase(curIt);
	}
	return true;
}

bool Index::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	//���ݾɵ�preCmpLen��indexId�����ȼ������ҵ���Ӧ��
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

	//�ӻ��浱�л�ȡ�Ǹ��ڵ�
	auto cacheIt = indexNodeCache.find(indexId);
	if (cacheIt == end(indexNodeCache))
	{
		return false;
	}

	if (cacheIt->second == nullptr)
	{
		return false;
	}

	//�����ȼ�������ɾ�����޸�
	IndexIdPreority.erase(it);
	IndexIdPreority.insert({ newPreCmpLen, indexId });
	//�ѻ��������preCmpId�ĵ�
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
	//�������ʹ����µĽڵ�
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

	//��ȡ�´����Ľڵ��id
	unsigned long long indexId = UniqueGenerator::getUGenerator().acquireNumber();

	//���´����Ľڵ���뵽���浱��
	bool ok = indexNodeCache.insert({ indexId, pNode }).second;
	if (!ok)
	{
		delete pNode;
		return nullptr;
	}

	//��������������ͬʱҲҪ������ȼ�����
	IndexIdPreority.insert({ preCmpLen, indexId });

	//����preCmdLen
	pNode->setPreCmpLen(preCmpLen);

	//����indexId
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
	UniqueGenerator::getUGenerator().recycleNumber(indexId);
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

Index::~Index()
{
	clearCache();
}