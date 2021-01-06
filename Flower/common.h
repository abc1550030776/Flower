#pragma once

const unsigned short SIZE_PER_INDEX_FILE_GRID = 512;				//索引文件里面一个格子的大小、每个节点可能占用多个格子

const unsigned short MAX_SIZE_PER_INDEX_NODE = 8 * 1024;			//每个索引节点在索引文件里面最多占用的大小

bool getIndexPath(const char* dstFilePath, char* indexPath);

bool compareTwoType(const unsigned char leftType, const unsigned char rightType);

bool getKVFilePath(const char* dstFilePath, char* kVFilePath);

unsigned char swiftBigLittleEnd(unsigned char value);

unsigned short swiftBigLittleEnd(unsigned short value);

unsigned int swiftBigLittleEnd(unsigned int value);

unsigned long long swiftBigLittleEnd(unsigned long long value);

float getAvailableMemRate();

bool FlwPrintf(const char* fileName, const char* format, ...);
