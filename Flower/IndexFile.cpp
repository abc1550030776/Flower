#include "IndexFile.h"

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
	//�ж��Ƿ��Ѿ���ʼ��
	if (pIndex == nullptr)
	{
		return nullptr;
	}

	//�ȴӻ��浱�в���Ȼ�󷵻�
	IndexNode* pIndexNode = pIndex->getIndexNode(indexId);
	if (pIndexNode != nullptr)
	{
		return pIndexNode;
	}

	//���ļ����а����ݶ�ȡ����
	char* buffer = (char*)malloc(8 * 1024);

	//���ļ����еĴ洢λ����������id * 4 * 1024������,��Щ�洢�Ĵ洢�ıȽϴ�����4k
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.read(pos, buffer, 8 * 1024);

	//���ݲ�ͬ�Ľڵ����ʹ����ڵ�
	char* p = buffer;
	switch (*p)
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
		break;
	}
	if 
	p++;
	unsigned short len = *((unsigned short*)p);
	p += 2;

}