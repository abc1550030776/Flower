#include "SearchContext.h"
#include "Index.h"
#include "string.h"
#include "SetWithLock.h"
#include "SearchIndex.h"
#include <thread>
#include "common.h"
#include "ResultMapWithLock.h"
#include "KVContent.h"
#include <vector>
#include <sys/stat.h>

namespace {

constexpr size_t SHORT_SEARCH_CHUNK_SIZE = 1 << 20;

struct ShortSearchWorker
{
	const char* fileName = nullptr;
	const unsigned char* needle = nullptr;
	unsigned int needleLen = 0;
	uint64_t packedNeedle = 0;
	unsigned char firstByte = 0;
	unsigned char lastByte = 0;
	unsigned long long rangeStart = 0;
	unsigned long long rangeEnd = 0;
	unsigned long long fileSize = 0;
	SetWithLock* resultSet = nullptr;

	bool init(const char* fileName, const char* searchTarget, unsigned int targetLen,
		unsigned long long rangeStart, unsigned long long rangeEnd, unsigned long long fileSize, SetWithLock* resultSet)
	{
		if (fileName == nullptr || searchTarget == nullptr || targetLen == 0 || targetLen >= 8 || resultSet == nullptr)
		{
			return false;
		}
		this->fileName = fileName;
		this->needle = reinterpret_cast<const unsigned char*>(searchTarget);
		this->needleLen = targetLen;
		this->packedNeedle = LoadUint64Partial(this->needle, targetLen);
		this->firstByte = this->needle[0];
		this->lastByte = this->needle[targetLen - 1];
		this->rangeStart = rangeStart;
		this->rangeEnd = rangeEnd;
		this->fileSize = fileSize;
		this->resultSet = resultSet;
		return true;
	}

	bool search()
	{
		Myfile file;
		if (!file.init(fileName, false))
		{
			return false;
		}

		if (rangeStart >= rangeEnd || rangeStart >= fileSize)
		{
			return true;
		}

		unsigned long long cursor = rangeStart;
		while (cursor < rangeEnd)
		{
			size_t scanSize = static_cast<size_t>(rangeEnd - cursor);
			if (scanSize > SHORT_SEARCH_CHUNK_SIZE)
			{
				scanSize = SHORT_SEARCH_CHUNK_SIZE;
			}

			size_t overlap = needleLen > 0 ? needleLen - 1 : 0;
			size_t readSize = scanSize + overlap;
			unsigned long long remainingFileSize = fileSize - cursor;
			if (readSize > remainingFileSize)
			{
				readSize = static_cast<size_t>(remainingFileSize);
			}

			std::vector<unsigned char> buffer(readSize);
			if (readSize != 0 && !file.read(cursor, buffer.data(), readSize))
			{
				return false;
			}

			if (needleLen == 1)
			{
				const unsigned char* found = buffer.data();
				size_t left = scanSize;
				while (left != 0)
				{
					found = reinterpret_cast<const unsigned char*>(memchr(found, firstByte, left));
					if (found == nullptr)
					{
						break;
					}
					size_t offset = static_cast<size_t>(found - buffer.data());
					resultSet->insert(cursor + offset);
					++found;
					left = scanSize - offset - 1;
				}
			}
			else if (readSize >= needleLen)
			{
				size_t matchLimit = scanSize;
				size_t maxMatchStarts = readSize - needleLen + 1;
				if (matchLimit > maxMatchStarts)
				{
					matchLimit = maxMatchStarts;
				}
				for (size_t i = 0; i < matchLimit; ++i)
				{
					if (buffer[i] != firstByte || buffer[i + needleLen - 1] != lastByte)
					{
						continue;
					}
					if (LoadUint64Partial(buffer.data() + i, needleLen) == packedNeedle)
					{
						resultSet->insert(cursor + i);
					}
				}
			}

			cursor += scanSize;
		}
		return true;
	}
};

static void* ShortSearchThreadFun(void* arg)
{
	return reinterpret_cast<void*>(static_cast<uintptr_t>(reinterpret_cast<ShortSearchWorker*>(arg)->search()));
}

bool RunShortSearchParallel(const char* fileName, const char* searchTarget, unsigned int targetLen,
	unsigned long threadNum, unsigned long long fileSize, SetWithLock* resultSet)
{
	if (fileName == nullptr || searchTarget == nullptr || targetLen == 0 || targetLen >= 8 || resultSet == nullptr)
	{
		return false;
	}

	if (fileSize < targetLen)
	{
		return true;
	}

	unsigned long long searchableSize = fileSize - targetLen + 1;
	unsigned long workerCount = threadNum == 0 ? 1 : threadNum;
	if (workerCount > searchableSize)
	{
		workerCount = static_cast<unsigned long>(searchableSize);
	}
	if (workerCount == 0)
	{
		workerCount = 1;
	}

	unsigned long long rangePerWorker = (searchableSize + workerCount - 1) / workerCount;
	std::vector<ShortSearchWorker> workers(workerCount);
	std::vector<pthread_t> pids(workerCount);
	unsigned long created = 0;
	for (unsigned long i = 0; i < workerCount; ++i)
	{
		unsigned long long rangeStart = i * rangePerWorker;
		unsigned long long rangeEnd = rangeStart + rangePerWorker;
		if (rangeEnd > searchableSize)
		{
			rangeEnd = searchableSize;
		}
		if (!workers[i].init(fileName, searchTarget, targetLen, rangeStart, rangeEnd, fileSize, resultSet))
		{
			return false;
		}
		if (pthread_create(&pids[i], NULL, ShortSearchThreadFun, &workers[i]) != 0)
		{
			for (unsigned long j = 0; j < created; ++j)
			{
				pthread_join(pids[j], NULL);
			}
			return false;
		}
		++created;
	}

	bool success = true;
	for (unsigned long i = 0; i < created; ++i)
	{
		void* ret = nullptr;
		pthread_join(pids[i], &ret);
		if (!static_cast<bool>(reinterpret_cast<uintptr_t>(ret)))
		{
			success = false;
		}
	}
	return success;
}

bool FillLineAndColumnResult(const char* dstFileName, Index* kvIndex, const std::set<unsigned long long>& positions,
	unsigned int targetLen, ResultMapWithLock& resultMap)
{
	if (dstFileName == nullptr || kvIndex == nullptr)
	{
		return false;
	}

	char kvIndexFile[4096] = {};
	if (!getKVFilePath(dstFileName, kvIndexFile))
	{
		return false;
	}

	KVContent kvContent;
	if (!kvContent.init(kvIndexFile, kvIndex))
	{
		return false;
	}

	for (auto& filePos : positions)
	{
		unsigned long long lowerKey = 0;
		unsigned long long upperKey = 0;
		unsigned long long value = 0;
		if (!kvContent.get(filePos, lowerKey, upperKey, value))
		{
			return false;
		}

		unsigned long long startLine = value;
		unsigned long long startColumn = filePos - lowerKey;
		unsigned long long matchEnd = filePos + targetLen - 1;
		unsigned long long endLine = 0;
		unsigned long long endColumn = 0;
		if (matchEnd < upperKey)
		{
			endLine = startLine;
			endColumn = matchEnd - lowerKey;
		}
		else
		{
			unsigned long long endLowerKey = 0;
			unsigned long long endUpperKey = 0;
			unsigned long long endValue = 0;
			if (!kvContent.get(matchEnd, endLowerKey, endUpperKey, endValue))
			{
				return false;
			}
			endLine = endValue;
			endColumn = matchEnd - endLowerKey;
		}

		resultMap.insert(filePos, startLine, startColumn, endLine, endColumn);
	}
	return true;
}

}

SearchContext::SearchContext()
{
	index = nullptr;
	dstFileName = nullptr;
	threadNum = 0;
	rootIndexNum = 0;
	dstFileSize = 0;
	kvIndex = nullptr;
}

bool SearchContext::init(const char* fileName, unsigned long threadNum, bool searchLine)
{
	if (fileName == nullptr)
	{
		return false;
	}
	index = new Index();
	unsigned long strLen = strlen(fileName);
	dstFileName = new char[strLen + 1];
	strcpy(dstFileName, fileName);
	if (threadNum == 0)
	{
		this->threadNum = std::thread::hardware_concurrency();
	}
	else
	{
		this->threadNum = threadNum;
	}

	//从索引文件当中把那个根节点数量给读取出来
	char indexFileName[4096];
	memset(indexFileName, 0, sizeof(indexFileName));
	//获取索引文件的名字
	if (!getIndexPath(fileName, indexFileName))
	{
		return false;
	}

	Myfile indexFile;
	if (!indexFile.init(indexFileName, false))
	{
		return false;
	}

	unsigned long long pos;
	pos = 0;
	if (!indexFile.read(pos, &rootIndexNum, 8))
	{
		return false;
	}

	struct stat statbuf;
	if (stat(fileName, &statbuf) != 0)
	{
		return false;
	}
	dstFileSize = statbuf.st_size;

	if (searchLine)
	{
		kvIndex = new Index();
	}
	return true;
}

static void* ThreadFun(void* arg)
{
	return (void*)((SearchIndex*)arg)->search();
}

class SearchHelper
{
public:
	SearchHelper()
	{
		searchTarget = nullptr;
		targetLen = 0;
		resultSet = nullptr;
		dstFileName = nullptr;
		index = nullptr;
		orderStart = 0;
		orderEnd = 0;
	}
	bool init(const char* searchTarget, unsigned int targetLen, SetWithLock* resultSet, const char* dstFileName, Index* index, unsigned long orderStart, unsigned long orderEnd)
	{
		this->searchTarget = searchTarget;
		this->targetLen = targetLen;
		this->resultSet = resultSet;
		this->dstFileName = dstFileName;
		this->index = index;
		this->orderStart = orderStart;
		this->orderEnd = orderEnd;
		return true;
	}

	bool search()
	{
		bool success = true;
		for (unsigned long order = orderStart; order < orderEnd; ++order)
		{
			if (!searchOneOrder(order))
			{
				success = false;
				break;
			}
		}
		return success;
	}

private:
	//对其中一个根节点进行搜索
	bool searchOneOrder(unsigned long order)
	{
		SearchIndex searchIndex[8];
		pthread_t pids[8];
		for (char i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
		{
			searchIndex[i].init(searchTarget, targetLen, resultSet, dstFileName, index, i, order);
			if (pthread_create(&pids[i], NULL, ThreadFun, &searchIndex[i]) != 0)
			{
				for (char j = 0; j < i; ++j)
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
			void* ret = nullptr;
			pthread_join(pids[i], &ret);
			if (!((bool)ret))
			{
				success = false;
			}
		}
		return success;
	}
	const char* searchTarget;
	unsigned int targetLen;
	SetWithLock* resultSet;
	const char* dstFileName;
	Index* index;
	unsigned long orderStart;
	unsigned long orderEnd;
};

static void* HelperThreadFun(void* arg)
{
	return (void*)((SearchHelper*)arg)->search();
}

class SearchPosAndLineHelper
{
public:
	SearchPosAndLineHelper()
	{
		searchTarget = nullptr;
		targetLen = 0;
		resultMap = nullptr;
		dstFileName = nullptr;
		index = nullptr;
		orderStart = 0;
		orderEnd = 0;
	}
	bool init(const char* searchTarget, unsigned int targetLen, ResultMapWithLock* resultMap, const char* dstFileName, Index* index, unsigned long orderStart, unsigned long orderEnd, Index* kvIndex)
	{
		this->searchTarget = searchTarget;
		this->targetLen = targetLen;
		this->resultMap = resultMap;
		this->dstFileName = dstFileName;
		this->index = index;
		this->orderStart = orderStart;
		this->orderEnd = orderEnd;
		char kvIndexFile[4096] = {};
		if (!getKVFilePath(dstFileName, kvIndexFile))
		{
			return false;
		}
		if (!kvContent.init(kvIndexFile, kvIndex))
		{
			return false;
		}
		return true;
	}

	bool search()
	{
		if (resultMap == nullptr)
		{
			return false;
		}
		for (unsigned long order = orderStart; order < orderEnd; ++order)
		{
			std::set<unsigned long long> set;
			SetWithLock resultSet(&set);
			if (!searchOneOrder(order, &resultSet))
			{
				return false;
			}

			//通过这一部分的搜索结果查找是第几行第几列
			unsigned long long lowerKey = 0;
			unsigned long long upperKey = 0;
			unsigned long long value = 0;
			for (auto& filePos : set)
			{
				if (!kvContent.get(filePos, lowerKey, upperKey, value))
				{
					return false;
				}

				unsigned long long startLine = value;
				unsigned long long startColumn = filePos - lowerKey;
				unsigned long long matchEnd = filePos + targetLen - 1;

				unsigned long long endLine, endColumn;
				if (matchEnd < upperKey)
				{
					//单行匹配：结束位置在同一行
					endLine = startLine;
					endColumn = matchEnd - lowerKey;
				}
				else
				{
					//跨行匹配：查询结束位置所在行
					unsigned long long endLowerKey = 0, endUpperKey = 0, endValue = 0;
					if (!kvContent.get(matchEnd, endLowerKey, endUpperKey, endValue))
					{
						return false;
					}
					endLine = endValue;
					endColumn = matchEnd - endLowerKey;
				}

				resultMap->insert(filePos, startLine, startColumn, endLine, endColumn);
			}
		}
		return true;
	}
private:
	bool searchOneOrder(unsigned long order, SetWithLock* resultSet)
	{
		SearchIndex searchIndex[8];
		pthread_t pids[8];
		for (char i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
		{
			searchIndex[i].init(searchTarget, targetLen, resultSet, dstFileName, index, i, order);
			if (pthread_create(&pids[i], NULL, ThreadFun, &searchIndex[i]) != 0)
			{
				for (char j = 0; j < i; ++j)
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
			void* ret = nullptr;
			pthread_join(pids[i], &ret);
			if (!((bool)ret))
			{
				success = false;
			}
		}
		return success;
	}
	const char* searchTarget;
	unsigned int targetLen;
	ResultMapWithLock* resultMap;
	const char* dstFileName;
	Index* index;
	KVContent kvContent;
	unsigned long orderStart;
	unsigned long orderEnd;
};

static void* PosAndLineHelperThreadFun(void* arg)
{
	return (void*)((SearchPosAndLineHelper*)arg)->search();
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

	if (targetLen > 0 && targetLen < 8)
	{
		SetWithLock resultSet(set);
		return RunShortSearchParallel(dstFileName, searchTarget, targetLen, threadNum, dstFileSize, &resultSet);
	}
	//这里使用多线程搜索
	SetWithLock* resultSet = new SetWithLock(set);

	//这里先算出需要多少个helper,每个helper都会开8个线程这里控制线程数不会超过核心数太多
	unsigned long helperCount = (threadNum + 8 - 1) / 8;
	//算出每个helper算多少个根节点
	unsigned long rootPerHelper = (rootIndexNum + helperCount - 1) / helperCount;
	//算出了每个helper算多少个root了以后有可能是无法平均分导致不需要那么多个helper就能算完所有root所以这里修正一下。
	helperCount = (rootIndexNum + rootPerHelper - 1) / rootPerHelper;

	//这里创建helperCount个helper和pids
	std::vector<SearchHelper> helpers(helperCount);
	std::vector<pthread_t> pids(helperCount);
	for (unsigned long i = 0; i < helpers.size(); ++i)
	{
		unsigned long orderStart = i * rootPerHelper;
		unsigned long orderEnd = orderStart + rootPerHelper;
		if (orderEnd > rootIndexNum)
		{
			orderEnd = rootIndexNum;
		}
		helpers[i].init(searchTarget, targetLen, resultSet, dstFileName, index, orderStart, orderEnd);
		if (pthread_create(&pids[i], NULL, HelperThreadFun, &helpers[i]) != 0)
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
	for (unsigned int i = 0; i < helpers.size(); ++i)
	{
		void* ret = nullptr;
		pthread_join(pids[i], &ret);
		if (!((bool)ret))
		{
			success = false;
		}
	}
	delete resultSet;
	return success;
}

bool SearchContext::search(const char* searchTarget, unsigned int targetLen, ResultMap* map)
{
	if (dstFileName == nullptr)
	{
		return false;
	}

	if (index == nullptr)
	{
		return false;
	}

	if (kvIndex == nullptr)
	{
		return false;
	}

	if (searchTarget == nullptr)
	{
		return false;
	}

	if (map == nullptr)
	{
		return false;
	}

	ResultMapWithLock resultMap(*map);

	if (targetLen > 0 && targetLen < 8)
	{
		std::set<unsigned long long> positions;
		SetWithLock resultSet(&positions);
		if (!RunShortSearchParallel(dstFileName, searchTarget, targetLen, threadNum, dstFileSize, &resultSet))
		{
			return false;
		}
		return FillLineAndColumnResult(dstFileName, kvIndex, positions, targetLen, resultMap);
	}

	//这里先算出需要多少个helper,每个helper都会开8个线程这里控制线程数不会超过核心数太多
	unsigned long helperCount = (threadNum + 8 - 1) / 8;
	//算出每个helper算多少个根节点
	unsigned long rootPerHelper = (rootIndexNum + helperCount - 1) / helperCount;
	//算出了每个helper算多少个root了以后有可能是无法平均分导致不需要那么多个helper就能算完所有root所以这里修正一下。
	helperCount = (rootIndexNum + rootPerHelper - 1) / rootPerHelper;

	std::vector<SearchPosAndLineHelper> helpers(helperCount);
	std::vector<pthread_t> pids(helperCount);
	for (unsigned long i = 0; i < helpers.size(); ++i)
	{
		unsigned long orderStart = i * rootPerHelper;
		unsigned long orderEnd = orderStart + rootPerHelper;
		if (orderEnd > rootIndexNum)
		{
			orderEnd = rootIndexNum;
		}
		if (!helpers[i].init(searchTarget, targetLen, &resultMap, dstFileName, index, orderStart, orderEnd, kvIndex))
		{
			return false;
		}
		if (pthread_create(&pids[i], NULL, PosAndLineHelperThreadFun, &helpers[i]) != 0)
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
	for (unsigned int i = 0; i < helpers.size(); ++i)
	{
		void* ret = nullptr;
		pthread_join(pids[i], &ret);
		if (!((bool)ret))
		{
			success = false;
		}
	}
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

	if (kvIndex != nullptr)
	{
		delete kvIndex;
		kvIndex = nullptr;
	}
}
