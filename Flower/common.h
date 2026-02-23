#pragma once

class SetWithLock;
class Myfile;
class IndexNodePoolManager;

const unsigned short SIZE_PER_INDEX_FILE_GRID = 128;			//索引文件里面一个格子的大小、每个节点可能占用多个格子

const unsigned short MAX_SIZE_PER_INDEX_NODE = 20 * 1024;		//每个索引节点在索引文件里面最多占用的大小

const unsigned int DST_SIZE_PER_ROOT = 8 * 1024 * 1024;					//多少个目标文件字节的数据构建一个一部分的根节点

// ========== 内存管理阈值常量 ==========
// 紧急清理阈值：系统内存低于此值时清空所有缓存和内存池
const float EMERGENCY_CLEANUP_THRESHOLD = 0.1f;  // 10%

// 部分清理阈值：组合内存低于此值时进行部分清理
const float PARTIAL_CLEANUP_THRESHOLD_SEARCH = 0.2f;   // 20% (搜索模式)
const float PARTIAL_CLEANUP_THRESHOLD_BUILD = 0.4f;    // 40% (构建模式)

// 部分清理比例
const float PARTIAL_CLEANUP_RATIO_BUILD = 0.7f;  // 70% (构建模式清理比例)

// 惩罚因子阈值：系统内存低于这些阈值时应用不同程度的惩罚
const float PENALTY_THRESHOLD_LIGHT = 0.3f;   // 30% (开始应用惩罚)
const float PENALTY_THRESHOLD_MEDIUM = 0.2f;  // 20% (中度惩罚)
const float PENALTY_THRESHOLD_HEAVY = 0.1f;   // 10% (重度惩罚)

// 惩罚因子值
const float PENALTY_FACTOR_HEAVY = 0.1f;      // 重度惩罚因子
const float PENALTY_FACTOR_MEDIUM_MIN = 0.2f; // 中度惩罚因子最小值
const float PENALTY_FACTOR_MEDIUM_MAX = 0.5f; // 中度惩罚因子最大值
const float PENALTY_FACTOR_LIGHT_MIN = 0.5f;  // 轻度惩罚因子最小值
const float PENALTY_FACTOR_LIGHT_MAX = 1.0f;  // 轻度惩罚因子最大值

// 内存信息缓存刷新间隔（秒）
// 避免频繁读取 /proc/meminfo 文件，减少 I/O 开销
const float MEM_INFO_CACHE_INTERVAL = 0.5f;  // 0.5秒

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
