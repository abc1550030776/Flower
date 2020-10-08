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
	virtual bool toBinary(char *buffer, int len) = 0;					//把这个结构体转成二进制好进行存储
protected:
	unsigned long long start;	//在原文件当中的位置
	unsigned long long len;		//文件中指定位置的这个节点对应段的长度
	unsigned long long preCmpLen;	//查询到这个结点的时候前面已经比较过的字符的长度
	unsigned long long parentID;	//父节点的Id
	std::unordered_set<unsigned long long> leafSet;	//有些叶子节点是指向结尾的,为了节省空间这里记录这些比较到这个节点一部分全部一样的叶子节点的开始比较位置
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
	//0表示非叶子节点, 1表示叶子节点,有些叶子节点指向结尾,可能和这个分支相同,这时候用2表示这里还包含了一个指向结尾的叶子节点这样节省空间。
	unsigned char childType;
	unsigned long long indexId;	//如果是非叶子节点就是索引节点的Id,如果是叶子节点就是除开之前的比较后面从文件开始的比较处
};

//第一种节点是以8个字节来比较得到孩子节点的
class IndexNodeTypeOne : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned long long, IndexNodeChild> children;
};

//第二种节点是以4个字节来比较得到孩子节点的
class IndexNodeTypeTwo : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned int, IndexNodeChild> children;
};

//第三种节点是以2个字节来比较得到孩子节点的
class IndexNodeTypeThree : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned short, IndexNodeChild> children;
};

//第四种节点是以一个字节来比较得到孩子节点的
class IndexNodeTypeFour : public IndexNode
{
	bool toBinary(char* buffer, int len);
	std::unordered_map<unsigned char, IndexNodeChild> children;
};