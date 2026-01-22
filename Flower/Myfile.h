#pragma once
#include <stdio.h>
class Myfile
{
public:
	Myfile();
	bool init(const char* fileName, bool createIfNExist = true);
	bool read(unsigned long long pos, void* data, size_t size);
	bool write(unsigned long long pos, void* data, size_t size);
	bool sync();
	~Myfile();
	Myfile(const Myfile&) = delete;
	Myfile& operator=(const Myfile&) = delete;
private:
	FILE* file;
};
