#include "Myfile.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

Myfile::Myfile()
{
	fd = -1;
}

bool Myfile::init(const char* fileName, bool createIfNExist)
{
	fd = open(fileName, O_RDWR);
	if (fd >= 0)
	{
		return true;
	}

	if (!createIfNExist)
	{
		return false;
	}

	fd = open(fileName, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		return false;
	}

	return true;
}

bool Myfile::read(unsigned long long pos, void* data, size_t size)
{
	size_t totalRead = 0;
	while (totalRead < size)
	{
		ssize_t n = pread(fd, (char*)data + totalRead, size - totalRead, (off_t)(pos + totalRead));
		if (n <= 0)
		{
			return false;
		}
		totalRead += n;
	}
	return true;
}

bool Myfile::write(unsigned long long pos, void* data, size_t size)
{
	size_t totalWritten = 0;
	while (totalWritten < size)
	{
		ssize_t n = pwrite(fd, (const char*)data + totalWritten, size - totalWritten, (off_t)(pos + totalWritten));
		if (n <= 0)
		{
			return false;
		}
		totalWritten += n;
	}
	return true;
}

bool Myfile::sync()
{
#ifdef __APPLE__
	if (fsync(fd) == -1)
#else
	if (fdatasync(fd) == -1)
#endif
	{
		return false;
	}
	return true;
}

Myfile::~Myfile()
{
	if (fd >= 0)
	{
		close(fd);
		fd = -1;
	}
}
