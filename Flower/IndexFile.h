#pragma once
#include "Myfile.h"
#include "Index.h"

class IndexFile
{
public:
	IndexFile();
	bool init(const char* fileName, Index* index);

	IndexNode* getIndexNode(unsigned long long indexId);
	IndexNode* getTempIndexNode(unsigned long long indexId);
	bool writeFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool writeTempFile(unsigned long long indexId, IndexNode* pIndexNode);
	bool reduceCache();
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//�����µĽڵ�
	bool deleteIndexNode(unsigned long long indexId);										//ɾ���ڵ�
	void setRootIndexId(unsigned long long rootIndexId);									//���ø��ڵ�id
	unsigned long long getRootIndexId();													//��ȡ���ڵ�id
	bool writeEveryCache();																	//�ѻ��浱�е�����ȫ��д��
	bool putIndexNode(IndexNode* indexNode);												//�ⲿʹ�����˸���˵�ⲿ�Ѿ���������
private:
	Myfile indexFile;
	Index* pIndex;
	std::unordered_set<unsigned long long> tempIndexNodeId;
	unsigned long long rootIndexId;
};