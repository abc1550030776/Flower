#include "interface.h"
#include <stdio.h>
#include <string.h>

int main()
{
	BuildDstIndex("test");

	std::set<unsigned long long> result;
	SearchFile("test", "word", 4, &result);

	//打开文件看看查找的字符串对不对。
	FILE* file = fopen("test", "rb");
	if (file == nullptr)
	{
		printf("file open error");
		return 1;
	}

	char buffer[5];

	for (auto& val : result)
	{
		fpos_t pos;
		pos.__pos = val;
		fsetpos(file, &pos);
		if (fread(buffer, 4, 1, file) != 1)
		{
			fclose(file);
			printf("read file error filepos %llu", val);
			return 1;
		}

		buffer[5] = '\0';
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
