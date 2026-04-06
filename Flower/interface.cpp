#include "Index.h"
#include "BuildIndex.h"
#include "SearchIndex.h"
#include <sys/resource.h>
#include <sys/stat.h>
#include <pthread.h>
#include <thread>
#include <vector>
#include <cstdio>
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

struct KvLineCountContext {
	const char* fileName;
	unsigned long long startPos;
	unsigned long long endPos;
	unsigned long long dstFileSize;
	char delimiter;
	unsigned long long producedLineStartCount;
	bool success;
};

struct KvSegmentBuildContext {
	const char* fileName;
	const char* kvFileName;
	UniqueGenerator* sharedGenerator;
	unsigned long long startPos;
	unsigned long long endPos;
	unsigned long long nextLineNum;
	bool includeFirstLine;
	char delimiter;
	unsigned long long rootId;
	bool success;
};

struct KvMergeContext {
	const char* kvFileName;
	UniqueGenerator* sharedGenerator;
	unsigned long long leftRootId;
	unsigned long long rightRootId;
	unsigned long long outRootId;
	bool success;
};

static bool countProducedLineStarts(const char* fileName, unsigned long long startPos, unsigned long long endPos,
	unsigned long long dstFileSize, char delimiter, unsigned long long& count)
{
	Myfile dstFile;
	if (!dstFile.init(fileName, false))
	{
		return false;
	}

	count = 0;
	for (unsigned long long filePos = startPos; filePos < endPos; filePos += 8)
	{
		unsigned char buffer[8];
		unsigned long long remainSize = endPos - filePos;
		unsigned long long readSize = (remainSize >= 8) ? 8 : remainSize;
		unsigned long long pos = filePos;
		if (!dstFile.read(pos, buffer, readSize))
		{
			return false;
		}

		for (unsigned long long i = 0; i < readSize; ++i)
		{
			if (buffer[i] != delimiter)
			{
				continue;
			}
			if ((filePos + i + 1) < dstFileSize)
			{
				++count;
			}
		}
	}
	return true;
}

static void* KvLineCountThreadFun(void* arg)
{
	KvLineCountContext* ctx = (KvLineCountContext*)arg;
	ctx->success = countProducedLineStarts(ctx->fileName, ctx->startPos, ctx->endPos,
		ctx->dstFileSize, ctx->delimiter, ctx->producedLineStartCount);
	return nullptr;
}

static void* KvSegmentBuildThreadFun(void* arg)
{
	KvSegmentBuildContext* ctx = (KvSegmentBuildContext*)arg;
	ctx->success = false;

	Index kvIndex(USE_TYPE_BUILD, ctx->sharedGenerator);
	BuildIndex buildIndex;
	if (!buildIndex.initForParallelKvBuild(ctx->fileName, ctx->kvFileName, &kvIndex))
	{
		return nullptr;
	}

	if (!buildIndex.buildKvSegment(ctx->startPos, ctx->endPos, ctx->nextLineNum, ctx->includeFirstLine,
		ctx->delimiter, ctx->rootId))
	{
		return nullptr;
	}

	ctx->success = true;
	return nullptr;
}

static void* KvMergeThreadFun(void* arg)
{
	KvMergeContext* ctx = (KvMergeContext*)arg;
	ctx->success = false;

	Index kvIndex(USE_TYPE_BUILD, ctx->sharedGenerator);
	BuildIndex buildIndex;
	if (!buildIndex.initForParallelKvMerge(ctx->kvFileName, &kvIndex))
	{
		return nullptr;
	}

	if (!buildIndex.mergeKvRoots(ctx->leftRootId, ctx->rightRootId, ctx->outRootId))
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
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	//获取文件大小
	struct stat statbuf;
	if (stat(fileName, &statbuf) != 0)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	unsigned long long dstFileSize = statbuf.st_size;

	//计算段数
	unsigned long rootIndexCount = (unsigned long)((dstFileSize + DST_SIZE_PER_ROOT - 1) / DST_SIZE_PER_ROOT);

	//确定线程数量
	unsigned long threadCount = std::thread::hardware_concurrency() * 2;
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
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}
		else
		{
			if (!buildIndex.init(fileName, &index))
			{
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}
		return buildIndex.build(needBuildLineIndex, delimiter);
	}

	//===== 多线程构建路径 =====

	//获取索引文件路径
	char indexFileName[4096] = { 0 };
	if (!getIndexPath(fileName, indexFileName))
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	//如果需要构建行索引，先并行构建KV索引
	if (needBuildLineIndex)
	{
		char kvFileName[4096] = { 0 };
		if (!getKVFilePath(fileName, kvFileName))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}

		remove(kvFileName);
		{
			Myfile kvFileCreator;
			if (!kvFileCreator.init(kvFileName, true))
			{
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}

		unsigned long kvSegmentsPerThread = (rootIndexCount + threadCount - 1) / threadCount;
		unsigned long kvThreadCount = (rootIndexCount + kvSegmentsPerThread - 1) / kvSegmentsPerThread;
		std::vector<KvLineCountContext> kvCountContexts(kvThreadCount);
		std::vector<pthread_t> kvCountPids(kvThreadCount);

		for (unsigned long i = 0; i < kvThreadCount; ++i)
		{
			unsigned long startSegment = i * kvSegmentsPerThread;
			unsigned long endSegment = startSegment + kvSegmentsPerThread;
			if (endSegment > rootIndexCount) endSegment = rootIndexCount;

			kvCountContexts[i].fileName = fileName;
			kvCountContexts[i].startPos = (unsigned long long)startSegment * DST_SIZE_PER_ROOT;
			kvCountContexts[i].endPos = (unsigned long long)endSegment * DST_SIZE_PER_ROOT;
			if (kvCountContexts[i].endPos > dstFileSize)
			{
				kvCountContexts[i].endPos = dstFileSize;
			}
			kvCountContexts[i].dstFileSize = dstFileSize;
			kvCountContexts[i].delimiter = delimiter;
			kvCountContexts[i].producedLineStartCount = 0;
			kvCountContexts[i].success = false;

			if (pthread_create(&kvCountPids[i], NULL, KvLineCountThreadFun, &kvCountContexts[i]) != 0)
			{
				for (unsigned long j = 0; j < i; ++j)
				{
					pthread_join(kvCountPids[j], NULL);
				}
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}

		bool allKvCountSuccess = true;
		for (unsigned long i = 0; i < kvThreadCount; ++i)
		{
			pthread_join(kvCountPids[i], NULL);
			if (!kvCountContexts[i].success)
			{
				allKvCountSuccess = false;
			}
		}
		if (!allKvCountSuccess)
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}

		UniqueGenerator sharedKvGenerator;
		std::vector<KvSegmentBuildContext> kvBuildContexts(kvThreadCount);
		std::vector<pthread_t> kvBuildPids(kvThreadCount);
		unsigned long long producedPrefixCount = 0;
		for (unsigned long i = 0; i < kvThreadCount; ++i)
		{
			kvBuildContexts[i].fileName = fileName;
			kvBuildContexts[i].kvFileName = kvFileName;
			kvBuildContexts[i].sharedGenerator = &sharedKvGenerator;
			kvBuildContexts[i].startPos = kvCountContexts[i].startPos;
			kvBuildContexts[i].endPos = kvCountContexts[i].endPos;
			kvBuildContexts[i].nextLineNum = producedPrefixCount + 1;
			kvBuildContexts[i].includeFirstLine = (i == 0);
			kvBuildContexts[i].delimiter = delimiter;
			kvBuildContexts[i].rootId = 0;
			kvBuildContexts[i].success = false;
			producedPrefixCount += kvCountContexts[i].producedLineStartCount;

			if (pthread_create(&kvBuildPids[i], NULL, KvSegmentBuildThreadFun, &kvBuildContexts[i]) != 0)
			{
				for (unsigned long j = 0; j < i; ++j)
				{
					pthread_join(kvBuildPids[j], NULL);
				}
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}

		bool allKvBuildSuccess = true;
		for (unsigned long i = 0; i < kvThreadCount; ++i)
		{
			pthread_join(kvBuildPids[i], NULL);
			if (!kvBuildContexts[i].success)
			{
				allKvBuildSuccess = false;
			}
		}
		if (!allKvBuildSuccess)
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}

		std::vector<unsigned long long> kvRoots;
		for (unsigned long i = 0; i < kvThreadCount; ++i)
		{
			if (kvBuildContexts[i].rootId != 0)
			{
				kvRoots.push_back(kvBuildContexts[i].rootId);
			}
		}

		while (kvRoots.size() > 1)
		{
			unsigned long pairCount = (unsigned long)(kvRoots.size() / 2);
			std::vector<KvMergeContext> mergeContexts(pairCount);
			std::vector<pthread_t> mergePids(pairCount);

			for (unsigned long i = 0; i < pairCount; ++i)
			{
				mergeContexts[i].kvFileName = kvFileName;
				mergeContexts[i].sharedGenerator = &sharedKvGenerator;
				mergeContexts[i].leftRootId = kvRoots[i * 2];
				mergeContexts[i].rightRootId = kvRoots[i * 2 + 1];
				mergeContexts[i].outRootId = 0;
				mergeContexts[i].success = false;

				if (pthread_create(&mergePids[i], NULL, KvMergeThreadFun, &mergeContexts[i]) != 0)
				{
					for (unsigned long j = 0; j < i; ++j)
					{
						pthread_join(mergePids[j], NULL);
					}
					printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
				}
			}

			bool allMergeSuccess = true;
			for (unsigned long i = 0; i < pairCount; ++i)
			{
				pthread_join(mergePids[i], NULL);
				if (!mergeContexts[i].success)
				{
					allMergeSuccess = false;
				}
			}
			if (!allMergeSuccess)
			{
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}

			std::vector<unsigned long long> nextRoots;
			nextRoots.reserve((kvRoots.size() + 1) / 2);
			for (unsigned long i = 0; i < pairCount; ++i)
			{
				nextRoots.push_back(mergeContexts[i].outRootId);
			}
			if (kvRoots.size() % 2 == 1)
			{
				nextRoots.push_back(kvRoots.back());
			}
			kvRoots.swap(nextRoots);
		}

		unsigned long long finalKvRootId = kvRoots.empty() ? 0 : kvRoots[0];
		Myfile kvIndexFile;
		if (!kvIndexFile.init(kvFileName, false))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
		unsigned long long pos = 0;
		if (!kvIndexFile.write(pos, &finalKvRootId, 8))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
		if (!kvIndexFile.sync())
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	unsigned long long rootCount = allRootIds.size();
	unsigned long long pos = 0;
	if (!indexFileForHeader.write(pos, &rootCount, 8))
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	if (rootCount > 0)
	{
		pos = 8;
		if (!indexFileForHeader.write(pos, &allRootIds[0], 8 * rootCount))
		{
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
	}
	if (!indexFileForHeader.sync())
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	return true;
}

LineAndColumn::LineAndColumn()
{
	lineNum = 0;
	columnNum = 0;
	endLineNum = 0;
	endColumnNum = 0;
}

LineAndColumn::LineAndColumn(unsigned long long lineNum, unsigned long long columnNum)
	: lineNum(lineNum), columnNum(columnNum), endLineNum(lineNum), endColumnNum(columnNum)
{}

LineAndColumn::LineAndColumn(unsigned long long lineNum, unsigned long long columnNum,
							 unsigned long long endLineNum, unsigned long long endColumnNum)
	: lineNum(lineNum), columnNum(columnNum), endLineNum(endLineNum), endColumnNum(endColumnNum)
{}

unsigned long long LineAndColumn::GetLineNum()
{
	return lineNum;
}

unsigned long long LineAndColumn::GetColumnNum()
{
	return columnNum;
}

unsigned long long LineAndColumn::GetEndLineNum()
{
	return endLineNum;
}

unsigned long long LineAndColumn::GetEndColumnNum()
{
	return endColumnNum;
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
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	if (searchTarget == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	if (set == nullptr)
	{
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
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
