#pragma once
#include <unordered_map>
#include <set>
#include <vector>
#include "ReadWriteLock.h"
#include "SetWithLock.h"

//索引孩子节点的类型
const unsigned char CHILD_TYPE_NODE = 0;
const unsigned char CHILD_TYPE_LEAF = 1;
const unsigned char CHILD_TYPE_VALUE = 2;

//索引节点的类型
const unsigned char NODE_TYPE_ONE = 0;
const unsigned char NODE_TYPE_TWO = 1;
const unsigned char NODE_TYPE_THREE = 2;
const unsigned char NODE_TYPE_FOUR = 3;

//是构建文件索引还是键值索引
const unsigned char BUILD_TYPE_FILE = 0;
const unsigned char BUILD_TYPE_KV = 1;

class BuildIndex;
class Myfile;
class IndexNode
{
public:
	IndexNode();
	virtual bool toBinary(char *buffer, int len) = 0;					//把这个结构体转成二进制好进行存储
	virtual bool toObject(char* buffer, int len, unsigned char buildType = BUILD_TYPE_FILE) = 0;					//把二进制转成结构体
	unsigned long long getPreCmpLen();									//获取这个节点前面已经比较过的长度
	bool getIsModified();												//获取节点是否已经修改过
	unsigned long long getParentId();									//获取父节点Id;
	virtual unsigned char getType() = 0;								//获取节点的类型
	virtual bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId) = 0;
	virtual bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId) = 0;
	void setParentID(unsigned long long parentID);								//设置父节点id
	virtual size_t getChildrenNum() = 0;										//获取孩子的数量
	virtual  IndexNode* changeType(BuildIndex* buildIndex, unsigned char buildType = BUILD_TYPE_FILE) = 0;//改变节点的类型缩小每个索引查找使用的键的大小
	void setStart(unsigned long long start);													//设置在原文件的开始位置
	unsigned long long getStart();										//获取节点在远文件的开始位置
	void setLen(unsigned long long len);								//设置节点在文件当中的长度
	unsigned long long getLen();										//获取节点在文件当中的长度
	void setPreCmpLen(unsigned long long preCmpLen);												//设置这个节点前面已经比较过的长度
	void setIsModified(bool isModified);								//设置是否已经改变过
	virtual IndexNode* cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId, unsigned char buildType = BUILD_TYPE_FILE) = 0;				//减小节点的大小
	void setIndexId(unsigned long long indexId);
	unsigned long long getIndexId();									//获取节点的索引id
	void insertLeafSet(unsigned long long start);												//插入叶子节点
	bool mergeSameLenNode(BuildIndex* buildIndex, IndexNode* indexNode, unsigned char buildType = BUILD_TYPE_FILE);//合并相同长度的节点
	bool appendLeafSet(IndexNode* indexNode, unsigned long long beforeNumber, unsigned long long fileSize);		//把某个长度以前的叶子节点加入到现在的节点
	void increaseRef();													//增加索引
	void decreaseRef();													//减少索引
	bool isZeroRef();													//判断是否是没有任何索引
	bool decreaseAndTestZero();											//减少索引并判断是否是0
	bool  getFirstLeafSet(unsigned long long* firstLeaf);	//获得最长的叶子节点
	bool addLeafPosToResult(unsigned long long leastEndPos, unsigned char skipCharNum, unsigned long long fileSize, SetWithLock& result, Myfile& dstFile, const char* searchTarget, unsigned int targetLen);
	unsigned long long getPartOfKey();
	void setPartOfKey(unsigned long long partOfKey);
	void swiftPartOfKey(unsigned long long byte);
	unsigned char getGridNum();
	void setGridNum(unsigned char gridNum);
	virtual ~IndexNode();
protected:
	unsigned long long start;	//在原文件当中的位置
	unsigned long long len;		//文件中指定位置的这个节点对应段的长度
	unsigned long long preCmpLen;	//查询到这个结点的时候前面已经比较过的字符的长度
	unsigned long long parentID;	//父节点的Id
	unsigned long long indexId;		//节点的id;
	unsigned long long partOfKey;	//索引有可能用于建立键值存储的情况这里记录一部分的key值
	std::set<unsigned long long> leafSet;	//有些叶子节点是指向结尾的,为了节省空间这里记录这些比较到这个节点一部分全部一样的叶子节点的开始比较位置
	bool isModified;			//从缓存中删除了以后是否需要写入硬盘
	volatile unsigned long refCount;		//搜索文件的时候是采用多线程的这个时候有可能多个线程同时使用同一个的情况不好判断删除的时机所以这里加一个引用数量
	unsigned char gridNum;		//在索引文件当中占用的格子数
};

class IndexNodeChild
{
	friend class IndexNodeTypeOne;
	friend class IndexNodeTypeTwo;
	friend class IndexNodeTypeThree;
	friend class IndexNodeTypeFour;
public:
	IndexNodeChild();
	IndexNodeChild(unsigned char childType, unsigned long long indexId);
	unsigned char getType() const;
	unsigned long long getIndexId() const;
	void setIndexId(unsigned long long indexId);
	void setChildType(unsigned char childType);
private:
	//0表示非叶子节点, 1表示叶子节点,2表示表示一个值。
	unsigned char childType;
	unsigned long long indexId;	//如果是非叶子节点就是索引节点的Id,如果是叶子节点就是除开之前的比较后面从文件开始的比较处
};

//第一种节点是以8个字节来比较得到孩子节点的
class IndexNodeTypeOne : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNode;
	friend class SearchIndex;
	friend class KVContent;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len, unsigned char buildType);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex, unsigned char buildType);
	IndexNode* cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId, unsigned char buildType);
	bool insertChildNode(BuildIndex* buildIndex, unsigned long long key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool mergeSameLenNode(BuildIndex* buildIndex, IndexNodeTypeOne* indexNode, unsigned char buildType = BUILD_TYPE_FILE);
	IndexNodeChild* getIndexNodeChild(unsigned long long key);
	std::unordered_map<unsigned long long, IndexNodeChild>& getChildren();
	std::unordered_map<unsigned long long, IndexNodeChild> children;
};

//第二种节点是以4个字节来比较得到孩子节点的
class IndexNodeTypeTwo : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNodeTypeOne;
	friend class IndexNode;
	friend class SearchIndex;
	friend class KVContent;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len, unsigned char buildType);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex, unsigned char buildType);
	IndexNode* cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId, unsigned char buildType);
	bool insertChildNode(BuildIndex* buildIndex, unsigned long long key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool insertChildNode(BuildIndex* buildIndex, unsigned int key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool mergeSameLenNode(BuildIndex* buildIndex, IndexNodeTypeTwo* indexNode, unsigned char buildType = BUILD_TYPE_FILE);
	IndexNodeChild* getIndexNodeChild(unsigned int key);
	std::unordered_map<unsigned int, IndexNodeChild>& getChildren();
	std::unordered_map<unsigned int, IndexNodeChild> children;
};

//第三种节点是以2个字节来比较得到孩子节点的
class IndexNodeTypeThree : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNodeTypeTwo;
	friend class IndexNode;
	friend class SearchIndex;
	friend class KVContent;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len, unsigned char buildType);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex, unsigned char buildType);
	IndexNode* cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId, unsigned char buildType);
	bool insertChildNode(BuildIndex* buildIndex, unsigned int key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool insertChildNode(BuildIndex* buildIndex, unsigned short key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool mergeSameLenNode(BuildIndex* buildIndex, IndexNodeTypeThree* indexNode, unsigned char buildType = BUILD_TYPE_FILE);
	IndexNodeChild* getIndexNodeChild(unsigned short key);
	std::unordered_map<unsigned short, IndexNodeChild>& getChildren();
	std::unordered_map<unsigned short, IndexNodeChild> children;
};

//第四种节点是以一个字节来比较得到孩子节点的
class IndexNodeTypeFour : public IndexNode
{
	friend class BuildIndex;
	friend class IndexNodeTypeThree;
	friend class IndexNode;
	friend class SearchIndex;
	friend class KVContent;
	bool toBinary(char* buffer, int len);
	bool toObject(char* buffer, int len, unsigned char buildType);
	unsigned char getType();
	bool changeChildIndexId(unsigned long long orgIndexId, unsigned long long newIndexId);
	bool getAllChildNodeId(std::vector<unsigned long long>& childIndexId);
	size_t getChildrenNum();
	IndexNode* changeType(BuildIndex* buildIndex, unsigned char buildType);
	IndexNode* cutNodeSize(BuildIndex* buildIndex, unsigned long long indexId, unsigned char buildType);
	bool insertChildNode(BuildIndex* buildIndex, unsigned short key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool insertChildNode(BuildIndex* buildIndex, unsigned char key, const IndexNodeChild& indexNodeChild, unsigned char buildType = BUILD_TYPE_FILE);
	bool mergeSameLenNode(BuildIndex* buildIndex, IndexNodeTypeFour* indexNode, unsigned char buildType = BUILD_TYPE_FILE);
	IndexNodeChild* getIndexNodeChild(unsigned char key);
	std::unordered_map<unsigned char, IndexNodeChild>& getChildren();
	std::unordered_map<unsigned char, IndexNodeChild> children;
};
