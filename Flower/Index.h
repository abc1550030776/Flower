#pragma once
#include "IndexNode.h"
#include <map>
#include <vector>
#include "ReadWriteLock.h"

unsigned char USE_TYPE_BUILD = 0;
unsigned char USE_TYPE_SEARCH = 1;
class Index
{
public:
	Index();
	Index(unsigned char useType);
	IndexNode* getIndexNode(unsigned long long indexId);
	bool insert(unsigned long long indexId, IndexNode*& pIndexNode);
	unsigned int size();
	bool getLastNodes(unsigned int num, std::vector<unsigned long long>& indexIdVec, std::vector<IndexNode*>& indexNodeVec);
	bool reduceCache(unsigned int needReduceNum);
	bool reduceCache();
	bool changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen);
	bool swapNode(unsigned long long indexId, IndexNode* newNode);
	IndexNode* newIndexNode(unsigned char nodeType, unsigned long long preCmpLen);			//�����µĽڵ�
	bool deleteIndexNode(unsigned long long indexId);										//ɾ���ڵ�
	void clearCache();																		//�������
	unsigned char getUseType();																//��ȡʹ�÷�ʽ
	bool putIndexNode(IndexNode* indexNode);												//�ⲿʹ�����˸���˵�ⲿ�Ѿ���������
	~Index();																				//��������
private:
	unsigned char useType;																	//ʹ�õķ�ʽ��������ڲ�ѯ��ʱ���Ƕ��̵߳�
	std::unordered_map<unsigned long long, IndexNode*> indexNodeCache;						//���ﱣ��һ���ֵ������ڵ㻺���������Լӿ��ٶ�
	std::multimap<unsigned long long, unsigned long long> IndexIdPreority;					//���ﱣ�����������ȼ�key��������ǰ���Ѿ��ȽϹ��Ĵ�С,ԽС���ȼ�Խ��
	RTL_SRWLOCK rwLock;																		//����������ģʽ�»ᱻ����߳�ʹ�õ��Ӹ���д��
};

//���̶߳�ȡ��ʱ����õ��ĺ���getIndexNode,insert,size,reduceCache,