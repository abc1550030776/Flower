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
	struct timeval start;
	struct timeval aend;
	unsigned long diff;
	gettimeofday(&start, nullptr);
	if(!BuildDstIndex("/test"))
	{
		fprintf(out, "build index fail\n");
		return 1;
	}

	fprintf(out, "build success\n");
	gettimeofday(&aend, nullptr);
	diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - aend.tv_usec;
	fprintf(out, "build use time %ld\n", diff);

	//从文件当中读取一点点数据作为搜索
	char searchTarget[16] = { 0 };
	Myfile myfile;
	if (!myfile.init("/test", false))
	{
		fprintf(out, "file init fail");
		return 1;
	}

	fpos_t pos;
	pos.__pos = 1024;
	if (!myfile.read(pos, searchTarget, 16))
	{
		fprintf(out, "read fail");
		return 1;
	}
	gettimeofday(&start, nullptr);
	std::set<unsigned long long> result;
	if(!SearchFile("/test", searchTarget, 16, &result))
	{
		fprintf(out, "search fail \n");
		return 1;
	}

	fprintf(out, "search File success\n");
	gettimeofday(&aend, nullptr);
	diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - aend.tv_usec;
	fprintf(out, "search use time %ld\n", diff);

	//打开文件看看查找的字符串对不对。
	FILE* file = fopen("/test", "rb");
	if (file == nullptr)
	{
		fprintf(out, "file open error");
		return 1;
	}

	char buffer[16];

	for (auto& val : result)
	{
		fpos_t pos;
		pos.__pos = val;
		fsetpos(file, &pos);
		if (fread(buffer, 16, 1, file) != 1)
		{
			fclose(file);
			fprintf(out, "read file error filepos %llu", val);
			return 1;
		}

		//buffer[10] = '\0';
		//printf("file content %s \n", buffer);
		if (memcmp(buffer, searchTarget, 16))
		{
			fclose(file);
			fprintf(out, "search word pos not correct resultPos %llu result word %s", val, buffer);
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
	buildInex.init("/test", &index, &kvIndex);
	for (unsigned long i = 0; i < sizeof(key) / sizeof(key[0]); ++i)
	{
		if (!buildInex.addKV(key[i], val[i]))
		{
			fprintf(out, "add kv failed\n");
			return false;
		}
	}
	if (!buildInex.writeKvEveryCache())
	{
		fprintf(out, "write cache failed\n");
		return 1;
	}
	char kvIndexFile[4096];
	if (!getKVFilePath("/test", kvIndexFile))
	{
		fprintf(out, "get kv indexFile name failed\n");
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
			return 1;
		}
		if (mapLowerBoundKey != kvLowerBoundKey || mapUpperBoundKey != kvUpperBoundKey || mapLowerBoundValue != kvLowerBoundValue)
		{
			fprintf(out, "search value not right right lowerKey %llu, upperKey %llu, value %llu, find lowerKey %llu, upperKey %llu, value %llu", mapLowerBoundKey, mapUpperBoundKey, mapLowerBoundValue, kvLowerBoundKey, kvUpperBoundKey, kvLowerBoundValue);
			return 1;
		}
	}
	return 0;
}
