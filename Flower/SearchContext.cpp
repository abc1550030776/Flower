#include "SearchContext.h"
#include "Index.h"
#include "string.h"
#include "SetWithLock.h"
#include "SearchIndex.h"
#include <thread>
#include "common.h"
#include "ResultMapWithLock.h"
#include "KVContent.h"

SearchContext::SearchContext()
{
	index = nullptr;
	dstFileName = nullptr;
	threadNum = 0;
	rootIndexNum = 0;
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

	fpos_t pos;
	pos.__pos = 0;
	if (!indexFile.read(pos, &rootIndexNum, 8))
	{
		return false;
	}

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

				//判断是否是最后一行或者是是否是搜索的结果在同一行之内
				if (upperKey == filePos || (filePos + targetLen) <= upperKey)
				{
					resultMap->insert(filePos, value, filePos - lowerKey);
				}
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
