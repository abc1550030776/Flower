#include "Index.h"
#include "BuildIndex.h"
#include "SearchIndex.h"

//创建索引
bool BuildDstIndex(const char* fileName)
{
	Index index(USE_TYPE_BUILD);

	BuildIndex buildIndex;
	if (!buildIndex.init(fileName, &index))
	{
		return false;
	}
	return buildIndex.build();
}

static void* ThreadFun(void* arg)
{
	SearchIndex* searchIndex = (SearchIndex*) arg;
	return (void*)searchIndex->search();
}

//查询文件
bool SearchFile(const char* fileName, const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set)
{
	if (fileName == nullptr)
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
	SetWithLock resultSet(*set);

	Index index;
	SearchIndex searchIndex[8];
	pthread_t pids[8];
	for (unsigned char i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
	{
		searchIndex[i].init(searchTarget, targetLen, &resultSet, fileName, &index, i);
		if (pthread_create(&pids[i], NULL, ThreadFun, &searchIndex[i]) != 0)
		{
			for (unsigned int j = 0; j < i; ++j)
			{
				pthread_join(pids[j], NULL);
			}

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

	return success;
}
