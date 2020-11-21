#pragma once
#include "SetWithLock.h"
#include "Myfile.h"
#include "IndexFile.h"
class SearchIndex
{
	SearchIndex();
	bool init(const char* searchTarget, unsigned int targetLen, SetWithLock* resultSet, const char* fileName, Index* index, unsigned char skipCharNum);
	bool search();																				//�ļ����е�Ŀ������
	const char* searchTarget;
	unsigned int targetLen;
	SetWithLock* resultSet;
	Myfile dstFile;
	IndexFile indexFile;
	unsigned long long dstFileSize;					//Ŀ���ļ��Ĵ�С
	unsigned char skipCharNum;						//����ǰ��Ҫ�������ַ�����
};