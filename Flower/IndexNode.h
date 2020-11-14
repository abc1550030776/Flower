#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "BuildIndex.h"

//�������ӽڵ������
const unsigned char CHILD_TYPE_NODE = 0;
const unsigned char CHILD_TYPE_LEAF = 1;

//�����ڵ������
const unsigned char NODE_TYPE_ONE = 0;
const unsigned char NODE_TYPE_TWO = 1;
const unsigned char NODE_TYPE_THREE = 2;
const unsigned char NODE_TYPE_FOUR = 3;

class IndexNode
{
public:
	IndexNode();
	virtual bool toBinary(char *buffer, int len) = 0;					//������ṹ��ת�ɶ����ƺý��д洢
	virtual bool toObject(char* buffer, int len) = 0;					//�Ѷ�����ת�ɽṹ��
	void setIsBig(bool isBig);											//�����ǲ��Ǵ�Ľڵ��
	unsigned long long getPreCmpLen();									//��ȡ����ڵ�ǰ���Ѿ��ȽϹ��ĳ���
	bool getIsModified();												//��ȡ�ڵ��Ƿ��Ѿ��޸Ĺ�
	bool getIsBig();													//��ȡ�Ƿ��ǱȽϴ�Ľڵ�
	unsigned long long getParentId();									//��ȡ���ڵ�Id;
	virtual unsigned char getType() = 0;								//��ȡ�ڵ������
	virtual bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId) = 0;
	virtual bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId) = 0;
	void setParentID(unsigned long long parentID);								//���ø��ڵ�id
	virtual size_t getChildrenNum() = 0;										//��ȡ���ӵ�����
	virtual  IndexNode* changeType(BuildIndex* buildIndex) = 0;					//�ı�ڵ��������Сÿ����������ʹ�õļ��Ĵ�С
	void setStart(unsigned long long start);													//������ԭ�ļ��Ŀ�ʼλ��
	unsigned long long getStart();										//��ȡ�ڵ���Զ�ļ��Ŀ�ʼλ��
	void setLen(unsigned long long len);								//���ýڵ����ļ����еĳ���
	unsigned long long getLen();										//��ȡ�ڵ����ļ����еĳ���
	void setPreCmpLen(unsigned long long preCmpLen);												//��������ڵ�ǰ���Ѿ��ȽϹ��ĳ���
	void setIsModified(bool isModified);								//�����Ƿ��Ѿ��ı��
	virtual bool cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId) = 0;				//��С�ڵ�Ĵ�С
	void setIndexId(unsigned long long indexId);
	unsigned long long getIndexId();									//��ȡ�ڵ������id
	void insertLeafSet(unsigned long long start);												//����Ҷ�ӽڵ�
	virtual ~IndexNode();
protected:
	unsigned long long start;	//��ԭ�ļ����е�λ��
	unsigned long long len;		//�ļ���ָ��λ�õ�����ڵ��Ӧ�εĳ���
	unsigned long long preCmpLen;	//��ѯ���������ʱ��ǰ���Ѿ��ȽϹ����ַ��ĳ���
	unsigned long long parentID;	//���ڵ��Id
	unsigned long long indexId;		//�ڵ��id;
	std::unordered_set<unsigned long long> leafSet;	//��ЩҶ�ӽڵ���ָ���β��,Ϊ�˽�ʡ�ռ������¼��Щ�Ƚϵ�����ڵ�һ����ȫ��һ����Ҷ�ӽڵ�Ŀ�ʼ�Ƚ�λ��
	bool isBig;					//��Щ�ڵ�д��Ӳ�̴���4k�ֽھ���big
	bool isModified;			//�ӻ�����ɾ�����Ժ��Ƿ���Ҫд��Ӳ��
};

class IndexNodeChild
{
	friend class IndexNodeTypeOne;
	friend class IndexNodeTypeTwo;
	friend class IndexNodeTypeThree;
	friend class IndexNodeTypeFour;
public:
	IndexNodeChild();
	IndexNodeChild(unsigned char childType, unsigned char indexId);
	unsigned char getType() const;
	unsigned long long getIndexId() const;
	void setIndexId(unsigned long long indexId);
	void setChildType(unsigned char childType);
private:
	//0��ʾ��Ҷ�ӽڵ�, 1��ʾҶ�ӽڵ㡣
	unsigned char childType;
	unsigned long long indexId;	//����Ƿ�Ҷ�ӽڵ���������ڵ��Id,�����Ҷ�ӽڵ���ǳ���֮ǰ�ıȽϺ�����ļ���ʼ�ıȽϴ�
};

//��һ�ֽڵ�����8���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeOne : public IndexNode
{
	friend class BuildIndex;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex);
	bool cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId);
	bool insertChildNode(BuildIndex* buildIndex, unsigned long long key, const IndexNodeChild& indexNodeChild);
	std::unordered_map<unsigned long long, IndexNodeChild> children;
};

//�ڶ��ֽڵ�����4���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeTwo : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNodeTypeOne;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex);
	bool cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId);
	bool insertChildNode(BuildIndex* buildIndex, unsigned long long key, const IndexNodeChild& indexNodeChild);
	bool insertChildNode(BuildIndex* buildIndex, unsigned int key, const IndexNodeChild& indexNodeChild);
	std::unordered_map<unsigned int, IndexNodeChild> children;
};

//�����ֽڵ�����2���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeThree : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNodeTypeTwo;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex);
	bool cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId);
	bool insertChildNode(BuildIndex* buildIndex, unsigned int key, const IndexNodeChild& indexNodeChild);
	bool insertChildNode(BuildIndex* buildIndex, unsigned short key, const IndexNodeChild& indexNodeChild);
	std::unordered_map<unsigned short, IndexNodeChild> children;
};

//�����ֽڵ�����һ���ֽ����Ƚϵõ����ӽڵ��
class IndexNodeTypeFour : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNodeTypeThree;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex);
	bool cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId);
	bool insertChildNode(BuildIndex* buildIndex, unsigned short key, const IndexNodeChild& indexNodeChild);
	bool insertChildNode(BuildIndex* buildIndex, unsigned char key, const IndexNodeChild& indexNodeChild);
	std::unordered_map<unsigned char, IndexNodeChild> children;
};