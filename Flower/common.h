#pragma once
#include <cstdint>
#include <cstring>

class SetWithLock;
class Myfile;
class IndexNodePoolManager;

#ifndef FLOWER_SIZE_PER_INDEX_FILE_GRID
#define FLOWER_SIZE_PER_INDEX_FILE_GRID 128
#endif
#ifndef FLOWER_MAX_SIZE_PER_INDEX_NODE
#define FLOWER_MAX_SIZE_PER_INDEX_NODE (20 * 1024)
#endif
#ifndef FLOWER_DST_SIZE_PER_ROOT
#define FLOWER_DST_SIZE_PER_ROOT (8 * 1024 * 1024)
#endif
#ifndef FLOWER_KV_CUT_NODE_SIZE_THRESHOLD
#define FLOWER_KV_CUT_NODE_SIZE_THRESHOLD 4096
#endif
#ifndef FLOWER_EMERGENCY_CLEANUP_THRESHOLD
#define FLOWER_EMERGENCY_CLEANUP_THRESHOLD 0.1f
#endif
#ifndef FLOWER_PARTIAL_CLEANUP_THRESHOLD_SEARCH
#define FLOWER_PARTIAL_CLEANUP_THRESHOLD_SEARCH 0.2f
#endif
#ifndef FLOWER_PARTIAL_CLEANUP_THRESHOLD_BUILD
#define FLOWER_PARTIAL_CLEANUP_THRESHOLD_BUILD 0.4f
#endif
#ifndef FLOWER_PARTIAL_CLEANUP_RATIO_BUILD
#define FLOWER_PARTIAL_CLEANUP_RATIO_BUILD 0.7f
#endif
#ifndef FLOWER_PENALTY_THRESHOLD_LIGHT
#define FLOWER_PENALTY_THRESHOLD_LIGHT 0.3f
#endif
#ifndef FLOWER_PENALTY_THRESHOLD_MEDIUM
#define FLOWER_PENALTY_THRESHOLD_MEDIUM 0.2f
#endif
#ifndef FLOWER_PENALTY_THRESHOLD_HEAVY
#define FLOWER_PENALTY_THRESHOLD_HEAVY 0.1f
#endif
#ifndef FLOWER_PENALTY_FACTOR_HEAVY
#define FLOWER_PENALTY_FACTOR_HEAVY 0.1f
#endif
#ifndef FLOWER_PENALTY_FACTOR_MEDIUM_MIN
#define FLOWER_PENALTY_FACTOR_MEDIUM_MIN 0.2f
#endif
#ifndef FLOWER_PENALTY_FACTOR_MEDIUM_MAX
#define FLOWER_PENALTY_FACTOR_MEDIUM_MAX 0.5f
#endif
#ifndef FLOWER_PENALTY_FACTOR_LIGHT_MIN
#define FLOWER_PENALTY_FACTOR_LIGHT_MIN 0.5f
#endif
#ifndef FLOWER_PENALTY_FACTOR_LIGHT_MAX
#define FLOWER_PENALTY_FACTOR_LIGHT_MAX 1.0f
#endif
#ifndef FLOWER_MEM_INFO_CACHE_INTERVAL
#define FLOWER_MEM_INFO_CACHE_INTERVAL 0.5f
#endif

const unsigned short SIZE_PER_INDEX_FILE_GRID = FLOWER_SIZE_PER_INDEX_FILE_GRID;			//索引文件里面一个格子的大小、每个节点可能占用多个格子

const unsigned short MAX_SIZE_PER_INDEX_NODE = FLOWER_MAX_SIZE_PER_INDEX_NODE;		//每个索引节点在索引文件里面最多占用的大小

const unsigned int DST_SIZE_PER_ROOT = FLOWER_DST_SIZE_PER_ROOT;					//多少个目标文件字节的数据构建一个一部分的根节点

const unsigned short KV_CUT_NODE_SIZE_THRESHOLD = FLOWER_KV_CUT_NODE_SIZE_THRESHOLD;				//KV构建路径cutNodeSize限流阈值

// ========== 内存管理阈值常量 ==========
// 紧急清理阈值：系统内存低于此值时清空所有缓存和内存池
const float EMERGENCY_CLEANUP_THRESHOLD = FLOWER_EMERGENCY_CLEANUP_THRESHOLD;  // 10%

// 部分清理阈值：组合内存低于此值时进行部分清理
const float PARTIAL_CLEANUP_THRESHOLD_SEARCH = FLOWER_PARTIAL_CLEANUP_THRESHOLD_SEARCH;   // 20% (搜索模式)
const float PARTIAL_CLEANUP_THRESHOLD_BUILD = FLOWER_PARTIAL_CLEANUP_THRESHOLD_BUILD;    // 40% (构建模式)

// 部分清理比例
const float PARTIAL_CLEANUP_RATIO_BUILD = FLOWER_PARTIAL_CLEANUP_RATIO_BUILD;  // 70% (构建模式清理比例)

// 惩罚因子阈值：系统内存低于这些阈值时应用不同程度的惩罚
const float PENALTY_THRESHOLD_LIGHT = FLOWER_PENALTY_THRESHOLD_LIGHT;   // 30% (开始应用惩罚)
const float PENALTY_THRESHOLD_MEDIUM = FLOWER_PENALTY_THRESHOLD_MEDIUM;  // 20% (中度惩罚)
const float PENALTY_THRESHOLD_HEAVY = FLOWER_PENALTY_THRESHOLD_HEAVY;   // 10% (重度惩罚)

// 惩罚因子值
const float PENALTY_FACTOR_HEAVY = FLOWER_PENALTY_FACTOR_HEAVY;      // 重度惩罚因子
const float PENALTY_FACTOR_MEDIUM_MIN = FLOWER_PENALTY_FACTOR_MEDIUM_MIN; // 中度惩罚因子最小值
const float PENALTY_FACTOR_MEDIUM_MAX = FLOWER_PENALTY_FACTOR_MEDIUM_MAX; // 中度惩罚因子最大值
const float PENALTY_FACTOR_LIGHT_MIN = FLOWER_PENALTY_FACTOR_LIGHT_MIN;  // 轻度惩罚因子最小值
const float PENALTY_FACTOR_LIGHT_MAX = FLOWER_PENALTY_FACTOR_LIGHT_MAX;  // 轻度惩罚因子最大值

// 内存信息缓存刷新间隔（秒）
// 避免频繁读取 /proc/meminfo 文件，减少 I/O 开销
const float MEM_INFO_CACHE_INTERVAL = FLOWER_MEM_INFO_CACHE_INTERVAL;  // 0.5秒

bool getIndexPath(const char* dstFilePath, char* indexPath);

bool compareTwoType(const unsigned char leftType, const unsigned char rightType);

bool getKVFilePath(const char* dstFilePath, char* kVFilePath);

unsigned char swiftBigLittleEnd(unsigned char value);

unsigned short swiftBigLittleEnd(unsigned short value);

unsigned int swiftBigLittleEnd(unsigned int value);

unsigned long long swiftBigLittleEnd(unsigned long long value);

// 获取系统可用内存比例（仅系统内存，不包括内存池）
// 用于判断是否需要紧急清理
float getSystemMemRate();

// 获取可用内存比例（系统内存 + 内存池空闲内存）
// 参数 poolManager: 实例专属的内存池管理器
float getAvailableMemRate(IndexNodePoolManager& poolManager);

bool FlwPrintf(const char* fileName, const char* format, ...);

bool AddFindPos(SetWithLock* resultSet, unsigned long long pos, char skipNum, Myfile& dstFile, const char* searchTarget, unsigned int targetLen);

inline uint64_t LoadUint64Partial(const unsigned char* data, unsigned int len)
{
	uint64_t value = 0;
	if (len != 0)
	{
		std::memcpy(&value, data, len);
	}
	return value;
}

inline bool EqualBytesFastPath(const unsigned char* left, const unsigned char* right, unsigned int len)
{
	switch (len)
	{
	case 0:
		return true;
	case 1:
		return left[0] == right[0];
	case 2:
	{
		uint16_t leftValue = 0;
		uint16_t rightValue = 0;
		std::memcpy(&leftValue, left, sizeof(leftValue));
		std::memcpy(&rightValue, right, sizeof(rightValue));
		return leftValue == rightValue;
	}
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		return LoadUint64Partial(left, len) == LoadUint64Partial(right, len);
	default:
		return std::memcmp(left, right, len) == 0;
	}
}
