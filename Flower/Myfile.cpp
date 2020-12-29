#include "Myfile.h"
#include <stdlib.h>
#include <unistd.h>

Myfile::Myfile()
{
	file = NULL;
}

bool Myfile::init(const char* fileName, bool createIfNExist)
{
	file = fopen(fileName, "rb+");
	if (file == NULL)
	{
		if (!createIfNExist)
		{
			return false;
		}
	}
	else
	{
		return true;
	}
	file = fopen(fileName, "wb+");
	if (file == NULL)
	{
		return false;
	}

	return true;
}

bool Myfile::read(fpos_t pos, void* data, size_t size)
{
	int flag = fsetpos(file, &pos);
	if (flag != 0)
	{
		return false;
	}

	if (fread(data, size, 1, file) != 1)
	{
		return false;
	}

	return true;
}

bool Myfile::write(fpos_t pos, void* data, size_t size)
{
	int flag = fsetpos(file, &pos);
	if (flag != 0)
	{
		//有可能写入的位置是在文件的最后面以后这个时候往后面补充足够的数据
		fseek(file, 0, SEEK_END);
		fpos_t endPos;
		fgetpos(file, &endPos);
		if (endPos.__pos >= pos.__pos)
		{
			return false;
		}

		size_t appendSize = pos.__pos - endPos.__pos;
		char* p = (char*)malloc(appendSize);
		if (p == NULL)
		{
			return false;
		}
		if (fwrite(p, appendSize, 1, file) != 1)
		{
			free(p);
			return false;
		}
		free(p);
	}

	if (fwrite(data, size, 1, file) != 1)
	{
		return false;
	}
	return true;
}

size_t Myfile::readTail(fpos_t pos, void* data, size_t size)
{
	int flag = fsetpos(file, &pos);
	if (flag != 0)
	{
		return 0;
	}

	return fread(data, 1, size, file);
}

bool Myfile::sync()
{
	if (fdatasync(fileno(file)) == -1)
	{
		return false;
	}
	return true;
}

Myfile::~Myfile()
{
	if (file != NULL)
	{
		fclose(file);
		file = NULL;
	}
}
