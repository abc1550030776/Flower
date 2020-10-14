#pragma once
#include "IndexNode.h"
#include <map>
class Index
{
public:
	IndexNode* getIndexNode(unsigned long long indexId);
private:
	std::unordered_map<unsigned long long, IndexNode*> indexNodeCache;						//���ﱣ��һ���ֵ������ڵ㻺���������Լӿ��ٶ�
	std::multimap<unsigned long long, unsigned long long> IndexIdPreority;					//���ﱣ�����������ȼ�key��������ǰ���Ѿ��ȽϹ��Ĵ�С,ԽС���ȼ�Խ��
};