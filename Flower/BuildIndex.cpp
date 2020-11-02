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
	//获取索引文件的名字
	if (!GetIndexPath(fileName, indexFileName))
	{
		return false;
	}

	//对目标文件进行初始化
	if (!dstFile.init(fileName))
	{
		return false;
	}
	
	//对索引文件初始化先要创建一个索引文件
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
	//首先先判断节点的大小是否比预计的还要大
	if (indexNode->getChildrenNum() <= 256)
	{
		return true;
	}

	//根据当前的类型查看转化了以后的孩子节点的索引长度
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

	//这里使用那个
}