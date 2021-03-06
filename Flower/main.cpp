#include "interface.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include "Index.h"
#include "KVContent.h"
#include "common.h"
#include "BuildIndex.h"
#include "sys/time.h"
#include "Myfile.h"

int main()
{
	FILE* out = fopen("out", "w");
	if (out == nullptr)
	{
		printf("file open error");
		return 1;
	}
	char pPath[256] = { 0 };

	getcwd(pPath, 256);

	fprintf(out, "filePath: %s\n", pPath);
	fclose(out);
	out = fopen("out", "a");
	struct timeval start;
	struct timeval aend;
	unsigned long diff;
	gettimeofday(&start, nullptr);
	if(!BuildDstIndex("/test", true))
	{
		fprintf(out, "build index fail\n");
		fclose(out);
		return 1;
	}

	fprintf(out, "build success\n");
	fclose(out);
	out = fopen("out", "a");
	gettimeofday(&aend, nullptr);
	diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - start.tv_usec;
	fprintf(out, "build use time %ld\n", diff);
	fclose(out);
	out = fopen("out", "a");

	//从文件当中读取一点点数据作为搜索
	const unsigned long searchStrLen = 4 * 1024;
	char searchTarget[searchStrLen] = { 0 };
	Myfile myfile;
	if (!myfile.init("/test", false))
	{
		fprintf(out, "file init fail");
		fclose(out);
		return 1;
	}

	fpos_t pos;
	pos.__pos = 1024;
	if (!myfile.read(pos, searchTarget, searchStrLen))
	{
		fprintf(out, "read fail");
		fclose(out);
		return 1;
	}
	SearchContext searchContext;
	searchContext.init("/test", 0, true);
	gettimeofday(&start, nullptr);
	ResultMap result;
	if(!searchContext.search(searchTarget, searchStrLen, &result))
	{
		fprintf(out, "search fail \n");
		fclose(out);
		return 1;
	}

	fprintf(out, "search File success\n");
	fclose(out);
	out = fopen("out", "a");
	gettimeofday(&aend, nullptr);
	diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - start.tv_usec;
	fprintf(out, "search use time %ld\n", diff);
	fclose(out);
	out = fopen("out", "a");

	//打开文件看看查找的字符串对不对。
	FILE* file = fopen("/test", "rb");
	if (file == nullptr)
	{
		fprintf(out, "file open error");
		fclose(out);
		return 1;
	}

	char buffer[searchStrLen];

	for (auto& val : result)
	{
		fpos_t pos;
		pos.__pos = val.first;
		fsetpos(file, &pos);
		if (fread(buffer, searchStrLen, 1, file) != 1)
		{
			fclose(file);
			fprintf(out, "read file error filepos %llu", val.first);
			fclose(out);
			return 1;
		}

		//buffer[10] = '\0';
		//printf("file content %s \n", buffer);
		if (memcmp(buffer, searchTarget, searchStrLen))
		{
			fclose(file);
			fprintf(out, "search word pos not correct resultPos %llu result word %s", val.first, buffer);
			fclose(out);
			return 1;
		}

		//打印搜索到的位置和行
		FlwPrintf("out", "searchResult filePos %llu, lineNum %llu, columnNum %llu\n", val.first, val.second.GetLineNum(), val.second.GetColumnNum());
	}

	result.clear();
	gettimeofday(&start, nullptr);
	if (!searchContext.search(searchTarget, searchStrLen, &result))
	{
		fprintf(out, "search fail \n");
		fclose(out);
		return 1;
	}

	gettimeofday(&aend, nullptr);
	diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - start.tv_usec;
	fprintf(out, "search use time %ld\n", diff);
	fclose(out);
	out = fopen("out", "a");

	for (auto& val : result)
	{
		fpos_t pos;
		pos.__pos = val.first;
		fsetpos(file, &pos);
		if (fread(buffer, searchStrLen, 1, file) != 1)
		{
			fclose(file);
			fprintf(out, "read file error filepos %llu", val.first);
			fclose(out);
			return 1;
		}

		//buffer[10] = '\0';
		//printf("file content %s \n", buffer);
		if (memcmp(buffer, searchTarget, searchStrLen))
		{
			fclose(file);
			fprintf(out, "search word pos not correct resultPos %llu result word %s", val.first, buffer);
			fclose(out);
			return 1;
		}
	}

	fclose(file);

	//测试kv存储
	unsigned long long key[] = { 1, 6, 34, 89, 100, 128, 170, 200, 234, 240, 245, 289, 300, 377, 489, 500, 645, 700, 899, 999 };
	unsigned long long val[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
	std::map<unsigned long long, unsigned long long> map;
	for (unsigned long i = 0; i < sizeof(key) / sizeof(key[0]); ++i)
	{
		map.insert({ key[i], val[i] });
	}

	Index index(USE_TYPE_BUILD);
	Index kvIndex(USE_TYPE_BUILD);
	BuildIndex buildInex;
	buildInex.init("/testkv", &index, &kvIndex);
	for (unsigned long i = 0; i < sizeof(key) / sizeof(key[0]); ++i)
	{
		if (!buildInex.addKV(key[i], val[i]))
		{
			fprintf(out, "add kv failed\n");
			fclose(out);
			return 1;
		}
	}
	if (!buildInex.writeKvEveryCache())
	{
		fprintf(out, "write cache failed\n");
		fclose(out);
		return 1;
	}
	char kvIndexFile[4096];
	if (!getKVFilePath("/testkv", kvIndexFile))
	{
		fprintf(out, "get kv indexFile name failed\n");
		fclose(out);
		return 1;
	}
	KVContent kvContent;
	Index kvContentIndex;
	kvContent.init(kvIndexFile, &kvContentIndex);
	unsigned long long test[] = { 77, 555, 777 };
	for (unsigned long i = 0; i < sizeof(test) / sizeof(test[0]); ++i)
	{
		unsigned long long mapLowerBoundKey = 0;
		unsigned long long mapLowerBoundValue = 0;

		unsigned long long mapUpperBoundKey = 0;
		auto it = map.upper_bound(test[i]);
		if (it != end(map))
		{
			mapUpperBoundKey = it->first;
			--it;
			mapLowerBoundKey = it->first;
			mapLowerBoundValue = it->second;
		}

		unsigned long long kvLowerBoundKey = 0;
		unsigned long long kvLowerBoundValue = 0;
		unsigned long long kvUpperBoundKey = 0;
		if (!kvContent.get(test[i], kvLowerBoundKey, kvUpperBoundKey, kvLowerBoundValue))
		{
			fprintf(out, "search kv failed search key %llu\n", test[i]);
			fclose(out);
			return 1;
		}
		if (mapLowerBoundKey != kvLowerBoundKey || mapUpperBoundKey != kvUpperBoundKey || mapLowerBoundValue != kvLowerBoundValue)
		{
			fprintf(out, "search value not right right lowerKey %llu, upperKey %llu, value %llu, find lowerKey %llu, upperKey %llu, value %llu", mapLowerBoundKey, mapUpperBoundKey, mapLowerBoundValue, kvLowerBoundKey, kvUpperBoundKey, kvLowerBoundValue);
			fclose(out);
			return 1;
		}
	}
	fclose(out);
	return 0;
}
