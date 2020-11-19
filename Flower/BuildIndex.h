#pragma once
#include "Myfile.h"
#include "IndexFile.h"
class BuildIndex
{
public:
	BuildIndex();
	bool init(const char* fileName, Index* index);				//初始化
	bool cutNodeSize(unsigned long long indexId, IndexNode* indexNode);			//减少节点的大小使节点总是能保存在比较小的空间里面
	IndexNode* getIndexNode(unsigned long long indexId);		//根据某个节点id获取节点
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool mergeNode(unsigned long long preCmpLen, unsigned long long parentId, IndexNodeChild& leftChildNode, const IndexNodeChild& rightChildNode);
	IndexNode* changeNodeType(unsigned long long indexId, IndexNode* indexNode);
	bool build();												//构建文件索引
private:
	Myfile dstFile;									//需要构建索引的目标文件
	IndexFile indexFile;							//用来处理索引文件和缓存的对象
	unsigned long long dstFileSize;					//目标文件的大小
};