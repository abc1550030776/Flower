#pragma once
#include "Myfile.h"
#include "IndexFile.h"
class BuildIndex
{
	bool init(const char* fileName, Index* index);				//初始化
	bool cutNodeSize(IndexNode* indexNode);			//减少节点的大小使节点总是能保存在比较小的空间里面
	Myfile dstFile;									//需要构建索引的目标文件
	IndexFile indexFile;							//用来处理索引文件和缓存的对象
};