#pragma once
#include "Myfile.h"
#include "IndexFile.h"
class BuildIndex
{
public:
	BuildIndex();
	bool init(const char* fileName, Index* index);				//��ʼ��
	bool cutNodeSize(unsigned long long indexId, IndexNode* indexNode);			//���ٽڵ�Ĵ�Сʹ�ڵ������ܱ����ڱȽ�С�Ŀռ�����
	IndexNode* getIndexNode(unsigned long long indexId);		//����ĳ���ڵ�id��ȡ�ڵ�
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool mergeNode(unsigned long long preCmpLen, unsigned long long parentId, IndexNodeChild& leftChildNode, const IndexNodeChild& rightChildNode);
	IndexNode* changeNodeType(unsigned long long indexId, IndexNode* indexNode);
	bool build();												//�����ļ�����
private:
	Myfile dstFile;									//��Ҫ����������Ŀ���ļ�
	IndexFile indexFile;							//�������������ļ��ͻ���Ķ���
	unsigned long long dstFileSize;					//Ŀ���ļ��Ĵ�С
};