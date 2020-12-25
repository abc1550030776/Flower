#pragma once
#include "SetWithLock.h"
#include "Myfile.h"
#include "BuildIndex.h"
class SearchIndex
{
public:
	SearchIndex();
	bool init(const char* searchTarget, unsigned int targetLen, SetWithLock* resultSet, const char* fileName, Index* index, unsigned char skipCharNum, unsigned long rootOrder);
	bool search();																				//文件当中的目标数据
private:
	const char* searchTarget;
	unsigned int targetLen;
	SetWithLock* resultSet;
	Myfile dstFile;
	IndexFile indexFile;
	unsigned long long dstFileSize;					//目标文件的大小
	unsigned char skipCharNum;						//搜索前需要跳过的字符数量
	unsigned long long rootIndexId;					//根节点id
};
