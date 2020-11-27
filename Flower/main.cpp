#include "interface.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main()
{

	char pPath[256] = { 0 };

	getcwd(pPath, 256);

	printf("filePath: %s\n", pPath);

	if(!BuildDstIndex("/test"))
	{
		printf("build index fail\n");
		return 1;
	}

	std::set<unsigned long long> result;
	if(!SearchFile("/test", "regulators", 10, &result))
	{
		printf("search fail \n");
		return 1;
	}

	//打开文件看看查找的字符串对不对。
	FILE* file = fopen("/test", "rb");
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

		buffer[10] = '\0';
		printf("file content %s \n", buffer);
		if (strcmp(buffer, "regulators"))
		{
			fclose(file);
			printf("search word pos not correct resultPos %llu result word %s", val, buffer);
			return 1;
		}
	}

	fclose(file);
	return 0;
}
