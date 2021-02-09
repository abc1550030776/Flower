#pragma once

class SetWithLock;
class Myfile;

const unsigned short SIZE_PER_INDEX_FILE_GRID = 128;				//索引文件里面一个格子的大小、每个节点可能占用多个格子

const unsigned short MAX_SIZE_PER_INDEX_NODE = 20 * 1024;			//每个索引节点在索引文件里面最多占用的大小

const unsigned int DST_SIZE_PER_ROOT = 8 * 1024 * 1024;					//多少个目标文件字节的数据构建一个一部分的根节点

bool getIndexPath(const char* dstFilePath, char* indexPath);

bool compareTwoType(const unsigned char leftType, const unsigned char rightType);

bool getKVFilePath(const char* dstFilePath, char* kVFilePath);

unsigned char swiftBigLittleEnd(unsigned char value);

unsigned short swiftBigLittleEnd(unsigned short value);

unsigned int swiftBigLittleEnd(unsigned int value);

unsigned long long swiftBigLittleEnd(unsigned long long value);

float getAvailableMemRate();

bool FlwPrintf(const char* fileName, const char* format, ...);

bool AddFindPos(SetWithLock* resultSet, unsigned long long pos, char skipNum, Myfile& dstFile, const char* searchTarget, unsigned int targetLen);
