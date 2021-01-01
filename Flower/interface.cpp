#include "Index.h"
#include "BuildIndex.h"
#include "SearchIndex.h"
#include <sys/resource.h>
#include "interface.h"

//创建索引
bool BuildDstIndex(const char* fileName)
{
	//创建的时候可能递归的层数会很大可能遇到栈空间不够用的情况先修改栈空间大小
	const rlim_t kStackSize = 1024 * 1024 * 1024;   // min stack size = 1 GB
	struct rlimit rl;
	int result;

	result = getrlimit(RLIMIT_STACK, &rl);
	if (result == 0)
	{
		if (rl.rlim_cur < kStackSize)
		{
			rl.rlim_cur = kStackSize;
			result = setrlimit(RLIMIT_STACK, &rl);
			if (result != 0)
			{
				fprintf(stderr, "setrlimit returned result = %d\n", result);
				return false;
			}
		}
	}
	else
	{
		return false;
	}
	Index index(USE_TYPE_BUILD);

	BuildIndex buildIndex;
	if (!buildIndex.init(fileName, &index))
	{
		return false;
	}
	return buildIndex.build();
}

LineAndColumn::LineAndColumn()
{
	lineNum = 0;
	columnNum = 0;
}

LineAndColumn::LineAndColumn(unsigned long long lineNum, unsigned long long columnNum) : lineNum(lineNum), columnNum(columnNum)
{}

unsigned long long LineAndColumn::GetLineNum()
{
	return lineNum;
}

unsigned long long LineAndColumn::GetColumnNum()
{
	return columnNum;
}

/*static void* ThreadFun(void* arg)
{
	SearchIndex* searchIndex = (SearchIndex*) arg;
	return (void*)searchIndex->search();
}*/

/*
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
	SetWithLock* resultSet = new SetWithLock(set);

	Index index;
	SearchIndex searchIndex[8];
	pthread_t pids[8];
	for (unsigned char i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
	{
		searchIndex[i].init(searchTarget, targetLen, resultSet, fileName, &index, i);
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
*/
