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