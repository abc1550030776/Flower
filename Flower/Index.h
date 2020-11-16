#pragma once
#include "IndexNode.h"
#include <map>
#include <vector>
class Index
{
public:
	IndexNode* getIndexNode(unsigned long long indexId);
	bool insert(unsigned long long indexId, IndexNode* pIndexNode);
	unsigned int size();
	bool getLastNodes(unsigned int num, std::vector<unsigned long long>& indexIdVec, std::vector<IndexNode*>& indexNodeVec);
	bool reduceCache(unsigned int needReduceNum);
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//�����µĽڵ�
	bool deleteIndexNode(unsigned long long indexId);										//ɾ���ڵ�
private:
	std::unordered_map<unsigned long long, IndexNode*> indexNodeCache;						//���ﱣ��һ���ֵ������ڵ㻺���������Լӿ��ٶ�
	std::multimap<unsigned long long, unsigned long long> IndexIdPreority;					//���ﱣ�����������ȼ�key��������ǰ���Ѿ��ȽϹ��Ĵ�С,ԽС���ȼ�Խ��
};