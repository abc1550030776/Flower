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

bool BuildIndex::cutNodeSize(IndexNode* indexNode)
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

	//���ݵ�ǰ�����Ͳ鿴ת�����Ժ�ĺ��ӽڵ����������
	unsigned char type = NODE_TYPE_ONE;
	switch (indexNode->getType())
	{
	case NODE_TYPE_ONE:
		type = NODE_TYPE_TWO;
		break;
	case NODE_TYPE_TWO:
		type = NODE_TYPE_THREE;
		break;
	case NODE_TYPE_THREE:
		type = NODE_TYPE_FOUR;
		break;
	case NODE_TYPE_FOUR:
		return false;
	}

	//����ʹ���Ǹ�
}