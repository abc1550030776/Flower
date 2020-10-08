#pragma once
#include <unordered_map>
#include <unordered_set>

const int CHILD_TYPE_NODE = 0;
const int CHILD_TYPE_LEAF = 1;
const int CHILD_TYPE_NODENLEAF = 2;
class IndexNode
{
public:
	IndexNode();
	virtual bool toBinary(char *buffer, int len) = 0;					//������ṹ��ת�ɶ����ƺý��д洢
protected:
	unsigned long long start;	//��ԭ�ļ����е�λ��
	unsigned long long len;		//�ļ���ָ��λ�õ�����ڵ��Ӧ�εĳ���
	unsigned long long preCmpLen;	//��ѯ���������ʱ��ǰ���Ѿ��ȽϹ����ַ��ĳ���
	unsigned long long parentID;	//���ڵ��Id
	std::unordered_set<unsigned long long> leafSet;	//��ЩҶ�ӽڵ���ָ���β��,Ϊ�˽�ʡ�ռ������¼��Щ�Ƚϵ�����ڵ�һ����ȫ��һ����Ҷ�ӽڵ�Ŀ�ʼ�Ƚ�λ��
};

class IndexNodeChild
{
	friend class IndexNodeTypeOne;
	friend class IndexNodeTypeTwo;
	friend class IndexNodeTypeThree;
	friend class IndexNodeTypeFour;
public:
	IndexNodeChild();
private:
	//0��ʾ��Ҷ�ӽڵ�, 1��ʾҶ�ӽڵ�,��ЩҶ�ӽڵ�ָ���β,���ܺ������֧��ͬ,��ʱ����2��ʾ���ﻹ������һ��ָ���β��Ҷ�ӽڵ�������ʡ�ռ䡣
	unsigned char childType;
	unsigned long long indexId;	//����Ƿ�Ҷ�ӽڵ���������ڵ��Id,�����Ҷ�ӽڵ���ǳ���֮ǰ�ıȽϺ�����ļ���ʼ�ıȽϴ�
};

//��һ�ֽڵ�����8���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeOne : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned long long, IndexNodeChild> children;
};

//�ڶ��ֽڵ�����4���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeTwo : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned int, IndexNodeChild> children;
};

//�����ֽڵ�����2���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeThree : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned short, IndexNodeChild> children;
};

//�����ֽڵ�����һ���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeFour : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned char, IndexNodeChild> children;
};