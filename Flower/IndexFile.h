#pragma once
#include "Myfile.h"
#include "IndexNode.h"
#include "Index.h"
#include <unordered_set>
#include <vector>

const char WRITE_FILE_CHECK_EVERY_ROOT = 0;
const char WRITE_FILE_CHECK_NEW_ROOT = 1;

class Index;
class IndexNode;
class IndexFile
{
public:
	IndexFile();
	bool init(const char* fileName, Index* index);

	IndexNode* getIndexNode(unsigned long long indexId, unsigned char buildType = BUILD_TYPE_FILE);
	IndexNode* getTempIndexNode(unsigned long long indexId);
	bool writeFile(unsigned long long indexId, IndexNode* pIndexNode, char writeFileType = WRITE_FILE_CHECK_EVERY_ROOT);
	bool writeTempFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool reduceCache();
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//创建新的节点
	bool deleteIndexNode(unsigned long long indexId);										//删除节点
	void setRootIndexId(unsigned long long rootIndexId);									//设置根节点id
	unsigned long long getRootIndexId();													//获取根节点id
	bool writeEveryCache();																	//把缓存当中的数据全部写盘
	bool putIndexNode(IndexNode* indexNode);												//外部使用完了告诉说外部已经不再引用
	size_t size();																			//返回内存中索引的数量
	bool writeCacheWithoutRootIndex();														//把所有的缓存写入文件当中但是不处理根节点部分
	void pushRootIndexId(unsigned long long rootIndexId);									//已经把一块和并完成了记录这个根节点
	void setInitMaxUniqueNum(unsigned long long initMaxUniqueNum);							//设置生成器初始值
	bool writeEveryRootIndexId();															//把所有的rootIndexId写入文件当中
	unsigned long long getRootIndexIdByOrder(unsigned long rootOrder);						//根据根节点次序获取根节点id
private:
	Myfile indexFile;
	Index* pIndex;
	std::unordered_set<unsigned long long> tempIndexNodeId;
	unsigned long long rootIndexId;
	std::vector<unsigned long long> rootIndexIds;											//为了加快构建速度现在把一个文件分成一块一块每一块一个rootIndexId
};
