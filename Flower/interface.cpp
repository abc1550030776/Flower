#include "Index.h"
#include "BuildIndex.h"
#include "SearchIndex.h"
#include <sys/resource.h>
#include <sys/stat.h>
#include <pthread.h>
#include <thread>
#include <vector>
#include "interface.h"
#include "common.h"
#include "UniqueGenerator.h"

//多线程段构建的上下文
struct SegmentBuildContext {
	const char* fileName;
	const char* indexFileName;
	UniqueGenerator* sharedGenerator;
	unsigned long long startPos;
	unsigned long long endPos;
	unsigned long long dstFileSize;
	std::vector<unsigned long long> rootIds;
	bool success;
};

static void* SegmentBuildThreadFun(void* arg)
{
	SegmentBuildContext* ctx = (SegmentBuildContext*)arg;
	ctx->success = false;

	//每个线程创建自己的Index，使用共享的UniqueGenerator
	Index index(USE_TYPE_BUILD, ctx->sharedGenerator);

	//每个线程创建自己的BuildIndex，各自打开独立的文件描述符
	BuildIndex buildIndex;
	if (!buildIndex.initForSegment(ctx->fileName, ctx->indexFileName, &index))
	{
		return nullptr;
	}

	if (!buildIndex.buildSegment(ctx->startPos, ctx->endPos, ctx->rootIds))
	{
		return nullptr;
	}

	ctx->success = true;
	return nullptr;
}

static bool increaseStackSize()
{
	const rlim_t kStackSize = 1024 * 1024 * 1024;   // min stack size = 1 GB
	struct rlimit rl;

	int result = getrlimit(RLIMIT_STACK, &rl);
	if (result != 0)
	{
		return false;
	}

	if (rl.rlim_cur < kStackSize)
	{
		rl.rlim_cur = kStackSize;
		if (rl.rlim_cur > rl.rlim_max)
		{
			rl.rlim_cur = rl.rlim_max;
		}
		result = setrlimit(RLIMIT_STACK, &rl);
		if (result != 0)
		{
			fprintf(stderr, "setrlimit returned result = %d\n", result);
		}
	}
	return true;
}

//创建索引
bool BuildDstIndex(const char* fileName, bool needBuildLineIndex, char delimiter)
{
	if (!increaseStackSize())
	{
		return false;
	}

	//获取文件大小
	struct stat statbuf;
	if (stat(fileName, &statbuf) != 0)
	{
		return false;
	}
	unsigned long long dstFileSize = statbuf.st_size;

	//计算段数
	unsigned long rootIndexCount = (unsigned long)((dstFileSize + DST_SIZE_PER_ROOT - 1) / DST_SIZE_PER_ROOT);

	//确定线程数量
	unsigned long threadCount = std::thread::hardware_concurrency();
	if (threadCount == 0) threadCount = 1;
	if (threadCount > rootIndexCount) threadCount = rootIndexCount;

	//段数<=1或线程数<=1时使用原有的单线程路径
	if (threadCount <= 1 || rootIndexCount <= 1)
	{
		Index index(USE_TYPE_BUILD);
		Index kvIndex(USE_TYPE_BUILD);

		BuildIndex buildIndex;
		if (needBuildLineIndex)
		{
			if (!buildIndex.init(fileName, &index, &kvIndex))
			{
				return false;
			}
		}
		else
		{
			if (!buildIndex.init(fileName, &index))
			{
				return false;
			}
		}
		return buildIndex.build(needBuildLineIndex, delimiter);
	}

	//===== 多线程构建路径 =====

	//获取索引文件路径
	char indexFileName[4096] = { 0 };
	if (!getIndexPath(fileName, indexFileName))
	{
		return false;
	}

	//如果需要构建行索引，先顺序构建KV索引（行号必须顺序计算）
	if (needBuildLineIndex)
	{
		Index kvIndex(USE_TYPE_BUILD);
		Index dummyIndex(USE_TYPE_BUILD);
		BuildIndex kvBuilder;
		if (!kvBuilder.init(fileName, &dummyIndex, &kvIndex))
		{
			return false;
		}
		if (!kvBuilder.buildKvIndex(delimiter))
		{
			return false;
		}
	}

	//计算文件头需要的空间
	unsigned long needBlock = (unsigned long)(((rootIndexCount + 1) * 8 + SIZE_PER_INDEX_FILE_GRID - 1) / SIZE_PER_INDEX_FILE_GRID);

	//创建共享的UniqueGenerator
	UniqueGenerator sharedGenerator;
	sharedGenerator.setInitMaxUniqueNum(needBlock);

	//主线程创建/截断索引文件（确保文件存在且为空）
	//KV阶段可能已创建此文件，需要先删除再重建
	remove(indexFileName);
	{
		Myfile indexFileCreator;
		if (!indexFileCreator.init(indexFileName, true))
		{
			return false;
		}
	}

	//分配段给各线程
	unsigned long segmentsPerThread = (rootIndexCount + threadCount - 1) / threadCount;
	//重新计算线程数以避免空线程
	threadCount = (rootIndexCount + segmentsPerThread - 1) / segmentsPerThread;

	std::vector<SegmentBuildContext> contexts(threadCount);
	std::vector<pthread_t> pids(threadCount);

	for (unsigned long i = 0; i < threadCount; ++i)
	{
		unsigned long startSegment = i * segmentsPerThread;
		unsigned long endSegment = startSegment + segmentsPerThread;
		if (endSegment > rootIndexCount) endSegment = rootIndexCount;

		contexts[i].fileName = fileName;
		contexts[i].indexFileName = indexFileName;
		contexts[i].sharedGenerator = &sharedGenerator;
		contexts[i].startPos = (unsigned long long)startSegment * DST_SIZE_PER_ROOT;
		contexts[i].endPos = (unsigned long long)endSegment * DST_SIZE_PER_ROOT;
		if (contexts[i].endPos > dstFileSize)
			contexts[i].endPos = dstFileSize;
		contexts[i].dstFileSize = dstFileSize;
		contexts[i].success = false;

		if (pthread_create(&pids[i], NULL, SegmentBuildThreadFun, &contexts[i]) != 0)
		{
			//创建线程失败，等待已创建的线程
			for (unsigned long j = 0; j < i; ++j)
			{
				pthread_join(pids[j], NULL);
			}
			return false;
		}
	}

	//等待所有线程完成
	bool allSuccess = true;
	for (unsigned long i = 0; i < threadCount; ++i)
	{
		pthread_join(pids[i], NULL);
		if (!contexts[i].success)
		{
			allSuccess = false;
		}
	}

	if (!allSuccess)
	{
		return false;
	}

	//按顺序收集所有根节点id
	std::vector<unsigned long long> allRootIds;
	for (unsigned long i = 0; i < threadCount; ++i)
	{
		for (auto id : contexts[i].rootIds)
		{
			allRootIds.push_back(id);
		}
	}

	//把根节点id列表写入索引文件头部
	Myfile indexFileForHeader;
	if (!indexFileForHeader.init(indexFileName, false))
	{
		return false;
	}

	unsigned long long rootCount = allRootIds.size();
	unsigned long long pos = 0;
	if (!indexFileForHeader.write(pos, &rootCount, 8))
	{
		return false;
	}
	if (rootCount > 0)
	{
		pos = 8;
		if (!indexFileForHeader.write(pos, &allRootIds[0], 8 * rootCount))
		{
			return false;
		}
	}
	if (!indexFileForHeader.sync())
	{
		return false;
	}

	return true;
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
