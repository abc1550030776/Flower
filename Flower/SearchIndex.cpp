#include "SearchIndex.h"
#include "memory.h"
#include "common.h"
#include <sys/stat.h>

SearchIndex::SearchIndex()
{
	searchTarget = nullptr;
	targetLen = 0;
	resultSet = 0;
	dstFileSize = 0;
}

bool SearchIndex::init(const char* searchTarget, unsigned int targetLen, SetWithLock* resultSet, const char* fileName, Index* index, unsigned char skipCharNum)
{
	if (index == nullptr)
	{
		return false;
	}

	if (searchTarget == nullptr)
	{
		return false;
	}

	if (fileName == nullptr)
	{
		return false;
	}

	if (resultSet == nullptr)
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

	if (!dstFile.init(fileName, false))
	{
		return false;
	}

	if (!indexFile.init(indexFileName, index))
	{
		return false;
	}

	this->searchTarget = searchTarget;
	this->targetLen = targetLen;
	this->resultSet = resultSet;

	//读取目标文件的大小
	struct stat statbuf;
	stat(fileName, &statbuf);
	dstFileSize = statbuf.st_size;
	this->skipCharNum = skipCharNum;
	return true;
}

bool SearchIndex::search()
{
	return true;
}