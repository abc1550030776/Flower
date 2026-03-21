#include "interface.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include <vector>
#include <ctime>
#include "Index.h"
#include "common.h"
#include "BuildIndex.h"
#include "sys/time.h"
#include "Myfile.h"
#include "SearchContext.h"

static void cleanupTestFiles(const char* fileName)
{
	char idxFile[4096] = {0};
	char kvFile[4096] = {0};
	getIndexPath(fileName, idxFile);
	getKVFilePath(fileName, kvFile);
	remove(fileName);
	remove(idxFile);
	remove(kvFile);
}

bool testCrossLineSearch()
{
	const char* testFileName = "test_cross_line";
	const unsigned long lineCount = 200;

	//随机生成测试文件，记录每行起始偏移
	std::vector<unsigned long long> lineOffsets;
	unsigned int seed = (unsigned int)time(nullptr);

	FILE* genFile = fopen(testFileName, "wb");
	if (genFile == nullptr)
	{
		printf("failed to create test file\n");
		return false;
	}

	unsigned long long offset = 0;
	for (unsigned long i = 0; i < lineCount; ++i)
	{
		lineOffsets.push_back(offset);
		//每行长度20~100字节（不含换行符）
		seed = seed * 1103515245 + 12345;
		unsigned long lineLen = 20 + ((seed >> 16) % 81);
		for (unsigned long j = 0; j < lineLen; ++j)
		{
			seed = seed * 1103515245 + 12345;
			//可打印字符 0x21~0x7E，避免产生 \n
			unsigned char ch = 0x21 + ((seed >> 16) % 94);
			fputc(ch, genFile);
		}
		fputc('\n', genFile);
		offset += lineLen + 1;
	}
	fclose(genFile);

	printf("generated test file: %llu bytes, %lu lines\n", offset, lineCount);

	//构建索引（含行索引）
	struct timeval start, aend;
	gettimeofday(&start, nullptr);
	if (!BuildDstIndex(testFileName, true, '\n'))
	{
		printf("build index fail\n");
		cleanupTestFiles(testFileName);
		return false;
	}
	gettimeofday(&aend, nullptr);
	unsigned long diff = 1000000 * (aend.tv_sec - start.tv_sec) + aend.tv_usec - start.tv_usec;
	printf("build index use time %ld us\n", diff);

	SearchContext searchContext;
	if (!searchContext.init(testFileName, 0, true))
	{
		printf("search context init fail\n");
		cleanupTestFiles(testFileName);
		return false;
	}

	//读取文件内容用于构造搜索目标
	Myfile myfile;
	if (!myfile.init(testFileName, false))
	{
		printf("file init fail\n");
		cleanupTestFiles(testFileName);
		return false;
	}

	//用例A：单行搜索
	{
		//取第10行中间5个字节
		unsigned long testLine = 10;
		unsigned long long lineStart = lineOffsets[testLine];
		unsigned long long nextLineStart = lineOffsets[testLine + 1];
		unsigned long long lineLen = nextLineStart - lineStart - 1; //不含\n
		unsigned long long readStart = lineStart + lineLen / 4;
		unsigned int readLen = 5;

		char target[5];
		unsigned long long readPos = readStart;
		if (!myfile.read(readPos, target, readLen))
		{
			printf("用例A：read fail\n");
			cleanupTestFiles(testFileName);
			return false;
		}

		ResultMap result;
		if (!searchContext.search(target, readLen, &result))
		{
			printf("用例A：search fail\n");
			cleanupTestFiles(testFileName);
			return false;
		}

		//在结果中找到我们期望的那个匹配
		auto it = result.find(readStart);
		if (it == result.end())
		{
			printf("用例A：expected position %llu not found in results\n", readStart);
			cleanupTestFiles(testFileName);
			return false;
		}

		unsigned long long expectStartLine = testLine;
		unsigned long long expectStartCol = readStart - lineStart;
		unsigned long long expectEndLine = expectStartLine;
		unsigned long long expectEndCol = readStart + readLen - 1 - lineStart;

		if (it->second.GetLineNum() != expectStartLine ||
			it->second.GetColumnNum() != expectStartCol ||
			it->second.GetEndLineNum() != expectEndLine ||
			it->second.GetEndColumnNum() != expectEndCol)
		{
			printf("用例A FAIL: expect line %llu col %llu endLine %llu endCol %llu, got line %llu col %llu endLine %llu endCol %llu\n",
				expectStartLine, expectStartCol, expectEndLine, expectEndCol,
				it->second.GetLineNum(), it->second.GetColumnNum(),
				it->second.GetEndLineNum(), it->second.GetEndColumnNum());
			cleanupTestFiles(testFileName);
			return false;
		}
		printf("用例A PASSED (单行搜索)\n");
	}

	//用例B：跨两行搜索
	{
		//从第50行中间读到第51行中间
		unsigned long testLine = 50;
		unsigned long long lineStart = lineOffsets[testLine];
		unsigned long long nextLineStart = lineOffsets[testLine + 1];
		unsigned long long nextNextLineStart = lineOffsets[testLine + 2];
		unsigned long long lineLen = nextLineStart - lineStart - 1;
		unsigned long long nextLineLen = nextNextLineStart - nextLineStart - 1;

		unsigned long long readStart = lineStart + lineLen / 2;
		unsigned long long readEnd = nextLineStart + nextLineLen / 2;
		unsigned int readLen = (unsigned int)(readEnd - readStart + 1);

		char* target = new char[readLen];
		unsigned long long readPos = readStart;
		if (!myfile.read(readPos, target, readLen))
		{
			printf("用例B：read fail\n");
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}

		ResultMap result;
		if (!searchContext.search(target, readLen, &result))
		{
			printf("用例B：search fail\n");
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}

		auto it = result.find(readStart);
		if (it == result.end())
		{
			printf("用例B：expected position %llu not found in results\n", readStart);
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}

		unsigned long long expectStartLine = testLine;
		unsigned long long expectStartCol = readStart - lineStart;
		unsigned long long expectEndLine = testLine + 1;
		unsigned long long expectEndCol = readEnd - nextLineStart;

		if (it->second.GetLineNum() != expectStartLine ||
			it->second.GetColumnNum() != expectStartCol ||
			it->second.GetEndLineNum() != expectEndLine ||
			it->second.GetEndColumnNum() != expectEndCol)
		{
			printf("用例B FAIL: expect line %llu col %llu endLine %llu endCol %llu, got line %llu col %llu endLine %llu endCol %llu\n",
				expectStartLine, expectStartCol, expectEndLine, expectEndCol,
				it->second.GetLineNum(), it->second.GetColumnNum(),
				it->second.GetEndLineNum(), it->second.GetEndColumnNum());
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}
		printf("用例B PASSED (跨两行搜索)\n");
		delete[] target;
	}

	//用例C：跨多行搜索（跨4行）
	{
		unsigned long testLine = 100;
		unsigned long long readStart = lineOffsets[testLine] + 3;
		unsigned long long readEnd = lineOffsets[testLine + 4] + 3;
		unsigned int readLen = (unsigned int)(readEnd - readStart + 1);

		char* target = new char[readLen];
		unsigned long long readPos = readStart;
		if (!myfile.read(readPos, target, readLen))
		{
			printf("用例C：read fail\n");
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}

		ResultMap result;
		if (!searchContext.search(target, readLen, &result))
		{
			printf("用例C：search fail\n");
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}

		auto it = result.find(readStart);
		if (it == result.end())
		{
			printf("用例C：expected position %llu not found in results\n", readStart);
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}

		unsigned long long expectStartLine = testLine;
		unsigned long long expectStartCol = readStart - lineOffsets[testLine];
		unsigned long long expectEndLine = testLine + 4;
		unsigned long long expectEndCol = readEnd - lineOffsets[testLine + 4];

		if (it->second.GetLineNum() != expectStartLine ||
			it->second.GetColumnNum() != expectStartCol ||
			it->second.GetEndLineNum() != expectEndLine ||
			it->second.GetEndColumnNum() != expectEndCol)
		{
			printf("用例C FAIL: expect line %llu col %llu endLine %llu endCol %llu, got line %llu col %llu endLine %llu endCol %llu\n",
				expectStartLine, expectStartCol, expectEndLine, expectEndCol,
				it->second.GetLineNum(), it->second.GetColumnNum(),
				it->second.GetEndLineNum(), it->second.GetEndColumnNum());
			delete[] target;
			cleanupTestFiles(testFileName);
			return false;
		}
		printf("用例C PASSED (跨多行搜索)\n");
		delete[] target;
	}

	cleanupTestFiles(testFileName);
	printf("testCrossLineSearch ALL PASSED\n");
	return true;
}

int main()
{
	if (!testCrossLineSearch())
	{
		printf("CROSS LINE SEARCH TEST FAILED\n");
		return 1;
	}
	return 0;
}
