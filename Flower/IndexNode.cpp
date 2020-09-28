#include "IndexNode.h"
#include "string.h"

IndexNode::IndexNode()
{
	start = 0;
	len = 0;
	preCmpLen = 0;
}

IndexNodeChild::IndexNodeChild()
{
	childType = 0;
	indexId = 0;
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
		char* indexNodeNum = p;
		*indexNodeNum = 0;
		p += 1;
		totalSize++;
		leftSize--;
		char leafBuffer[256 * 16];
		char leafNum = 0;
		char lastLeafBuffer[256 * 8];			//��Щָ��ĩβ��Ҷ�ӽڵ�������ڵ��غϵ�����
		char lastLeafNum = 0;
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