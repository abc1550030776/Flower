#include "BuildIndex.h"
#include <memory.h>
#include "common.h"

bool BuildIndex::init(const char* fileName, Index* index)
{
	if (index == nullptr)
	{
		return false;
	}

	char indexFileName[4096];
	memset(indexFileName, 0, sizeof(indexFileName));
	//��ȡ�����ļ�������
	if (!GetIndexPath(fileName, indexFileName))
	{
		return false;
	}

	//��Ŀ���ļ����г�ʼ��
	if (!dstFile.init(fileName))
	{
		return false;
	}
	
	//�������ļ���ʼ����Ҫ����һ�������ļ�
	Myfile tmpIndexFile;
	if (!tmpIndexFile.init(indexFileName))
	{
		return false;
	}

	indexFile.init(tmpIndexFile, index);
	return true;
}

bool BuildIndex::cutNodeSize(unsigned long long indexId, IndexNode* indexNode)
{
	if (indexNode == nullptr)
	{
		return false;
	}
	//�������жϽڵ�Ĵ�С�Ƿ��Ԥ�ƵĻ�Ҫ��
	if (indexNode->getChildrenNum() <= 256)
	{
		return true;
	}

	//�ı�ڵ������ýڵ�ĺ��ӽ��ļ�С���������ӽڵ���ٵ�
	IndexNode* newNode = changeNodeType(indexId, indexNode);

	if (newNode == nullptr)
	{
		return false;
	}

	//�ı��˽ڵ����͵��ǻ����޷��ų���ǰ�ڵ���ܱ�256Ҫ��Ͳ������µĺ��ӽڵ��256Ҫ�����Ե��ýڵ�ĺ����ı�ڵ�
	newNode->cutNodeSize(this, indexId);
	return true;
}

IndexNode* BuildIndex::getIndexNode(unsigned long long indexId)
{
	return indexFile.getIndexNode(indexId);
}

bool BuildIndex::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	return indexFile.changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
}

IndexNode* BuildIndex::changeNodeType(unsigned long long indexId, IndexNode* indexNode)
{
	if (indexNode == nullptr)
	{
		return nullptr;
	}
	
	//ֱ�ӵ��ýڵ�ĺ����ı�ڵ������
	IndexNode* newNode = indexNode->changeType(this);
	if (newNode == nullptr)
	{
		return nullptr;
	}
	
	//�������Ѿ���С�˵Ľڵ��ԭ���Ľڵ㽻��
	if (!indexFile.swapNode(indexId, newNode))
	{
		delete newNode;
		return nullptr;
	}

	//�ɹ������˽ڵ����Ժ󽻻������Ľڵ�Ҫ��ɾ��
	delete indexNode;
	return newNode;
}