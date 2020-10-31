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


}