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

float getSystemMemRate()
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
	
	// 返回系统可用内存比例（不包括内存池）
	return float(availableMem) / float(totalMem);
}

float getAvailableMemRate(IndexNodePoolManager& poolManager)
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
	
	// 计算该实例内存池的空闲内存（使用传入的poolManager参数）
	
	// 计算每个内存池的空闲内存（字节）
	size_t poolFreeBytes = 0;
	
	// TypeOne 内存池
	poolFreeBytes += poolManager.getPoolTypeOne().getFreeCount() * sizeof(IndexNodeTypeOne);
	
	// TypeTwo 内存池
	poolFreeBytes += poolManager.getPoolTypeTwo().getFreeCount() * sizeof(IndexNodeTypeTwo);
	
	// TypeThree 内存池
	poolFreeBytes += poolManager.getPoolTypeThree().getFreeCount() * sizeof(IndexNodeTypeThree);
	
	// TypeFour 内存池
	poolFreeBytes += poolManager.getPoolTypeFour().getFreeCount() * sizeof(IndexNodeTypeFour);
	
	// 转换内存池内存为 KB
	unsigned long poolFreeKB = poolFreeBytes / 1024;
	
	// 计算基础可用内存比例
	float baseAvailableRate = float(availableMem + poolFreeKB) / float(totalMem);
	
	// 计算系统剩余内存比例（不包括内存池空闲部分）
	float systemAvailableRate = float(availableMem) / float(totalMem);
	
	// 安全策略：如果系统剩余内存过低，应用惩罚因子
	// 这样可以避免内存池占用过多导致系统内存不足而崩溃
	// 
	// 惩罚阈值和因子：
	// - 系统剩余 > 30%: 无惩罚，正常使用内存池空闲内存
	// - 系统剩余 20-30%: 轻度惩罚
	// - 系统剩余 10-20%: 中度惩罚
	// - 系统剩余 < 10%: 重度惩罚
	
	if (systemAvailableRate < PENALTY_THRESHOLD_LIGHT)
	{
		// 系统剩余内存不足30%时，需要应用惩罚因子
		float penaltyFactor;
		
		if (systemAvailableRate >= PENALTY_THRESHOLD_MEDIUM)
		{
			// 20-30%: 线性惩罚从1.0降到0.5
			float range = PENALTY_THRESHOLD_LIGHT - PENALTY_THRESHOLD_MEDIUM;  // 0.1
			float factorRange = PENALTY_FACTOR_LIGHT_MAX - PENALTY_FACTOR_LIGHT_MIN;  // 0.5
			penaltyFactor = PENALTY_FACTOR_LIGHT_MIN + (systemAvailableRate - PENALTY_THRESHOLD_MEDIUM) / range * factorRange;
		}
		else if (systemAvailableRate >= PENALTY_THRESHOLD_HEAVY)
		{
			// 10-20%: 线性惩罚从0.5降到0.2
			float range = PENALTY_THRESHOLD_MEDIUM - PENALTY_THRESHOLD_HEAVY;  // 0.1
			float factorRange = PENALTY_FACTOR_MEDIUM_MAX - PENALTY_FACTOR_MEDIUM_MIN;  // 0.3
			penaltyFactor = PENALTY_FACTOR_MEDIUM_MIN + (systemAvailableRate - PENALTY_THRESHOLD_HEAVY) / range * factorRange;
		}
		else
		{
			// < 10%: 强惩罚，固定为0.1
			penaltyFactor = PENALTY_FACTOR_HEAVY;
		}
		
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