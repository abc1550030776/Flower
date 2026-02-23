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

bool Myfile::read(unsigned long long pos, void* data, size_t size)
{
	if (fseeko(file, (off_t)pos, SEEK_SET) != 0)
	{
		return false;
	}

	if (fread(data, size, 1, file) != 1)
	{
		return false;
	}

	return true;
}

bool Myfile::write(unsigned long long pos, void* data, size_t size)
{
	if (fseeko(file, (off_t)pos, SEEK_SET) != 0)
	{
		return false;
	}

	if (fwrite(data, size, 1, file) != 1)
	{
		return false;
	}
	return true;
}

bool Myfile::sync()
{
	if (fflush(file) != 0)
	{
		return false;
	}
#ifdef __APPLE__
	if (fsync(fileno(file)) == -1)
#else
	if (fdatasync(fileno(file)) == -1)
#endif
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
