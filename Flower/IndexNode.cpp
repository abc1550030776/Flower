#include "IndexNode.h"
#include "string.h"

IndexNode::IndexNode()
{
	start = 0;
	len = 0;
	preCmpLen = 0;
	parentID = 0;
	isBig = false;
	isModified = false;
}

IndexNodeChild::IndexNodeChild()
{
	childType = 0;
	indexId = 0;
}

IndexNodeChild::IndexNodeChild(unsigned char childType, unsigned char indexId)
{
	this->childType = childType;
	this->indexId = indexId;
}

bool IndexNodeTypeOne::toBinary(char* buffer, int len)
{
	short totalSize = 0;
	char* p = buffer;
	p += 2;
	if (len < 34)
	{
		return false;
	}
	*((unsigned long long*)p) = start;
	p += 8;
	*((unsigned long long*)p) = len;
	p += 8;
	*((unsigned long long*)p) = preCmpLen;
	p += 8;
	*((unsigned long long*)p) = parentID;
	p += 8;
	totalSize = 32;
	int leftSize = len - 34;
	if (!children.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		unsigned char* indexNodeNum = (unsigned char*)p;
		*indexNodeNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		char leafBuffer[256 * 16];
		unsigned char leafNum = 0;
		char lastLeafBuffer[256 * 8];			//��Щָ��ĩβ��Ҷ�ӽڵ�������ڵ��غϵ�����
		unsigned char lastLeafNum = 0;
		for (auto& value : children)
		{
			if (value.second.childType == CHILD_TYPE_NODE)
			{
				if (leftSize < 16)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned long long*)p) = value.first;
				p += 8;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 16;
				leftSize -= 16;
			}
			else if (value.second.childType == CHILD_TYPE_LEAF)
			{
				*((unsigned long long*)(leafBuffer + 16 * leafNum)) = value.first;
				*((unsigned long long*)(leafBuffer + 16 * leafNum + 8)) = value.second.indexId;
				leafNum++;
			}
			else
			{
				if (leftSize < 16)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned long long*)p) = value.first;
				p += 8;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 16;
				leftSize -= 16;
				*((unsigned long long*)(lastLeafBuffer + 8 * lastLeafNum)) = value.first;
				lastLeafNum++;
			}
		}
		//����ֻ��Ҷ�ӽڵ㲿��
		if (leftSize < 1 + leafNum * 16)
		{
			return false;
		}
		*p = leafNum;
		p++;
		memcpy(p, leafBuffer, leafNum * 16);
		p += leafNum * 16;
		totalSize += (1 + leafNum * 16);
		leftSize -= (1 + leafNum * 16);
		//���������ͬһ����֧��ĿǰΪָֹ���ļ�ĩβ��Ҷ�ӽڵ�ʹ�õ�����
		if (leftSize < 1 + lastLeafNum * 8)
		{
			return false;
		}
		*p = lastLeafNum;
		p++;
		memcpy(p, lastLeafBuffer, lastLeafNum * 8);
		p += lastLeafNum * 8;
		totalSize += (1 + lastLeafNum * 8);
		leftSize -= (1 + lastLeafNum * 8);
	}
	else
	{
		//û�к��ӽڵ�������0���ֽڱ�ʾ���ֽڵ���Ŀ������
		if (leftSize < 3)
		{
			return false;
		}
		*((short*)p) = 0;
		p += 2;
		*p = 0;
		p++;
		totalSize += 3;
		leftSize -= 3;
	}

	//��Щ�Ƚϵ������;�͵��ļ�ĩβ����ʱ�����֧��¼��Ҷ�ӽڵ㼯����
	if (!leafSet.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		char* leafNum = p;
		*leafNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		for (auto& value : leafSet)
		{
			if (leftSize < 8)
			{
				return false;
			}
			(*leafNum)++;
			*((unsigned long long*)p) = value;
			p += 8;
			totalSize += 8;
			leftSize -= 8;
		}
	}
	else
	{
		//�յĻ�д���ʾ�ⲿ����0
		if (leftSize < 1)
		{
			return false;
		}
		*p = 0;
		totalSize += 1;
	}
	//���������Сд����ǰ��
	*((short*)buffer) = totalSize;
	return true;
}

bool IndexNodeTypeOne::toObject(char* buffer, int len)
{
	//�洢��ʱ��ǰ���м������ͻ��д洢�Ĵ�С����ǰ���3���ֽ�
	if ((len + 3) > 4 * 1024)
	{
		isBig = true;
	}
	char* p = buffer;
	int leftSize = len;
	if (leftSize < 32)
	{
		return false;
	}

	start = *((unsigned long long*)p);
	p += 8;
	len = *((unsigned long long*)p);
	p += 8;
	preCmpLen = *((unsigned long long*)p);
	p += 8;
	parentID = *((unsigned long long*)p);
	p += 8;
	leftSize -= 32;
	//�ȶ�ȡ�������ڵ�Ĳ���
	if (leftSize < 1)
	{
		return false;
	}
	unsigned char indexNodeNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < indexNodeNum * 16)
	{
		return false;
	}

	for (unsigned char i = 0; i < indexNodeNum; ++i)
	{
		unsigned long long findValue = *((unsigned long long*)p);
		p += 8;
		IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= indexNodeNum * 16;
	//���ֻ��Ҷ�ӽڵ㲿��
	unsigned char leafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < leafNum * 16)
	{
		return false;
	}

	for (unsigned char i = 0; i < leafNum; ++i)
	{
		unsigned long long findValue = *((unsigned long long*)p);
		p += 8;
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= leafNum * 16;
	//��Ӻ������ڵ�ͬһ����֧��ָ��ĩβ��Ҷ�ڵ�
	unsigned char lastLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < lastLeafNum * 8)
	{
		return false;
	}

	for (unsigned char i = 0; i < lastLeafNum; ++i)
	{
		std::unordered_map<unsigned long long, IndexNodeChild>::iterator it = children.find(*((unsigned long long*)p));
		if (it == end(children))
		{
			return false;
		}
		it->second.childType = CHILD_TYPE_NODENLEAF;
		p += 8;
	}

	leftSize -= lastLeafNum * 8;


	//��ӱȽϵ���;�͵��ļ���β��Ҷ�ӽڵ�
	unsigned char endLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < endLeafNum * 8)
	{
		return false;
	}

	for (unsigned char i = 0; i < endLeafNum; ++i)
	{
		bool ok = leafSet.insert(*((unsigned long long*)p)).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}
	return true;
}

bool IndexNodeTypeTwo::toBinary(char* buffer, int len)
{
	short totalSize = 0;
	char* p = buffer;
	p += 2;
	if (len < 34)
	{
		return false;
	}
	*((unsigned long long*)p) = start;
	p += 8;
	*((unsigned long long*)p) = len;
	p += 8;
	*((unsigned long long*)p) = preCmpLen;
	p += 8;
	*((unsigned long long*)p) = parentID;
	p += 8;
	totalSize = 32;
	int leftSize = len - 34;
	if (!children.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		unsigned char* indexNodeNum = (unsigned char*)p;
		*indexNodeNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		char leafBuffer[256 * 12];
		unsigned char leafNum = 0;
		char lastLeafBuffer[256 * 4];			//��Щָ��ĩβ��Ҷ�ӽڵ�������ڵ��غϵ�����
		unsigned char lastLeafNum = 0;
		for (auto& value : children)
		{
			if (value.second.childType == CHILD_TYPE_NODE)
			{
				if (leftSize < 12)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned int*)p) = value.first;
				p += 4;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 12;
				leftSize -= 12;
			}
			else if (value.second.childType == CHILD_TYPE_LEAF)
			{
				*((unsigned int*)(leafBuffer + 12 * leafNum)) = value.first;
				*((unsigned long long*)(leafBuffer + 12 * leafNum + 4)) = value.second.indexId;
				leafNum++;
			}
			else
			{
				if (leftSize < 12)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned int*)p) = value.first;
				p += 4;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 12;
				leftSize -= 12;
				*((unsigned int*)(lastLeafBuffer + 4 * lastLeafNum)) = value.first;
				lastLeafNum++;
			}
		}
		//����ֻ��Ҷ�ӽڵ㲿��
		if (leftSize < 1 + leafNum * 12)
		{
			return false;
		}
		*p = leafNum;
		p++;
		memcpy(p, leafBuffer, leafNum * 12);
		p += leafNum * 16;
		totalSize += (1 + leafNum * 12);
		leftSize -= (1 + leafNum * 12);
		//���������ͬһ����֧��ĿǰΪָֹ���ļ�ĩβ��Ҷ�ӽڵ�ʹ�õ�����
		if (leftSize < 1 + lastLeafNum * 4)
		{
			return false;
		}
		*p = lastLeafNum;
		p++;
		memcpy(p, lastLeafBuffer, lastLeafNum * 4);
		p += lastLeafNum * 4;
		totalSize += (1 + lastLeafNum * 4);
		leftSize -= (1 + lastLeafNum * 4);
	}
	else
	{
		//û�к��ӽڵ�������0���ֽڱ�ʾ���ֽڵ���Ŀ������
		if (leftSize < 3)
		{
			return false;
		}
		*((short*)p) = 0;
		p += 2;
		*p = 0;
		p++;
		totalSize += 3;
		leftSize -= 3;
	}

	//��Щ�Ƚϵ������;�͵��ļ�ĩβ����ʱ�����֧��¼��Ҷ�ӽڵ㼯����
	if (!leafSet.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		char* leafNum = p;
		*leafNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		for (auto& value : leafSet)
		{
			if (leftSize < 8)
			{
				return false;
			}
			(*leafNum)++;
			*((unsigned long long*)p) = value;
			p += 8;
			totalSize += 8;
			leftSize -= 8;
		}
	}
	else
	{
		//�յĻ�д���ʾ�ⲿ����0
		if (leftSize < 1)
		{
			return false;
		}
		*p = 0;
		totalSize += 1;
	}
	//���������Сд����ǰ��
	*((short*)buffer) = totalSize;
	return true;
}

bool IndexNodeTypeTwo::toObject(char* buffer, int len)
{
	//�洢��ʱ��ǰ���м������ͻ��д洢�Ĵ�С����ǰ���3���ֽ�
	if ((len + 3) > 4 * 1024)
	{
		isBig = true;
	}
	char* p = buffer;
	int leftSize = len;
	if (leftSize < 32)
	{
		return false;
	}

	start = *((unsigned long long*)p);
	p += 8;
	len = *((unsigned long long*)p);
	p += 8;
	preCmpLen = *((unsigned long long*)p);
	p += 8;
	parentID = *((unsigned long long*)p);
	p += 8;
	leftSize -= 32;
	//�ȶ�ȡ�������ڵ�Ĳ���
	if (leftSize < 1)
	{
		return false;
	}
	unsigned char indexNodeNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < indexNodeNum * 12)
	{
		return false;
	}

	for (unsigned char i = 0; i < indexNodeNum; ++i)
	{
		unsigned int findValue = *((unsigned int*)p);
		p += 4;
		IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= indexNodeNum * 12;
	//���ֻ��Ҷ�ӽڵ㲿��
	unsigned char leafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < leafNum * 12)
	{
		return false;
	}

	for (unsigned char i = 0; i < leafNum; ++i)
	{
		unsigned int findValue = *((unsigned int*)p);
		p += 4;
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= leafNum * 12;
	//��Ӻ������ڵ�ͬһ����֧��ָ��ĩβ��ҳ�ڵ�
	unsigned char lastLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < lastLeafNum * 4)
	{
		return false;
	}

	for (unsigned char i = 0; i < lastLeafNum; ++i)
	{
		std::unordered_map<unsigned int, IndexNodeChild>::iterator it = children.find(*((unsigned int*)p));
		if (it == end(children))
		{
			return false;
		}
		it->second.childType = CHILD_TYPE_NODENLEAF;
		p += 4;
	}

	leftSize -= lastLeafNum * 4;


	//��ӱȽϵ���;�͵��ļ���β��Ҷ�ӽڵ�
	unsigned char endLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < endLeafNum * 8)
	{
		return false;
	}

	for (unsigned char i = 0; i < endLeafNum; ++i)
	{
		bool ok = leafSet.insert(*((unsigned long long*)p)).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}
	return true;
}

bool IndexNodeTypeThree::toBinary(char* buffer, int len)
{
	short totalSize = 0;
	char* p = buffer;
	p += 2;
	if (len < 34)
	{
		return false;
	}
	*((unsigned long long*)p) = start;
	p += 8;
	*((unsigned long long*)p) = len;
	p += 8;
	*((unsigned long long*)p) = preCmpLen;
	p += 8;
	*((unsigned long long*)p) = parentID;
	p += 8;
	totalSize = 32;
	int leftSize = len - 34;
	if (!children.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		unsigned char* indexNodeNum = (unsigned char*)p;
		*indexNodeNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		char leafBuffer[256 * 10];
		unsigned char leafNum = 0;
		char lastLeafBuffer[256 * 2];			//��Щָ��ĩβ��Ҷ�ӽڵ�������ڵ��غϵ�����
		unsigned char lastLeafNum = 0;
		for (auto& value : children)
		{
			if (value.second.childType == CHILD_TYPE_NODE)
			{
				if (leftSize < 10)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned short*)p) = value.first;
				p += 2;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 10;
				leftSize -= 10;
			}
			else if (value.second.childType == CHILD_TYPE_LEAF)
			{
				*((unsigned short*)(leafBuffer + 10 * leafNum)) = value.first;
				*((unsigned long long*)(leafBuffer + 10 * leafNum + 2)) = value.second.indexId;
				leafNum++;
			}
			else
			{
				if (leftSize < 10)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned short*)p) = value.first;
				p += 2;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 10;
				leftSize -= 10;
				*((unsigned short*)(lastLeafBuffer + 2 * lastLeafNum)) = value.first;
				lastLeafNum++;
			}
		}
		//����ֻ��Ҷ�ӽڵ㲿��
		if (leftSize < 1 + leafNum * 10)
		{
			return false;
		}
		*p = leafNum;
		p++;
		memcpy(p, leafBuffer, leafNum * 10);
		p += leafNum * 10;
		totalSize += (1 + leafNum * 10);
		leftSize -= (1 + leafNum * 10);
		//���������ͬһ����֧��ĿǰΪָֹ���ļ�ĩβ��Ҷ�ӽڵ�ʹ�õ�����
		if (leftSize < 1 + lastLeafNum * 2)
		{
			return false;
		}
		*p = lastLeafNum;
		p++;
		memcpy(p, lastLeafBuffer, lastLeafNum * 2);
		p += lastLeafNum * 2;
		totalSize += (1 + lastLeafNum * 2);
		leftSize -= (1 + lastLeafNum * 2);
	}
	else
	{
		//û�к��ӽڵ�������0���ֽڱ�ʾ���ֽڵ���Ŀ������
		if (leftSize < 3)
		{
			return false;
		}
		*((short*)p) = 0;
		p += 2;
		*p = 0;
		p++;
		totalSize += 3;
		leftSize -= 3;
	}

	//��Щ�Ƚϵ������;�͵��ļ�ĩβ����ʱ�����֧��¼��Ҷ�ӽڵ㼯����
	if (!leafSet.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		char* leafNum = p;
		*leafNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		for (auto& value : leafSet)
		{
			if (leftSize < 8)
			{
				return false;
			}
			(*leafNum)++;
			*((unsigned long long*)p) = value;
			p += 8;
			totalSize += 8;
			leftSize -= 8;
		}
	}
	else
	{
		//�յĻ�д���ʾ�ⲿ����0
		if (leftSize < 1)
		{
			return false;
		}
		*p = 0;
		totalSize += 1;
	}
	//���������Сд����ǰ��
	*((short*)buffer) = totalSize;
	return true;
}

bool IndexNodeTypeThree::toObject(char* buffer, int len)
{
	//�洢��ʱ��ǰ���м������ͻ��д洢�Ĵ�С����ǰ���3���ֽ�
	if ((len + 3) > 4 * 1024)
	{
		isBig = true;
	}
	char* p = buffer;
	int leftSize = len;
	if (leftSize < 32)
	{
		return false;
	}

	start = *((unsigned long long*)p);
	p += 8;
	len = *((unsigned long long*)p);
	p += 8;
	preCmpLen = *((unsigned long long*)p);
	p += 8;
	parentID = *((unsigned long long*)p);
	p += 8;
	leftSize -= 32;
	//�ȶ�ȡ�������ڵ�Ĳ���
	if (leftSize < 1)
	{
		return false;
	}
	unsigned char indexNodeNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < indexNodeNum * 10)
	{
		return false;
	}

	for (unsigned char i = 0; i < indexNodeNum; ++i)
	{
		unsigned short findValue = *((unsigned short*)p);
		p += 2;
		IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= indexNodeNum * 10;
	//���ֻ��Ҷ�ӽڵ㲿��
	unsigned char leafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < leafNum * 10)
	{
		return false;
	}

	for (unsigned char i = 0; i < leafNum; ++i)
	{
		unsigned short findValue = *((unsigned short*)p);
		p += 2;
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= leafNum * 10;
	//��Ӻ������ڵ�ͬһ����֧��ָ��ĩβ��ҳ�ڵ�
	unsigned char lastLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < lastLeafNum * 2)
	{
		return false;
	}

	for (unsigned char i = 0; i < lastLeafNum; ++i)
	{
		std::unordered_map<unsigned short, IndexNodeChild>::iterator it = children.find(*((unsigned short*)p));
		if (it == end(children))
		{
			return false;
		}
		it->second.childType = CHILD_TYPE_NODENLEAF;
		p += 2;
	}

	leftSize -= lastLeafNum * 2;


	//��ӱȽϵ���;�͵��ļ���β��Ҷ�ӽڵ�
	unsigned char endLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < endLeafNum * 8)
	{
		return false;
	}

	for (unsigned char i = 0; i < endLeafNum; ++i)
	{
		bool ok = leafSet.insert(*((unsigned long long*)p)).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}
	return true;
}

bool IndexNodeTypeFour::toBinary(char* buffer, int len)
{
	short totalSize = 0;
	char* p = buffer;
	p += 2;
	if (len < 34)
	{
		return false;
	}
	*((unsigned long long*)p) = start;
	p += 8;
	*((unsigned long long*)p) = len;
	p += 8;
	*((unsigned long long*)p) = preCmpLen;
	p += 8;
	*((unsigned long long*)p) = parentID;
	p += 8;
	totalSize = 32;
	int leftSize = len - 34;
	if (!children.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		unsigned char* indexNodeNum = (unsigned char*)p;
		*indexNodeNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		char leafBuffer[256 * 9];
		unsigned char leafNum = 0;
		char lastLeafBuffer[256 * 1];			//��Щָ��ĩβ��Ҷ�ӽڵ�������ڵ��غϵ�����
		unsigned char lastLeafNum = 0;
		for (auto& value : children)
		{
			if (value.second.childType == CHILD_TYPE_NODE)
			{
				if (leftSize < 9)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned char*)p) = value.first;
				p += 1;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 9;
				leftSize -= 9;
			}
			else if (value.second.childType == CHILD_TYPE_LEAF)
			{
				*((unsigned char*)(leafBuffer + 9 * leafNum)) = value.first;
				*((unsigned long long*)(leafBuffer + 9 * leafNum + 1)) = value.second.indexId;
				leafNum++;
			}
			else
			{
				if (leftSize < 9)
				{
					return false;
				}
				(*indexNodeNum)++;
				*((unsigned char*)p) = value.first;
				p += 1;
				*((unsigned long long*)p) = value.second.indexId;
				p += 8;
				totalSize += 9;
				leftSize -= 9;
				*((unsigned char*)(lastLeafBuffer + 1 * lastLeafNum)) = value.first;
				lastLeafNum++;
			}
		}
		//����ֻ��Ҷ�ӽڵ㲿��
		if (leftSize < 1 + leafNum * 9)
		{
			return false;
		}
		*p = leafNum;
		p++;
		memcpy(p, leafBuffer, leafNum * 9);
		p += leafNum * 9;
		totalSize += (1 + leafNum * 9);
		leftSize -= (1 + leafNum * 9);
		//���������ͬһ����֧��ĿǰΪָֹ���ļ�ĩβ��Ҷ�ӽڵ�ʹ�õ�����
		if (leftSize < 1 + lastLeafNum * 1)
		{
			return false;
		}
		*p = lastLeafNum;
		p++;
		memcpy(p, lastLeafBuffer, lastLeafNum * 1);
		p += lastLeafNum * 1;
		totalSize += (1 + lastLeafNum * 1);
		leftSize -= (1 + lastLeafNum * 1);
	}
	else
	{
		//û�к��ӽڵ�������0���ֽڱ�ʾ���ֽڵ���Ŀ������
		if (leftSize < 3)
		{
			return false;
		}
		*((short*)p) = 0;
		p += 2;
		*p = 0;
		p++;
		totalSize += 3;
		leftSize -= 3;
	}

	//��Щ�Ƚϵ������;�͵��ļ�ĩβ����ʱ�����֧��¼��Ҷ�ӽڵ㼯����
	if (!leafSet.empty())
	{
		if (leftSize < 1)
		{
			return false;
		}
		char* leafNum = p;
		*leafNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		for (auto& value : leafSet)
		{
			if (leftSize < 8)
			{
				return false;
			}
			(*leafNum)++;
			*((unsigned long long*)p) = value;
			p += 8;
			totalSize += 8;
			leftSize -= 8;
		}
	}
	else
	{
		//�յĻ�д���ʾ�ⲿ����0
		if (leftSize < 1)
		{
			return false;
		}
		*p = 0;
		totalSize += 1;
	}
	//���������Сд����ǰ��
	*((short*)buffer) = totalSize;
	return true;
}

bool IndexNodeTypeFour::toObject(char* buffer, int len)
{
	//�洢��ʱ��ǰ���м������ͻ��д洢�Ĵ�С����ǰ���3���ֽ�
	if ((len + 3) > 4 * 1024)
	{
		isBig = true;
	}
	char* p = buffer;
	int leftSize = len;
	if (leftSize < 32)
	{
		return false;
	}

	start = *((unsigned long long*)p);
	p += 8;
	len = *((unsigned long long*)p);
	p += 8;
	preCmpLen = *((unsigned long long*)p);
	p += 8;
	parentID = *((unsigned long long*)p);
	p += 8;
	leftSize -= 32;
	//�ȶ�ȡ�������ڵ�Ĳ���
	if (leftSize < 1)
	{
		return false;
	}
	unsigned char indexNodeNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < indexNodeNum * 9)
	{
		return false;
	}

	for (unsigned char i = 0; i < indexNodeNum; ++i)
	{
		unsigned char findValue = *((unsigned char*)p);
		p += 1;
		IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= indexNodeNum * 9;
	//���ֻ��Ҷ�ӽڵ㲿��
	unsigned char leafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < leafNum * 9)
	{
		return false;
	}

	for (unsigned char i = 0; i < leafNum; ++i)
	{
		unsigned char findValue = *((unsigned char*)p);
		p += 1;
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, *((unsigned long long*)p));
		bool ok = children.insert({ findValue, indexNodeChild }).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}

	leftSize -= leafNum * 9;
	//��Ӻ������ڵ�ͬһ����֧��ָ��ĩβ��ҳ�ڵ�
	unsigned char lastLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < lastLeafNum * 1)
	{
		return false;
	}

	for (unsigned char i = 0; i < lastLeafNum; ++i)
	{
		std::unordered_map<unsigned char, IndexNodeChild>::iterator it = children.find(*((unsigned char*)p));
		if (it == end(children))
		{
			return false;
		}
		it->second.childType = CHILD_TYPE_NODENLEAF;
		p += 1;
	}

	leftSize -= lastLeafNum * 1;


	//��ӱȽϵ���;�͵��ļ���β��Ҷ�ӽڵ�
	unsigned char endLeafNum = *p;
	p += 1;
	leftSize -= 1;
	if (leftSize < endLeafNum * 8)
	{
		return false;
	}

	for (unsigned char i = 0; i < endLeafNum; ++i)
	{
		bool ok = leafSet.insert(*((unsigned long long*)p)).second;
		if (!ok)
		{
			return false;
		}
		p += 8;
	}
	return true;
}