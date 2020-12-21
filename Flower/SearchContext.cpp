#include "SearchContext.h"
#include "Index.h"
#include "string.h"
#include "SetWithLock.h"
#include "SearchIndex.h"

SearchContext::SearchContext()
{
	index = nullptr;
	dstFileName = nullptr;
}

bool SearchContext::init(const char* fileName)
{
	if (fileName == nullptr)
	{
		return false;
	}
	index = new Index();
	unsigned long strLen = strlen(fileName);
	dstFileName = new char[strLen + 1];
	strcpy(dstFileName, fileName);
	return true;
}

static void* ThreadFun(void* arg)
{
	SearchIndex* searchIndex = (SearchIndex*)arg;
	return (void*)searchIndex->search();
}

bool SearchContext::search(const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set)
{
	if (dstFileName == nullptr)
	{
		return false;
	}

	if (index == nullptr)
	{
		return false;
	}

	if (searchTarget == nullptr)
	{
		return false;
	}

	if (set == nullptr)
	{
		return false;
	}
	//这里使用多线程搜索
	SetWithLock* resultSet = new SetWithLock(set);

	SearchIndex searchIndex[8];
	pthread_t pids[8];
	for (unsigned char i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
	{
		searchIndex[i].init(searchTarget, targetLen, resultSet, dstFileName, index, i);
		if (pthread_create(&pids[i], NULL, ThreadFun, &searchIndex[i]) != 0)
		{
			for (unsigned int j = 0; j < i; ++j)
			{
				pthread_join(pids[j], NULL);
			}

			delete resultSet;
			return false;
		}
	}

	bool success = true;
	//等待线程的退出
	for (unsigned int i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
	{
		bool ret = false;
		pthread_join(pids[i], (void**)&ret);
		if (!ret)
		{
			success = false;
		}
	}
	delete resultSet;
	return success;
}

SearchContext::~SearchContext()
{
	if (index != nullptr)
	{
		delete index;
		index = nullptr;
	}

	if (dstFileName != nullptr)
	{
		delete[] dstFileName;
		dstFileName = nullptr;
	}
}
