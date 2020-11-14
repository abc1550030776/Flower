#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "BuildIndex.h"

//索引孩子节点的类型
const unsigned char CHILD_TYPE_NODE = 0;
const unsigned char CHILD_TYPE_LEAF = 1;

//索引节点的类型
const unsigned char NODE_TYPE_ONE = 0;
const unsigned char NODE_TYPE_TWO = 1;
const unsigned char NODE_TYPE_THREE = 2;
const unsigned char NODE_TYPE_FOUR = 3;

class IndexNode
{
public:
	IndexNode();
	virtual bool toBinary(char *buffer, int len) = 0;					//把这个结构体转成二进制好进行存储
	virtual bool toObject(char* buffer, int len) = 0;					//把二进制转成结构体
	void setIsBig(bool isBig);											//设置是不是大的节点块
	unsigned long long getPreCmpLen();									//获取这个节点前面已经比较过的长度
	bool getIsModified();												//获取节点是否已经修改过
	bool getIsBig();													//获取是否是比较大的节点
	unsigned long long getParentId();									//获取父节点Id;
	virtual unsigned char getType() = 0;								//获取节点的类型
	virtual bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId) = 0;
	virtual bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId) = 0;
	void setParentID(unsigned long long parentID);								//设置父节点id
	virtual size_t getChildrenNum() = 0;										//获取孩子的数量
	virtual  IndexNode* changeType(BuildIndex* buildIndex) = 0;					//改变节点的类型缩小每个索引查找使用的键的大小
	void setStart(unsigned long long start);													//设置在原文件的开始位置
	unsigned long long getStart();										//获取节点在远文件的开始位置
	void setLen(unsigned long long len);								//设置节点在文件当中的长度
	unsigned long long getLen();										//获取节点在文件当中的长度
	void setPreCmpLen(unsigned long long preCmpLen);												//设置这个节点前面已经比较过的长度
	void setIsModified(bool isModified);								//设置是否已经改变过
	virtual bool cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId) = 0;				//减小节点的大小
	void setIndexId(unsigned long long indexId);
	unsigned long long getIndexId();									//获取节点的索引id
	void insertLeafSet(unsigned long long start);												//插入叶子节点
	virtual ~IndexNode();
protected:
	unsigned long long start;	//在原文件当中的位置
	unsigned long long len;		//文件中指定位置的这个节点对应段的长度
	unsigned long long preCmpLen;	//查询到这个结点的时候前面已经比较过的字符的长度
	unsigned long long parentID;	//父节点的Id
	unsigned long long indexId;		//节点的id;
	std::unordered_set<unsigned long long> leafSet;	//有些叶子节点是指向结尾的,为了节省空间这里记录这些比较到这个节点一部分全部一样的叶子节点的开始比较位置
	bool isBig;					//有些节点写入硬盘大于4k字节就是big
	bool isModified;			//从缓存中删除了以后是否需要写入硬盘
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
	//0表示非叶子节点, 1表示叶子节点。
	unsigned char childType;
	unsigned long long indexId;	//如果是非叶子节点就是索引节点的Id,如果是叶子节点就是除开之前的比较后面从文件开始的比较处
};

//第一种节点是以8个字节来比较得到孩子节点的
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

//第二种节点是以4个字节来比较得到孩子节点的
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

//第三种节点是以2个字节来比较得到孩子节点的
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

//第四种节点是以一个字节来比较得到孩子节点的
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