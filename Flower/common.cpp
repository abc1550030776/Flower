#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "Myfile.h"
#include "SetWithLock.h"

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

bool AddFindPos(SetWithLock* resultSet, unsigned long long pos, char skipNum, Myfile& dstFile, const char* searchTarget, unsigned int targetLen)
{
	if (resultSet == nullptr)
	{
		return false;
	}

	if (searchTarget == nullptr)
	{
		return false;
	}
	//判断跳过的字节是否是负的来判断是否需要比较前面的几个字节
	if (skipNum < 0)
	{
		//有可能是最开头的匹配后面的那段这个结果跳过不加入
		if (pos == 0)
		{
			return true;
		}

		char absSkipNum = -skipNum;
		if (pos < absSkipNum)
		{
			return false;
		}
		if (absSkipNum >= 8)
		{
			return false;
		}
		if (absSkipNum > targetLen)
		{
			return false;
		}
		unsigned long long possiblePos = pos - absSkipNum;
		//从文件当中读取前面还没比较的部分比较全部相等了才算是找到
		unsigned char buffer[8];
		fpos_t filePos;
		filePos.__pos = possiblePos;
		if (!dstFile.read(filePos, buffer, absSkipNum))
		{
			return false;
		}

		for (char i = 0; i < absSkipNum; ++i)
		{
			if (buffer[i] != searchTarget[i])
			{
				return true;
			}
		}

		//这部分都一样的话就是全部一样了找到了一个位置
		resultSet->insert(possiblePos);
	}
	else
	{
		//找到的位置是跳过一部分以后再开始比较的所以已经匹配到结果直接加入就行了
		resultSet->insert(pos + skipNum);
	}
	return true;
}