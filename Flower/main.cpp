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

bool testMultiThread()
{
	//创建一个刚好超过8MB的测试文件（2个段，最少化测试时间）
	const unsigned long long testFileSize = 8 * 1024 * 1024 + 8 * 1024; // 8MB + 8KB = 2个段
	const char* testFileName = "test_file_mt";

	//快速生成测试文件：用大块写入
	FILE* genFile = fopen(testFileName, "wb");
	if (genFile == nullptr)
	{
		printf("failed to create test file\n");
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	//使用伪随机数据，避免短周期导致mergeNode陷入超长比较
	const size_t blockSize = 4096;
	unsigned char block[blockSize];
	unsigned long long written = 0;
	unsigned int seed = 12345;
	while (written < testFileSize)
	{
		for (size_t i = 0; i < blockSize; ++i)
		{
			seed = seed * 1103515245 + 12345;
			block[i] = (unsigned char)((seed >> 16) & 0xFF);
		}
		size_t toWrite = blockSize;
		if (written + toWrite > testFileSize) toWrite = (size_t)(testFileSize - written);
		fwrite(block, 1, toWrite, genFile);
		written += toWrite;
	}
	fclose(genFile);
	struct timeval start, aend;
	gettimeofday(&start, nullptr);

	//构建索引（多线程路径：>8MB，应该产生2个段，不构建行索引以加快测试速度）
	if (!BuildDstIndex(testFileName, false))
	{
		printf("multi-thread build index fail\n");
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}

	gettimeofday(&aend, nullptr);
	unsigned long diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - start.tv_usec;
	printf("multi-thread build use time %ld us\n", diff);

	//搜索测试：从文件中读取一段数据作为搜索目标
	const unsigned long searchStrLen = 64;
	char searchTarget[searchStrLen];
	Myfile myfile;
	if (!myfile.init(testFileName, false))
	{
		printf("file init fail\n");
		printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
	}
	//从多个不同段读取搜索目标进行测试
	unsigned long long testPositions[] = { 1024, 1024 * 1024, 9 * 1024 * 1024 }; // 段0, 段1, 段1
	for (int t = 0; t < 3; ++t)
	{
		unsigned long long pos = testPositions[t];
		if (pos + searchStrLen > testFileSize) continue;
		if (!myfile.read(pos, searchTarget, searchStrLen))
		{
			printf("read fail at pos %llu\n", pos);
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}

		SearchContext searchContext;
		searchContext.init(testFileName, 0, false);

		std::set<unsigned long long> result;
		if (!searchContext.search(searchTarget, searchStrLen, &result))
		{
			printf("search fail for pos %llu\n", pos);
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
		printf("search from pos %llu: found %lu results\n", pos, result.size());

		//验证搜索结果正确性
		FILE* verifyFile = fopen(testFileName, "rb");
		if (verifyFile == nullptr)
		{
			printf("failed to open file for verify\n");
			printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
		}
		char buffer[searchStrLen];
		for (auto val : result)
		{
			fseeko(verifyFile, (off_t)val, SEEK_SET);
			if (fread(buffer, searchStrLen, 1, verifyFile) != 1)
			{
				printf("read file error filepos %llu\n", val);
				fclose(verifyFile);
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
			if (memcmp(buffer, searchTarget, searchStrLen))
			{
				printf("search result mismatch at pos %llu\n", val);
				fclose(verifyFile);
				printf("failed at %s:%d\n", __FILE__, __LINE__); return false;
			}
		}
		fclose(verifyFile);
		printf("  all results verified correct\n");
	}

	//清理临时文件
	remove(testFileName);
	char idxFile[4096] = {0};
	char kvFile[4096] = {0};
	getIndexPath(testFileName, idxFile);
	getKVFilePath(testFileName, kvFile);
	remove(idxFile);
	remove(kvFile);

	printf("multi-thread test PASSED\n");
	return true;
}

int main()
{
	//printf("=== Multi-thread build test ===\n");
	//if (!testMultiThread())
	//{
	//	printf("MULTI-THREAD TEST FAILED\n");
	//	return 11;
	//}

	FILE* out = fopen("out", "w");
	if (out == nullptr)
	{
		printf("file open error");
		return 12;
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
	if(!BuildDstIndex("test_file", true))
	{
		fprintf(out, "build index fail\n");
		fclose(out);
		return 13;
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
	if (!myfile.init("test_file", false))
	{
		fprintf(out, "file init fail");
		fclose(out);
		return 14;
	}

	unsigned long long pos;
	pos = 1024;
	if (!myfile.read(pos, searchTarget, searchStrLen))
	{
		fprintf(out, "read fail");
		fclose(out);
		return 15;
	}
	SearchContext searchContext;
	searchContext.init("test_file", 0, true);
	gettimeofday(&start, nullptr);
	ResultMap result;
	if(!searchContext.search(searchTarget, searchStrLen, &result))
	{
		fprintf(out, "search fail \n");
		fclose(out);
		return 16;
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
	FILE* file = fopen("test_file", "rb");
	if (file == nullptr)
	{
		fprintf(out, "file open error");
		fclose(out);
		return 17;
	}

	char buffer[searchStrLen];

	for (auto& val : result)
	{
		unsigned long long pos;
		pos = val.first;
		fseeko(file, (off_t)pos, SEEK_SET);
		if (fread(buffer, searchStrLen, 1, file) != 1)
		{
			fclose(file);
			fprintf(out, "read file error filepos %llu", val.first);
			fclose(out);
			return 18;
		}

		//buffer[10] = '\0';
		//printf("file content %s \n", buffer);
		if (memcmp(buffer, searchTarget, searchStrLen))
		{
			fclose(file);
			fprintf(out, "search word pos not correct resultPos %llu result word %s", val.first, buffer);
			fclose(out);
			return 19;
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
		return 20;
	}

	gettimeofday(&aend, nullptr);
	diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - start.tv_usec;
	fprintf(out, "search use time %ld\n", diff);
	fclose(out);
	out = fopen("out", "a");

	for (auto& val : result)
	{
		unsigned long long pos;
		pos = val.first;
		fseeko(file, (off_t)pos, SEEK_SET);
		if (fread(buffer, searchStrLen, 1, file) != 1)
		{
			fclose(file);
			fprintf(out, "read file error filepos %llu", val.first);
			fclose(out);
			return 21;
		}

		//buffer[10] = '\0';
		//printf("file content %s \n", buffer);
		if (memcmp(buffer, searchTarget, searchStrLen))
		{
			fclose(file);
			fprintf(out, "search word pos not correct resultPos %llu result word %s", val.first, buffer);
			fclose(out);
			return 22;
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

	printf("Before KV index\n"); fflush(stdout); Index index(USE_TYPE_BUILD);
	Index kvIndex(USE_TYPE_BUILD);
	BuildIndex buildInex;
	buildInex.init("testkv", &index, &kvIndex);
	for (unsigned long i = 0; i < sizeof(key) / sizeof(key[0]); ++i)
	{
		printf("Adding KV %d\n", i); fflush(stdout); if (!buildInex.addKV(key[i], val[i]))
		{
			fprintf(out, "add kv failed\n");
			fclose(out);
			return 23;
		}
	}
	printf("Before writeKvEveryCache\n"); fflush(stdout); if (!buildInex.writeKvEveryCache())
	{
		fprintf(out, "write cache failed\n");
		fclose(out);
		return 24;
	}
	char kvIndexFile[4096];
	if (!getKVFilePath("testkv", kvIndexFile))
	{
		fprintf(out, "get kv indexFile name failed\n");
		fclose(out);
		return 25;
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
			fprintf(out, "search kv failed search key %llu\n", test[i]); printf("KV SEARCH FAIL 1\n");
			fclose(out);
			return 26;
		}
		if (mapLowerBoundKey != kvLowerBoundKey || mapUpperBoundKey != kvUpperBoundKey || mapLowerBoundValue != kvLowerBoundValue)
		{
			printf("KV SEARCH FAIL 2\n"); fprintf(out, "search value not right right lowerKey %llu, upperKey %llu, value %llu, find lowerKey %llu, upperKey %llu, value %llu", mapLowerBoundKey, mapUpperBoundKey, mapLowerBoundValue, kvLowerBoundKey, kvUpperBoundKey, kvLowerBoundValue);
			fclose(out);
			return 27;
		}
	}
	fclose(out);
	return 0;
}
