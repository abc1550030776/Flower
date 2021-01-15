#pragma once
#include <stdio.h>
class Myfile
{
public:
	Myfile();
	bool init(const char* fileName, bool createIfNExist = true);
	bool read(fpos_t pos, void* data, size_t size);
	bool write(fpos_t pos, void* data, size_t size);
	bool sync();
	~Myfile();
private:
	FILE* file;
};
