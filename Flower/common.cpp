#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

bool getIndexPath(const char* dstFilePath, char* indexPath)
{
	//判断路劲长度加上后缀了以后是否会超过最长长度
	size_t len = strlen(dstFilePath);
	if (len + 5 > 4096)
	{
		return false;
	}

	strcpy(indexPath, dstFilePath);

	strcpy(indexPath + len, ".idx");

	return true;
}

bool compareTwoType(const unsigned char leftType, const unsigned char rightType)
{
	//比较孩子节点key值大小,左边小的化返回true
	if (leftType > rightType)
	{
		return true;
	}
	return false;
}

bool getKVFilePath(const char* dstFilePath, char* kVFilePath)
{
	size_t len = strlen(dstFilePath);
	if (len + 5 > 4096)
	{
		return false;
	}

	strcpy(kVFilePath, dstFilePath);

	strcpy(kVFilePath + len, ".kvi");

	return true;
}

unsigned char swiftBigLittleEnd(unsigned char value)
{
	return value;
}

unsigned short swiftBigLittleEnd(unsigned short value)
{
	return (unsigned short)(((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8));
}

unsigned int swiftBigLittleEnd(unsigned int value)
{
	return ((value & 0x000000FF) << 24) | ((value & 0x0000FF00) << 8) | ((value & 0x00FF0000) >> 8) | ((value & 0xFF000000) >> 24);
}

unsigned long long swiftBigLittleEnd(unsigned long long value)
{
	unsigned long long highValue = (unsigned long long)swiftBigLittleEnd((unsigned int)value);
	unsigned long long lowValue = (unsigned long long)swiftBigLittleEnd((unsigned int)(value >> 32));
	return lowValue + (highValue << 32);
}

float getAvailableMemRate()
{
	FILE* fd;
	fd = fopen("/proc/meminfo", "r");
	if (fd == NULL)
	{
		return 0;
	}
	char buff[256];
	fgets(buff, sizeof(buff), fd);
	char name[20];
	unsigned long total;
	char name2[20];
	sscanf(buff, "%s %lu %s\n", name, &total, name2);
	unsigned long totalMem = total;
	fgets(buff, sizeof(buff), fd);
	fgets(buff, sizeof(buff), fd);
	sscanf(buff, "%s %lu %s\n", name, &total, name2);
	fclose(fd);
	return float(total) / float(totalMem);
}

bool FlwPrintf(const char* fileName, const char* format, ...)
{
	FILE* fd = fopen(fileName, "a");
	if (fd == nullptr)
	{
		return false;
	}

	va_list valist;
	va_start(valist, format);
	if (vfprintf(fd, format, valist) == -1)
	{
		va_end(valist);
		fclose(fd);
		return false;
	}
	va_end(valist);
	fclose(fd);
	return true;
}
