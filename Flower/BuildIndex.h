#pragma once
#include "Myfile.h"
#include "IndexFile.h"
class BuildIndex
{
	bool init(const char* fileName, Index* index);				//��ʼ��
	bool cutNodeSize(IndexNode* indexNode);			//���ٽڵ�Ĵ�Сʹ�ڵ������ܱ����ڱȽ�С�Ŀռ�����
	Myfile dstFile;									//��Ҫ����������Ŀ���ļ�
	IndexFile indexFile;							//�������������ļ��ͻ���Ķ���
};