#pragma once
#include "Myfile.h"
#include "IndexNode.h"
#include "IndexFile.h"
class IndexNode;
class IndexNodeChild;
class Index;
class BuildIndex
{
public:
	BuildIndex();
	bool init(const char* fileName, Index* index, Index* kvIndex = nullptr);				//初始化
	bool cutNodeSize(unsigned long long indexId, IndexNode*& indexNode, unsigned char buildType = BUILD_TYPE_FILE);			//减少节点的大小使节点总是能保存在比较小的空间里面
	IndexNode* getIndexNode(unsigned long long indexId, unsigned char buildType = BUILD_TYPE_FILE);		//根据某个节点id获取节点
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen, unsigned char buildType = BUILD_TYPE_FILE);
	bool mergeNode(unsigned long long preCmpLen, unsigned long long parentId, IndexNodeChild& leftChildNode, const IndexNodeChild& rightChildNode);
	bool addVMergeNode(unsigned long long preCmpLen, unsigned long long parentId, IndexNodeChild& leftChildNode, const IndexNodeChild& rightChildNode);
	IndexNode* changeNodeType(unsigned long long indexId, IndexNode* indexNode, unsigned char buildType = BUILD_TYPE_FILE);
	IndexNode* newKvNode(unsigned char nodeType, unsigned long long preCmpLen);
	bool addKV(unsigned long long key, unsigned long long value);
	bool build(bool needBuildLineIndex = false, char delimiter = '\n');	//构建文件索引
	bool writeKvEveryCache();									//把所有的kv的缓存写入硬盘中
private:
	Myfile dstFile;									//需要构建索引的目标文件
	IndexFile indexFile;							//用来处理索引文件和缓存的对象
	IndexFile kvIndexFile;							//这里用来存储kv存储的索引索引
	unsigned long long dstFileSize;					//目标文件的大小
};
