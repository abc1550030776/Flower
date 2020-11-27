#include "interface.h"
#include <stdio.h>
#include <string.h>

int main()
{
	if(!BuildDstIndex("test"))
	{
		printf("build index fail\n");
		return 1;
	}

	std::set<unsigned long long> result;
	if(!SearchFile("test", "regulators", 10, &result))
	{
		printf("search fail \n");
		return 1;
	}

	//打开文件看看查找的字符串对不对。
	FILE* file = fopen("test", "rb");
	if (file == nullptr)
	{
		printf("file open error");
		return 1;
	}

	char buffer[11];

	for (auto& val : result)
	{
		fpos_t pos;
		pos.__pos = val;
		fsetpos(file, &pos);
		if (fread(buffer, 10, 1, file) != 1)
		{
			fclose(file);
			printf("read file error filepos %llu", val);
			return 1;
		}

		printf("file content %s", buffer);
		buffer[10] = '\0';
		if (strcmp(buffer, "word"))
		{
			fclose(file);
			printf("search word pos not correct resultPos %llu result word %s", val, buffer);
			return 1;
		}
	}

	fclose(file);
	return 0;
}
