#include "Index.h"

IndexNode* Index::getIndexNode(unsigned long long indexId)
{
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		return NULL;
	}

	return it->second;
}

bool Index::insert(unsigned long long indexId, IndexNode* pIndexNode)
{
	if (pIndexNode == NULL)
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
		indexNodeCache.erase(curIt->second);
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