#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "Myfile.h"
#include "SetWithLock.h"
#include "MemoryPool.h"
#include "IndexNode.h"

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
	unsigned long totalMem = total;  // 系统总内存 (KB)
	fgets(buff, sizeof(buff), fd);
	fgets(buff, sizeof(buff), fd);
	sscanf(buff, "%s %lu %s\n", name, &total, name2);
	unsigned long availableMem = total;  // 系统可用内存 (KB)
	fclose(fd);
	
	// 获取内存池的空闲和总内存
	IndexNodePoolManager& poolManager = IndexNodePoolManager::getInstance();
	
	// 计算每个内存池的空闲和总内存（字节）
	size_t poolFreeBytes = 0;
	size_t poolTotalBytes = 0;
	
	// TypeOne 内存池
	poolFreeBytes += poolManager.getPoolTypeOne().getFreeCount() * sizeof(IndexNodeTypeOne);
	poolTotalBytes += poolManager.getPoolTypeOne().getTotalCount() * sizeof(IndexNodeTypeOne);
	
	// TypeTwo 内存池
	poolFreeBytes += poolManager.getPoolTypeTwo().getFreeCount() * sizeof(IndexNodeTypeTwo);
	poolTotalBytes += poolManager.getPoolTypeTwo().getTotalCount() * sizeof(IndexNodeTypeTwo);
	
	// TypeThree 内存池
	poolFreeBytes += poolManager.getPoolTypeThree().getFreeCount() * sizeof(IndexNodeTypeThree);
	poolTotalBytes += poolManager.getPoolTypeThree().getTotalCount() * sizeof(IndexNodeTypeThree);
	
	// TypeFour 内存池
	poolFreeBytes += poolManager.getPoolTypeFour().getFreeCount() * sizeof(IndexNodeTypeFour);
	poolTotalBytes += poolManager.getPoolTypeFour().getTotalCount() * sizeof(IndexNodeTypeFour);
	
	// 转换内存池内存为 KB
	unsigned long poolFreeKB = poolFreeBytes / 1024;
	unsigned long poolTotalKB = poolTotalBytes / 1024;
	
	// 计算内存池占用系统内存的比例
	float poolMemoryRatio = float(poolTotalKB) / float(totalMem);
	
	// 安全策略：
	// 1. 基础可用内存 = 系统可用内存 + 内存池空闲内存
	// 2. 如果内存池占用系统内存过多（超过50%），需要惩罚因子来降低可用率
	//    这样可以促使系统在内存池过大时清理缓存，避免程序崩溃
	// 3. 惩罚因子随着内存池占用比例增加而增加
	
	float baseAvailableRate = float(availableMem + poolFreeKB) / float(totalMem);
	
	// 如果内存池占用超过50%系统内存，应用惩罚因子
	if (poolMemoryRatio > 0.5f)
	{
		// 惩罚因子：内存池占用越多，可用率越低
		// 当内存池占用50%时，惩罚系数为1.0（无惩罚）
		// 当内存池占用70%时，惩罚系数约为0.7
		// 当内存池占用90%时，惩罚系数约为0.3
		float penaltyFactor = (1.0f - poolMemoryRatio) / 0.5f;
		if (penaltyFactor < 0.1f) penaltyFactor = 0.1f; // 最小保留10%
		
		baseAvailableRate *= penaltyFactor;
	}
	
	return baseAvailableRate;
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
		unsigned long long filePos;
		filePos = possiblePos;
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