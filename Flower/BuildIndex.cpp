#include "BuildIndex.h"
#include <memory.h>
#include "common.h"
#include <sys/stat.h>
#include "sys/time.h"

BuildIndex::BuildIndex()
{
	dstFileSize = 0;
}

bool BuildIndex::init(const char* fileName, Index* index, Index* kvIndex)
{
	if (index == nullptr)
	{
		return false;
	}

	if (fileName == nullptr)
	{
		return false;
	}

	char indexFileName[4096];
	memset(indexFileName, 0, sizeof(indexFileName));
	//获取索引文件的名字
	if (!getIndexPath(fileName, indexFileName))
	{
		return false;
	}

	char kVFileName[4096] = { 0 };

	//获取kv索引文件的名字
	if (!getKVFilePath(fileName, kVFileName))
	{
		return false;
	}

	//对目标文件进行初始化
	if (!dstFile.init(fileName))
	{
		return false;
	}

	if (!indexFile.init(indexFileName, index))
	{
		return false;
	}

	if (kvIndex != nullptr)
	{
		kvIndexFile.init(kVFileName, kvIndex);
	}

	//获取文件的大小
	struct stat statbuf;
	stat(fileName, &statbuf);
	dstFileSize = statbuf.st_size;
	return true;
}

bool BuildIndex::cutNodeSize(unsigned long long indexId, IndexNode*& indexNode, unsigned char buildType)
{
	if (indexNode == nullptr)
	{
		return false;
	}
	//首先先判断节点的大小是否比预计的还要大
	if (indexNode->getChildrenNum() <= 256)
	{
		return true;
	}

	//改变节点类型让节点的孩子结点的键小点这样孩子节点会少点
	IndexNode* newNode = changeNodeType(indexId, indexNode, buildType);

	if (newNode == nullptr)
	{
		return false;
	}

	//改变了节点类型但是还是无法排除当前节点可能比256要大和产生的新的孩子节点比256要大所以调用节点的函数改变节点
	indexNode = newNode->cutNodeSize(this, indexId, buildType);
	if (indexNode == nullptr)
	{
		return false;
	}
	return true;
}

IndexNode* BuildIndex::getIndexNode(unsigned long long indexId, unsigned char buildType)
{
	if (buildType == BUILD_TYPE_FILE)
	{
		return indexFile.getIndexNode(indexId);
	}
	return kvIndexFile.getIndexNode(indexId, BUILD_TYPE_KV);
}

bool BuildIndex::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen, unsigned char buildType)
{
	if (buildType == BUILD_TYPE_FILE)
	{
		return indexFile.changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
	}
	return kvIndexFile.changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
}

bool BuildIndex::mergeNode(unsigned long long preCmpLen, unsigned long long parentId, IndexNodeChild& leftChildNode, const IndexNodeChild& rightChildNode)
{
	//一种是两种都是叶子节点，一种是两个都是普通的节点，一种是一个是普通节点一个是叶子节点。
	unsigned char leftType = leftChildNode.getType();
	unsigned char rightType = rightChildNode.getType();
	if (leftType == CHILD_TYPE_LEAF && rightType == CHILD_TYPE_LEAF)
	{
		unsigned long long leftFilePos = leftChildNode.getIndexId();
		unsigned long long rightFilePos = rightChildNode.getIndexId();
		//如果开始比较位置不是8的整数倍的先比较前面那一部分
		unsigned long long offset = leftFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = 8 - offset;
			//从文件当中两个位置当中进行读取剩下字节的数据
			unsigned char leftData[8];
			unsigned char rightData[8];
			fpos_t leftPos;
			leftPos.__pos = leftFilePos;
			if (!dstFile.read(leftPos, leftData, needChartoEight))
			{
				return false;
			}
			fpos_t rightPos;
			rightPos.__pos = rightFilePos;
			if (!dstFile.read(rightPos, rightData, needChartoEight))
			{
				return false;
			}
			int curCmpLen = 0;
			while (cmpLen != needChartoEight)
			{
				if ((needChartoEight - cmpLen) % 4 == 0)
				{
					if (*(int*)(&leftData[cmpLen]) == *(int*)(&rightData[cmpLen]))
					{
						cmpLen += 4;
					}
					else
					{
						curCmpLen = 4;
						break;
					}
				}
				else if ((needChartoEight - cmpLen) % 2 == 0)
				{
					if (*(short*)(&leftData[cmpLen]) == *(short*)(&rightData[cmpLen]))
					{
						cmpLen += 2;
					}
					else
					{
						curCmpLen = 2;
						break;
					}
				}
				else
				{
					if (*(char*)(&leftData[cmpLen]) == *(char*)(&rightData[cmpLen]))
					{
						cmpLen++;
					}
					else
					{
						curCmpLen = 1;
						break;
					}
				}
			}

			if (cmpLen != needChartoEight)
			{
				//前面不够8个字节里面有不一样的地方
				//根据不同的字节创建不同的对象
				IndexNode* pNode = nullptr;
				switch (curCmpLen)
				{
				case 4:
					pNode = indexFile.newIndexNode(NODE_TYPE_TWO, preCmpLen);
					break;
				case 2:
					pNode = indexFile.newIndexNode(NODE_TYPE_THREE, preCmpLen);
					break;
				case 1:
					pNode = indexFile.newIndexNode(NODE_TYPE_FOUR, preCmpLen);
					break;
				default:
					break;
				}

				if (pNode == nullptr)
				{
					return false;
				}

				//设置这个新设置的节点的各个参数
				pNode->setStart(leftFilePos);
				pNode->setLen(cmpLen);
				pNode->setParentID(parentId);
				switch (pNode->getType())
				{
				case NODE_TYPE_TWO:
				{
					IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNode;
					IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leftFilePos + cmpLen + 4);

					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&leftData[cmpLen]), indexNodeChild))
					{
						return false;
					}
					IndexNodeChild rIndexNodeChild(CHILD_TYPE_LEAF, rightFilePos + cmpLen + 4);
					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&rightData[cmpLen]), rIndexNodeChild))
					{
						return false;
					}
				}
				break;
				case NODE_TYPE_THREE:
				{
					IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)pNode;
					IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leftFilePos + cmpLen + 2);
					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&leftData[cmpLen]), indexNodeChild))
					{
						return false;
					}
					
					IndexNodeChild rIndexNodeChild(CHILD_TYPE_LEAF, rightFilePos + cmpLen + 2);
					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&rightData[cmpLen]), rIndexNodeChild))
					{
						return false;
					}
				}
				break;
				case NODE_TYPE_FOUR:
				{
					IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)pNode;
					IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leftFilePos + cmpLen + 1);
					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&leftData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_LEAF, rightFilePos + cmpLen + 1);
					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&rightData[cmpLen]), rIndexNodeChild))
					{
						return false;
					}
				}
				break;
				default:
					break;
				}

				//已经修改了节点设置节点为已经修改状态
				pNode->setIsModified(true);

				//两个叶节点合并成一个索引节点了以后设置左边的那个孩子节点
				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNode->getIndexId());

				return true;
			}
		}

		//前面不足8个字节的部分已经比较完接下来比较长长的那部分。
		//文件以每次读取4k个字节这样读。这样避免一次读太多内存也不够。
		unsigned char* leftBuffer = (unsigned char*)malloc(4 * 1024);
		if (leftBuffer == nullptr)
		{
			return false;
		}

		unsigned char* rightBuffer = (unsigned char*)malloc(4 * 1024);

		if (rightBuffer == nullptr)
		{
			free(leftBuffer);
			return false;
		}

		//接下来需要比较的最多字节数
		unsigned long long leftRemainSize = dstFileSize - leftFilePos;
		unsigned long long rightRemainSize = dstFileSize - rightFilePos;
		unsigned long long remainReadSize = leftRemainSize;
		if (rightRemainSize < remainReadSize)
		{
			remainReadSize = rightRemainSize;
		}

		while (cmpLen + 4 * 1024 <= remainReadSize)
		{
			//从两个文件当中把相应的部分读取出来
			fpos_t leftPos;
			leftPos.__pos = leftFilePos + cmpLen;
			if (!dstFile.read(leftPos, leftBuffer, 4 * 1024))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
			fpos_t rightPos;
			rightPos.__pos = rightFilePos + cmpLen;
			if (!dstFile.read(rightPos, rightBuffer, 4 * 1024))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			unsigned long long subCmpLen = 0;
			for (; subCmpLen < 4 * 1024; subCmpLen += 8)
			{
				if (*(unsigned long long*)(&leftBuffer[subCmpLen]) != *(unsigned long long*)(&rightBuffer[subCmpLen]))
				{
					break;
				}
			}

			if (subCmpLen != 4 * 1024)
			{
				IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);
				
				if (pNode == nullptr)
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				pNode->setStart(leftFilePos);
				pNode->setLen(cmpLen + subCmpLen);
				pNode->setParentID(parentId);

				IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leftFilePos + cmpLen + subCmpLen + 8);

				if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&leftBuffer[subCmpLen]), indexNodeChild))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				IndexNodeChild rIndexNodeChild(CHILD_TYPE_LEAF, rightFilePos + cmpLen + subCmpLen + 8);

				if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&rightBuffer[subCmpLen]), rIndexNodeChild))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				pNode->setIsModified(true);

				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNode->getIndexId());

				free(leftBuffer);
				free(rightBuffer);
				return true;
			}

			cmpLen += 4 * 1024;
		}

		if (cmpLen == remainReadSize)
		{
			IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

			if (pNode == nullptr)
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			unsigned long long startPos = leftFilePos;
			unsigned long long nodeLen = leftRemainSize;
			if (rightFilePos < leftFilePos)
			{
				startPos = rightFilePos;
				nodeLen = rightRemainSize;
			}
			pNode->setStart(startPos);

			nodeLen -= dstFileSize % 8;
			pNode->setLen(nodeLen);
			pNode->setParentID(parentId);

			pNode->insertLeafSet(leftFilePos - preCmpLen);
			pNode->insertLeafSet(rightFilePos - preCmpLen);

			pNode->setIsModified(true);

			leftChildNode.setChildType(CHILD_TYPE_NODE);
			leftChildNode.setIndexId(pNode->getIndexId());

			free(leftBuffer);
			free(rightBuffer);
			return true;
		}

		//后面的比较长度不够4k
		unsigned long long lastNeedReadSize = remainReadSize - cmpLen;

		fpos_t leftPos;
		leftPos.__pos = leftFilePos + cmpLen;
		if (!dstFile.read(leftPos, leftBuffer, lastNeedReadSize))
		{
			free(leftBuffer);
			free(rightBuffer);
			return false;
		}
		fpos_t rightPos;
		rightPos.__pos = rightFilePos + cmpLen;
		if (!dstFile.read(rightPos, rightBuffer, lastNeedReadSize))
		{
			free(leftBuffer);
			free(rightBuffer);
			return false;
		}

		unsigned long long subCmpLen = 0;
		for (; subCmpLen + 8 <= lastNeedReadSize; subCmpLen += 8)
		{
			if (*(unsigned long long*)(&leftBuffer[subCmpLen]) != *(unsigned long long*)(&rightBuffer[subCmpLen]))
			{
				break;
			}
		}

		if (subCmpLen + 8 <= lastNeedReadSize)
		{
			IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

			if (pNode == nullptr)
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			pNode->setStart(leftFilePos);
			pNode->setLen(cmpLen + subCmpLen);
			pNode->setParentID(parentId);

			IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;
			IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leftFilePos + cmpLen + subCmpLen + 8);

			if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&leftBuffer[subCmpLen]), indexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			IndexNodeChild rIndexNodeChild(CHILD_TYPE_LEAF, rightFilePos + cmpLen + subCmpLen + 8);

			if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&rightBuffer[subCmpLen]), rIndexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			pNode->setIsModified(true);

			leftChildNode.setChildType(CHILD_TYPE_NODE);
			leftChildNode.setIndexId(pNode->getIndexId());

			free(leftBuffer);
			free(rightBuffer);
			return true;
		}

		//接下来还有剩下不够8个字节的部分
		unsigned long long suppleSize = 0;
		for (; subCmpLen + suppleSize < lastNeedReadSize; ++suppleSize)
		{
			if (*(unsigned char*)(&leftBuffer[subCmpLen + suppleSize]) != *(unsigned char*)(&rightBuffer[subCmpLen + suppleSize]))
			{
				break;
			}
		}

		if ((subCmpLen + suppleSize) == lastNeedReadSize)
		{
			//两个叶子节点前面的部分全部都一样
			IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

			if (pNode == nullptr)
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			unsigned long long startPos = leftFilePos;
			unsigned long long nodeLen = leftRemainSize;
			if (rightFilePos < leftFilePos)
			{
				startPos = rightFilePos;
				nodeLen = rightRemainSize;
			}
			pNode->setStart(startPos);
			nodeLen -= dstFileSize % 8;
			pNode->setLen(nodeLen);
			pNode->setParentID(parentId);

			pNode->insertLeafSet(leftFilePos - preCmpLen);
			pNode->insertLeafSet(rightFilePos - preCmpLen);

			pNode->setIsModified(true);

			leftChildNode.setChildType(CHILD_TYPE_NODE);
			leftChildNode.setIndexId(pNode->getIndexId());

			free(leftBuffer);
			free(rightBuffer);
			return true;
		}

		//最后几个字节不一样
		IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

		if (pNode == nullptr)
		{
			free(leftBuffer);
			free(rightBuffer);
			return false;
		}

		unsigned long long chooseFilePos = leftFilePos;
		unsigned long long anotherFilePos = rightFilePos;
		unsigned char* chooseBuffer = leftBuffer;
		if (rightFilePos < chooseFilePos)
		{
			chooseFilePos = rightFilePos;
			anotherFilePos = leftFilePos;
			chooseBuffer = rightBuffer;
		}

		//读取完整的key值
		fpos_t pos;
		pos.__pos = chooseFilePos + remainReadSize;
		if (!dstFile.read(pos, &chooseBuffer[lastNeedReadSize], subCmpLen + 8 - lastNeedReadSize))
		{
			free(leftBuffer);
			free(rightBuffer);
			return false;
		}

		pNode->setStart(leftFilePos);
		pNode->setLen(cmpLen + subCmpLen);
		pNode->setParentID(parentId);

		IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, chooseFilePos + cmpLen + subCmpLen + 8);

		if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&chooseBuffer[subCmpLen]), indexNodeChild))
		{
			free(leftBuffer);
			free(rightBuffer);
			return false;
		}

		pNode->insertLeafSet(anotherFilePos - preCmpLen);

		pNode->setIsModified(true);

		leftChildNode.setChildType(CHILD_TYPE_NODE);
		leftChildNode.setIndexId(pNode->getIndexId());

		free(leftBuffer);
		free(rightBuffer);
		return true;
	}
	else if (leftType == CHILD_TYPE_NODE && rightType == CHILD_TYPE_NODE)
	{
		//合并的两个孩子节点都是非叶子节点
		IndexNode* leftNode = indexFile.getIndexNode(leftChildNode.getIndexId());
		if (leftNode == nullptr)
		{
			return false;
		}

		IndexNode* rightNode = indexFile.getIndexNode(rightChildNode.getIndexId());
		if (rightNode == nullptr)
		{
			return false;
		}

		unsigned long long leftFilePos = leftNode->getStart();
		unsigned long long rightFilePos = rightNode->getStart();

		unsigned long long leftRemainSize = leftNode->getLen();
		unsigned long long rightRemainSize = rightNode->getLen();
		unsigned long long remainReadSize = leftRemainSize;
		if (rightRemainSize < remainReadSize)
		{
			remainReadSize = rightRemainSize;
		}

		//如果开始比较位置不是8的整数倍的先比较前面那一部分
		unsigned long long offset = leftFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = 8 - offset;

			//有可能剩下的读取大小比8个字节还要小
			if (remainReadSize < needChartoEight)
			{
				needChartoEight = remainReadSize;
			}

			//从文件当中两个位置当中读取剩下的字节的数据
			unsigned char leftData[8];
			unsigned char rightData[8];
			if (needChartoEight != 0)
			{
				fpos_t leftPos;
				leftPos.__pos = leftFilePos;
				if (!dstFile.read(leftPos, leftData, needChartoEight))
				{
					return false;
				}
				fpos_t rightPos;
				rightPos.__pos = rightFilePos;
				if (!dstFile.read(rightPos, rightData, needChartoEight))
				{
					return false;
				}
			}

			int curCmpLen = 0;
			while (cmpLen != needChartoEight)
			{
				if ((needChartoEight - cmpLen) % 4 == 0)
				{
					if (*(int*)(&leftData[cmpLen]) == *(int*)(&rightData[cmpLen]))
					{
						cmpLen += 4;
					}
					else
					{
						curCmpLen = 4;
						break;
					}
				}
				else if ((needChartoEight - cmpLen) % 2 == 0)
				{
					if (*(short*)(&leftData[cmpLen]) == *(short*)(&rightData[cmpLen]))
					{
						cmpLen += 2;
					}
					else
					{
						curCmpLen = 2;
						break;
					}
				}
				else
				{
					if (*(char*)(&leftData[cmpLen]) == *(char*)(&rightData[cmpLen]))
					{
						cmpLen++;
					}
					else
					{
						curCmpLen = 1;
						break;
					}
				}
			}

			if (cmpLen != needChartoEight)
			{
				//前面不够8个字节里面有不一样的地方
				//根据不同的字节创建不同的对象
				IndexNode* pNode = nullptr;
				switch (curCmpLen)
				{
				case 4:
					pNode = indexFile.newIndexNode(NODE_TYPE_TWO, preCmpLen);
					break;
				case 2:
					pNode = indexFile.newIndexNode(NODE_TYPE_THREE, preCmpLen);
					break;
				case 1:
					pNode = indexFile.newIndexNode(NODE_TYPE_FOUR, preCmpLen);
					break;
				default:
					break;
				}

				if (pNode == nullptr)
				{
					return false;
				}

				//设置这个新设置的节点的各个参数
				pNode->setStart(leftFilePos);
				pNode->setLen(cmpLen);
				pNode->setParentID(parentId);
				switch (pNode->getType())
				{
				case NODE_TYPE_TWO:
				{

					IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNode;

					//左边节点修改相应的节点长度
					leftNode->setStart(leftNode->getStart() + cmpLen + 4);
					leftNode->setLen(leftNode->getLen() - cmpLen - 4);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + 4))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&leftData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					//右边节点修改相应节点的长度
					rightNode->setStart(rightNode->getStart() + cmpLen + 4);
					rightNode->setLen(rightNode->getLen() - cmpLen - 4);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + 4))
					{
						return false;
					}
					rightNode->setParentID(pNode->getIndexId());
					rightNode->setIsModified(true);

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());
					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&rightData[cmpLen]), rIndexNodeChild))
					{
						return false;
					}
				}
					break;
				case NODE_TYPE_THREE:
				{

					IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)pNode;

					//左边节点修改相应节点长度
					leftNode->setStart(leftNode->getStart() + cmpLen + 2);
					leftNode->setLen(leftNode->getLen() - cmpLen - 2);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + 2))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&leftData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					//右边节点修改相应节点的长度
					rightNode->setStart(rightNode->getStart() + cmpLen + 2);
					rightNode->setLen(rightNode->getLen() - cmpLen - 2);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + 2))
					{
						return false;
					}

					rightNode->setParentID(pNode->getIndexId());
					rightNode->setIsModified(true);

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());
					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&rightData[cmpLen]), rIndexNodeChild))
					{
						return false;
					}
				}
					break;
				case NODE_TYPE_FOUR:
				{ 

					IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)pNode;

					//左边节点修改相应节点长度
					leftNode->setStart(leftNode->getStart() + cmpLen + 1);
					leftNode->setLen(leftNode->getLen() - cmpLen - 1);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + 1))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&leftData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					//右边节点修改相应节点的长度
					rightNode->setStart(rightNode->getStart() + cmpLen + 1);
					rightNode->setLen(rightNode->getLen() - cmpLen - 1);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + 1))
					{
						return false;
					}

					rightNode->setParentID(pNode->getIndexId());
					rightNode->setIsModified(true);

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());
					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&rightData[cmpLen]), rIndexNodeChild))
					{
						return false;
					}
				}
					break;
				default:
					break;
				}

				//已经修改了节点设置节点为已经修改状态
				pNode->setIsModified(true);

				//两个节点合并成一个节点了以后设置左边的那个孩子节点
				leftChildNode.setIndexId(pNode->getIndexId());

				return true;
			}
		}

		if (cmpLen == remainReadSize)
		{
			//其中一个节点的所有部分和另外一个节点相同这个时候有两种情况,一种情况是长度相同,另一种情况是长度不同
			if (leftRemainSize == rightRemainSize)
			{
				//改变节点的类型让两个节点的类型相同
				while (leftNode->getType() != rightNode->getType())
				{
					if (compareTwoType(leftNode->getType(), rightNode->getType()))
					{
						rightNode = changeNodeType(rightNode->getIndexId(), rightNode);
						if (rightNode == nullptr)
						{
							return false;
						}
					}
					else
					{
						leftNode = changeNodeType(leftNode->getIndexId(), leftNode);
						if (leftNode == nullptr)
						{
							return false;
						}
					}
				}

				//把两个长度相同类型相同的节点合并
				if (!leftNode->mergeSameLenNode(this, rightNode))
				{
					return false;
				}

				//合并完成了以后左边的节点可能比较大要缩小一下节点
				if (!cutNodeSize(leftNode->getIndexId(), leftNode))
				{
					return false;
				}

				leftNode->setIsModified(true);

				//右边的节点完全融入了左边的节点所以右边的节点可以说是完全不存在删除
				indexFile.deleteIndexNode(rightNode->getIndexId());

				leftChildNode.setIndexId(leftNode->getIndexId());

				return true;
			}

			//有一个节点比较长一个节点比较短
			IndexNode* longNode = leftNode;
			IndexNode* anotherNode = rightNode;
			unsigned long long longNodeStart = leftFilePos;
			if (leftRemainSize < rightRemainSize)
			{
				longNode = rightNode;
				anotherNode = leftNode;
				longNodeStart = rightFilePos;
			}

			anotherNode->appendLeafSet(longNode, cmpLen + 8, dstFileSize);

			//从一个节点添加进另一个节点
			switch (anotherNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				unsigned long long key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 8))
				{
					return false;
				}

				anotherNode->setParentID(parentId);
				
				//修改长节点的长度
				longNode->setStart(longNode->getStart() + cmpLen + 8);
				longNode->setLen(longNode->getLen() - cmpLen - 8);

				if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 8))
				{
					return false;
				}

				longNode->setParentID(anotherNode->getIndexId());
				longNode->setIsModified(true);

				IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

				IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)anotherNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			case NODE_TYPE_TWO:
			{
				unsigned int key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 4))
				{
					return false;
				}

				anotherNode->setParentID(parentId);

				//修改长节点的长度
				longNode->setStart(longNode->getStart() + cmpLen + 4);
				longNode->setLen(longNode->getLen() - cmpLen - 4);

				if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 4))
				{
					return false;
				}

				longNode->setParentID(anotherNode->getIndexId());
				longNode->setIsModified(true);

				IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

				IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)anotherNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			case NODE_TYPE_THREE:
			{
				unsigned short key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 2))
				{
					return false;
				}

				anotherNode->setParentID(parentId);

				//修改长节点的长度
				longNode->setStart(longNode->getStart() + cmpLen + 2);
				longNode->setLen(longNode->getLen() - cmpLen - 2);

				if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 2))
				{
					return false;
				}

				longNode->setParentID(anotherNode->getIndexId());
				longNode->setIsModified(true);

				IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

				IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)anotherNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			case NODE_TYPE_FOUR:
			{
				unsigned char key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 1))
				{
					return false;
				}

				anotherNode->setParentID(parentId);

				//修改长节点的长度
				longNode->setStart(longNode->getStart() + cmpLen + 1);
				longNode->setLen(longNode->getLen() - cmpLen - 1);

				if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 1))
				{
					return false;
				}

				longNode->setParentID(anotherNode->getIndexId());
				longNode->setIsModified(true);

				IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

				IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)anotherNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			default:
				break;
			}

			//比较小的节点加入了新的节点可能比较大缩小下大小
			if (!cutNodeSize(anotherNode->getIndexId(), anotherNode))
			{
				return false;
			}

			anotherNode->setIsModified(true);

			leftChildNode.setIndexId(anotherNode->getIndexId());

			return true;
		}

		//前面不够8个字节的比较完了下面处理长长的那一串
		//文件以每次读取4k个字节这样读。这样避免一次读太多内存也不够。
		unsigned char* leftBuffer = (unsigned char*)malloc(4 * 1024);
		if (leftBuffer == nullptr)
		{
			return false;
		}

		unsigned char* rightBuffer = (unsigned char*)malloc(4 * 1024);

		if (rightBuffer == nullptr)
		{
			free(leftBuffer);
			return false;
		}

		while (cmpLen + 4 * 1024 <= remainReadSize)
		{
			//从两个文件当中把相应的部分读取出来
			fpos_t leftPos;
			leftPos.__pos = leftFilePos + cmpLen;
			if (!dstFile.read(leftPos, leftBuffer, 4 * 1024))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
			fpos_t rightPos;
			rightPos.__pos = rightFilePos + cmpLen;
			if (!dstFile.read(rightPos, rightBuffer, 4 * 1024))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			unsigned long long subCmpLen = 0;
			for (; subCmpLen < 4 * 1024; subCmpLen += 8)
			{
				if (*(unsigned long long*)(&leftBuffer[subCmpLen]) != *(unsigned long long*)(&rightBuffer[subCmpLen]))
				{
					break;
				}
			}

			if (subCmpLen != 4 * 1024)
			{
				//中间有不一样的地方
				IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

				if (pNode == nullptr)
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				pNode->setStart(leftFilePos);
				pNode->setLen(cmpLen + subCmpLen);
				pNode->setParentID(parentId);

				//从两个节点当中的相同部分前面的叶子节点接入到公共节点
				if (!pNode->appendLeafSet(leftNode, cmpLen + subCmpLen + 8, dstFileSize))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				if (!pNode->appendLeafSet(rightNode, cmpLen + subCmpLen + 8, dstFileSize))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				//修改左边节点的长度
				leftNode->setStart(leftNode->getStart() + cmpLen + subCmpLen + 8);
				leftNode->setLen(leftNode->getLen() - cmpLen - subCmpLen - 8);

				if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				leftNode->setParentID(pNode->getIndexId());
				leftNode->setIsModified(true);

				IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

				IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;

				if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&leftBuffer[subCmpLen]), indexNodeChild))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				//右边节点修改相应节点的长度
				rightNode->setStart(rightNode->getStart() + cmpLen + subCmpLen + 8);
				rightNode->setLen(rightNode->getLen() - cmpLen - subCmpLen - 8);

				if (!indexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				rightNode->setParentID(pNode->getIndexId());
				rightNode->setIsModified(true);

				IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());

				if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&rightBuffer[subCmpLen]), rIndexNodeChild))
				{
					free(leftBuffer);
					free(rightBuffer);
					return false;
				}

				pNode->setIsModified(true);

				leftChildNode.setIndexId(pNode->getIndexId());

				free(leftBuffer);
				free(rightBuffer);
				return true;
			}

			cmpLen += 4 * 1024;
		}

		//后面比较的长度不够4k
		unsigned long long lastNeedReadSize = remainReadSize - cmpLen;
		if (lastNeedReadSize != 0)
		{
			fpos_t leftPos;
			leftPos.__pos = leftFilePos + cmpLen;
			if (!dstFile.read(leftPos, leftBuffer, lastNeedReadSize))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
			fpos_t rightPos;
			rightPos.__pos = rightFilePos + cmpLen;
			if (!dstFile.read(rightPos, rightBuffer, lastNeedReadSize))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
		}

		unsigned long long subCmpLen = 0;
		for (; subCmpLen < lastNeedReadSize; subCmpLen += 8)
		{
			if (*(unsigned long long*)(&leftBuffer[subCmpLen]) != *(unsigned long long*)(&rightBuffer[subCmpLen]))
			{
				break;
			}
		}

		if (subCmpLen < lastNeedReadSize)
		{
			//这一段有不一样的地方
			IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

			if (pNode == nullptr)
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			pNode->setStart(leftFilePos);
			pNode->setLen(cmpLen + subCmpLen);
			pNode->setParentID(parentId);

			//从两个节点当中的相同部分前面的叶子节点接入到公共节点
			if (!pNode->appendLeafSet(leftNode, cmpLen + subCmpLen + 8, dstFileSize))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			if (!pNode->appendLeafSet(rightNode, cmpLen + subCmpLen + 8, dstFileSize))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			//修改左边节点的长度
			leftNode->setStart(leftNode->getStart() + cmpLen + subCmpLen + 8);
			leftNode->setLen(leftNode->getLen() - cmpLen - subCmpLen - 8);

			if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			leftNode->setParentID(pNode->getIndexId());
			leftNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

			IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;

			if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&leftBuffer[subCmpLen]), indexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			//右边节点修改相应节点的长度
			rightNode->setStart(rightNode->getStart() + cmpLen + subCmpLen + 8);
			rightNode->setLen(rightNode->getLen() - cmpLen - subCmpLen - 8);

			if (!indexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			rightNode->setParentID(pNode->getIndexId());
			rightNode->setIsModified(true);

			IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());

			if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&rightBuffer[subCmpLen]), rIndexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			pNode->setIsModified(true);

			leftChildNode.setIndexId(pNode->getIndexId());

			free(leftBuffer);
			free(rightBuffer);
			return true;
		}

		cmpLen += subCmpLen;

		//前面所有的比较都是一样的
		if (leftRemainSize == rightRemainSize)
		{
			//改变节点的类型让两个节点的类型相同
			while (leftNode->getType() != rightNode->getType())
			{
				if (compareTwoType(leftNode->getType(), rightNode->getType()))
				{
					rightNode = changeNodeType(rightNode->getIndexId(), rightNode);
					if (rightNode == nullptr)
					{
						free(leftBuffer);
						free(rightBuffer);
						return false;
					}
				}
				else
				{
					leftNode = changeNodeType(leftNode->getIndexId(), leftNode);
					if (leftNode == nullptr)
					{
						free(leftBuffer);
						free(rightBuffer);
						return false;
					}
				}
			}

			//把两个长度相同类型相同的节点合并
			if (!leftNode->mergeSameLenNode(this, rightNode))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			//合并完成了以后左边的节点可能比较大要缩小一下节点
			if (!cutNodeSize(leftNode->getIndexId(), leftNode))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			leftNode->setIsModified(true);

			//右边的节点完全融入了左边的节点所以右边的节点可以说完全不存在删除
			indexFile.deleteIndexNode(rightNode->getIndexId());

			leftChildNode.setIndexId(leftNode->getIndexId());

			free(leftBuffer);
			free(rightBuffer);
			return true;
		}

		//有一个节点比较长一个节点比较短
		IndexNode* longNode = leftNode;
		IndexNode* anotherNode = rightNode;
		unsigned long long longNodeStart = leftFilePos;
		if (leftRemainSize < rightRemainSize)
		{
			longNode = rightNode;
			anotherNode = leftNode;
			longNodeStart = rightFilePos;
		}

		//把长节点前面相同部分的叶子节点加进去
		anotherNode->appendLeafSet(longNode, cmpLen + 8, dstFileSize);

		//从一个节点添加进另一个节点
		switch (anotherNode->getType())
		{
		case NODE_TYPE_ONE:
		{
			unsigned long long key;
			//从文件当中读取key
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 8))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//修改长节点的长度
			longNode->setStart(longNode->getStart() + cmpLen + 8);
			longNode->setLen(longNode->getLen() - cmpLen - 8);

			if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 8))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			longNode->setParentID(anotherNode->getIndexId());
			longNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

			IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)anotherNode;

			if (!tmpNode->insertChildNode(this, key, indexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
		}
			break;
		case NODE_TYPE_TWO:
		{
			unsigned int key;
			//从文件当中读取key
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 4))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//修改长节点的长度

			longNode->setStart(longNode->getStart() + cmpLen + 4);
			longNode->setLen(longNode->getLen() - cmpLen - 4);

			if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 4))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			longNode->setParentID(anotherNode->getIndexId());
			longNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

			IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)anotherNode;

			if (!tmpNode->insertChildNode(this, key, indexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
		}
			break;
		case NODE_TYPE_THREE:
		{
			unsigned short key;
			//从文件当中读取key
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 2))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//修改长节点的长度
			longNode->setStart(longNode->getStart() + cmpLen + 2);
			longNode->setLen(longNode->getLen() - cmpLen - 2);

			if (!indexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 2))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			longNode->setParentID(anotherNode->getIndexId());
			longNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

			IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)anotherNode;

			if (!tmpNode->insertChildNode(this, key, indexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
		}
			break;
		case NODE_TYPE_FOUR:
		{
			unsigned char key;
			//从文件当中读取key
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 1))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//修改长节点的长度
			longNode->setParentID(anotherNode->getIndexId());
			longNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

			IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)anotherNode;

			if (!tmpNode->insertChildNode(this, key, indexNodeChild))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}
		}
			break;
		default:
			break;
		}

		//比较小的节点加入了新的节点可能比较大缩小下大小
		if (!cutNodeSize(anotherNode->getIndexId(), anotherNode))
		{
			free(leftBuffer);
			free(rightBuffer);
			return false;
		}

		anotherNode->setIsModified(true);

		leftChildNode.setIndexId(anotherNode->getIndexId());

		free(leftBuffer);
		free(rightBuffer);
		return true;
	}
	else
	{
		//其中一个节点是非叶子节点,另一个节点是叶子节点
		unsigned long long nodeIndexId = leftChildNode.getIndexId();
		unsigned long long leafFilePos = rightChildNode.getIndexId();
		if (rightChildNode.getType() == CHILD_TYPE_NODE)
		{
			nodeIndexId = rightChildNode.getIndexId();
			leafFilePos = leftChildNode.getIndexId();
		}

		IndexNode* pNotLeafNode = indexFile.getIndexNode(nodeIndexId);
		if (pNotLeafNode == nullptr)
		{
			return false;
		}

		unsigned long long nodeFilePos = pNotLeafNode->getStart();
		unsigned long long leafRemainSize = dstFileSize - leafFilePos;
		unsigned long long nodeRemainSize = pNotLeafNode->getLen();
		unsigned long long remainReadSize = leafRemainSize;
		if (nodeRemainSize < remainReadSize)
		{
			remainReadSize = nodeRemainSize;
		}

		//如果开始比较位置不是8的整数倍的先比较前面那一部分
		unsigned long long offset = leafFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = 8 - offset;

			//有可能剩下的读取大小比8个字节还小
			if (remainReadSize < needChartoEight)
			{
				needChartoEight = remainReadSize;
			}

			//从文件当中两个位置当中读取剩下的字节的数据
			unsigned char leafData[8];
			unsigned char nodeData[8];
			if (needChartoEight != 0)
			{
				fpos_t leafPos;
				leafPos.__pos = leafFilePos;
				if (!dstFile.read(leafPos, leafData, needChartoEight))
				{
					return false;
				}
				fpos_t nodePos;
				nodePos.__pos = nodeFilePos;
				if (!dstFile.read(nodePos, nodeData, needChartoEight))
				{
					return false;
				}
			}

			int curCmpLen = 0;
			while (cmpLen != needChartoEight)
			{
				if ((needChartoEight - cmpLen) % 4 == 0)
				{
					if (*(int*)(&leafData[cmpLen]) == *(int*)(&nodeData[cmpLen]))
					{
						cmpLen += 4;
					}
					else
					{
						curCmpLen = 4;
						break;
					}
				}
				else if ((needChartoEight - cmpLen) % 2 == 0)
				{
					if (*(short*)(&leafData[cmpLen]) == *(short*)(&nodeData[cmpLen]))
					{
						cmpLen += 2;
					}
					else
					{
						curCmpLen = 2;
						break;
					}
				}
				else
				{
					if (*(char*)(&leafData[cmpLen]) == *(char*)(&nodeData[cmpLen]))
					{
						cmpLen += 1;
					}
					else
					{
						curCmpLen = 1;
						break;
					}
				}
			}

			if (cmpLen != needChartoEight)
			{
				//前面不够8个字节里面有不一样的地方
				//根据不同的字节创建不同的对象
				IndexNode* pNode = nullptr;
				switch (curCmpLen)
				{
				case 4:
					pNode = indexFile.newIndexNode(NODE_TYPE_TWO, preCmpLen);
					break;
				case 2:
					pNode = indexFile.newIndexNode(NODE_TYPE_THREE, preCmpLen);
					break;
				case 1:
					pNode = indexFile.newIndexNode(NODE_TYPE_FOUR, preCmpLen);
					break;
				default:
					break;
				}

				if (pNode == nullptr)
				{
					return false;
				}

				//设置这个新设置的节点的各个参数
				pNode->setStart(leafFilePos);
				pNode->setLen(cmpLen);
				pNode->setParentID(parentId);
				switch (pNode->getType())
				{
				case NODE_TYPE_TWO:
				{
					IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNode;

					//非叶子节点修改相应的节点长度
					pNotLeafNode->setStart(pNotLeafNode->getStart() + cmpLen + 4);
					pNotLeafNode->setLen(pNotLeafNode->getLen() - cmpLen - 4);
					
					if (!indexFile.changePreCmpLen(pNotLeafNode->getIndexId(), pNotLeafNode->getPreCmpLen(), pNotLeafNode->getPreCmpLen() + cmpLen + 4))
					{
						return false;
					}
					pNotLeafNode->setParentID(pNode->getIndexId());
					pNotLeafNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, pNotLeafNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&nodeData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					//插入叶子节点
					IndexNodeChild lIndexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 4);
					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&leafData[cmpLen]), lIndexNodeChild))
					{
						return false;
					}
				}
				break;
				case NODE_TYPE_THREE:
				{
					IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)pNode;

					//非叶子节点修改相应的节点长度
					pNotLeafNode->setStart(pNotLeafNode->getStart() + cmpLen + 2);
					pNotLeafNode->setLen(pNotLeafNode->getLen() - cmpLen - 2);

					if (!indexFile.changePreCmpLen(pNotLeafNode->getIndexId(), pNotLeafNode->getPreCmpLen(), pNotLeafNode->getPreCmpLen() + cmpLen + 2))
					{
						return false;
					}
					pNotLeafNode->setParentID(pNode->getIndexId());
					pNotLeafNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, pNotLeafNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&nodeData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					IndexNodeChild lIndexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 2);
					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&leafData[cmpLen]), lIndexNodeChild))
					{
						return false;
					}
				}
					break;
				case NODE_TYPE_FOUR:
				{
					IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)pNode;

					pNotLeafNode->setStart(pNotLeafNode->getStart() + cmpLen + 1);
					pNotLeafNode->setLen(pNotLeafNode->getLen() - cmpLen - 1);

					if (!indexFile.changePreCmpLen(pNotLeafNode->getIndexId(), pNotLeafNode->getPreCmpLen(), pNotLeafNode->getPreCmpLen() + cmpLen + 1))
					{
						return false;
					}
					pNotLeafNode->setParentID(pNode->getIndexId());
					pNotLeafNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, pNotLeafNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&nodeData[cmpLen]), indexNodeChild))
					{
						return false;
					}

					IndexNodeChild lIndexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 1);
					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&leafData[cmpLen]), lIndexNodeChild))
					{
						return false;
					}
				}
					break;
				default:
					break;
				}

				//已经修改了节点设置节点为已经修改状态
				pNode->setIsModified(true);

				//两个节点合并成一个节点了以后设置左边的那个孩子节点
				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNode->getIndexId());

				return true;
			}
		}

		if (cmpLen == remainReadSize)
		{
			//其中一个节点的所有部分和另外一个节点相同这个时候有2种情况,一种是可以直接插入叶子节点,另一种是在孩子节点当中插入
			if ((offset + remainReadSize) % 8 == 0 && leafRemainSize - remainReadSize < 8)
			{
				//直接把叶子节点加入到节点当中
				pNotLeafNode->setParentID(parentId);
				pNotLeafNode->insertLeafSet(leafFilePos - preCmpLen);

				pNotLeafNode->setIsModified(true);

				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNotLeafNode->getIndexId());

				return true;
			}

			//叶子节点比较长
			switch (pNotLeafNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				unsigned long long key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen;
				if (!dstFile.read(pos, &key, 8))
				{
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 8);

				IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			case NODE_TYPE_TWO:
			{
				unsigned int key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen;
				if (!dstFile.read(pos, &key, 4))
				{
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 4);

				IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			case NODE_TYPE_THREE:
			{
				unsigned short key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen;
				if (!dstFile.read(pos, &key, 2))
				{
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 2);

				IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			case NODE_TYPE_FOUR:
			{
				unsigned char key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen;
				if (!dstFile.read(pos, &key, 1))
				{
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + 1);

				IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					return false;
				}
			}
				break;
			default:
				break;
			}

			//叶子节点加入到了非叶子节点可能比较大缩小下大小
			if (!cutNodeSize(pNotLeafNode->getIndexId(), pNotLeafNode))
			{
				return false;
			}

			pNotLeafNode->setIsModified(true);

			leftChildNode.setChildType(CHILD_TYPE_NODE);
			leftChildNode.setIndexId(pNotLeafNode->getIndexId());
			return true;
		}

		//前面不够8个字节的比较完了下面处理长长的那一串
		//文件以每次读取4k个字节这样读。这样避免一次读太多内存也不够。
		unsigned char* leafBuffer = (unsigned char*)malloc(4 * 1024);
		if (leafBuffer == nullptr)
		{
			return false;
		}

		unsigned char* nodeBuffer = (unsigned char*)malloc(4 * 1024);
		if (nodeBuffer == nullptr)
		{
			free(leafBuffer);
			return false;
		}

		while (cmpLen + 4 * 1024 <= remainReadSize)
		{
			//从两个文件当中把相应的部分读取出来
			fpos_t leafPos;
			leafPos.__pos = leafFilePos + cmpLen;
			if (!dstFile.read(leafPos, leafBuffer, 4 * 1024))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}
			fpos_t nodePos;
			nodePos.__pos = nodeFilePos + cmpLen;
			if (!dstFile.read(nodePos, nodeBuffer, 4 * 1024))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			unsigned long long subCmpLen = 0;
			for (; subCmpLen < 4 * 1024; subCmpLen += 8)
			{
				if (*(unsigned long long*)(&leafBuffer[subCmpLen]) != *(unsigned long long*)(&nodeBuffer[subCmpLen]))
				{
					break;
				}
			}

			if (subCmpLen != 4 * 1024)
			{
				//中间有不一样的地方
				IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

				if (pNode == nullptr)
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNode->setStart(leafFilePos);
				pNode->setLen(cmpLen + subCmpLen);
				pNode->setParentID(parentId);

				//把非叶子节点前面的部分加入到公共节点
				if (!pNode->appendLeafSet(pNotLeafNode, cmpLen + subCmpLen + 8, dstFileSize))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				//修改非叶子节点的长度
				pNotLeafNode->setStart(pNotLeafNode->getStart() + cmpLen + subCmpLen + 8);
				pNotLeafNode->setLen(pNotLeafNode->getLen() - cmpLen - subCmpLen - 8);

				if (!indexFile.changePreCmpLen(pNotLeafNode->getIndexId(), pNotLeafNode->getPreCmpLen(), pNotLeafNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNotLeafNode->setParentID(pNode->getIndexId());
				pNotLeafNode->setIsModified(true);

				IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, pNotLeafNode->getIndexId());

				IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;

				if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&nodeBuffer[subCmpLen]), indexNodeChild))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				//插入叶子节点
				IndexNodeChild lIndexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + subCmpLen + 8);

				if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&leafBuffer[subCmpLen]), lIndexNodeChild))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNode->setIsModified(true);

				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNode->getIndexId());

				free(leafBuffer);
				free(nodeBuffer);
				return true;
			}

			cmpLen += 4 * 1024;
		}

		//后面比较的长度不够4k
		unsigned long long lastNeedReadSize = remainReadSize - cmpLen;
		if (lastNeedReadSize != 0)
		{
			fpos_t leafPos;
			leafPos.__pos = leafFilePos + cmpLen;
			if (!dstFile.read(leafPos, leafBuffer, lastNeedReadSize))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			fpos_t nodePos;
			nodePos.__pos = nodeFilePos + cmpLen;
			if (!dstFile.read(nodePos, nodeBuffer, lastNeedReadSize))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}
		}

		unsigned long long subCmpLen = 0;
		for (; subCmpLen + 8 <= lastNeedReadSize; subCmpLen += 8)
		{
			if (*(unsigned long long*)(&leafBuffer[subCmpLen]) != *(unsigned long long*)(&nodeBuffer[subCmpLen]))
			{
				break;
			}
		}

		if (subCmpLen + 8 <= lastNeedReadSize)
		{
			//这一段有不一样的地方
			IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

			if (pNode == nullptr)
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			pNode->setStart(leafFilePos);
			pNode->setLen(cmpLen + subCmpLen);
			pNode->setParentID(parentId);

			//把非叶子节点当中的相同部分前面的叶子节点接入到公共节点
			if (!pNode->appendLeafSet(pNotLeafNode, cmpLen + subCmpLen + 8, dstFileSize))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			//修改非叶子节点的长度
			pNotLeafNode->setStart(pNotLeafNode->getStart() + cmpLen + subCmpLen + 8);
			pNotLeafNode->setLen(pNotLeafNode->getLen() - cmpLen - subCmpLen - 8);

			if (!indexFile.changePreCmpLen(pNotLeafNode->getIndexId(), pNotLeafNode->getPreCmpLen(), pNotLeafNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			pNotLeafNode->setParentID(pNode->getIndexId());
			pNotLeafNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, pNotLeafNode->getIndexId());

			IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;

			if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&nodeBuffer[subCmpLen]), indexNodeChild))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			//插入叶子节点

			IndexNodeChild lIndexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + subCmpLen + 8);

			if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&leafBuffer[subCmpLen]), lIndexNodeChild))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			pNode->setIsModified(true);

			leftChildNode.setChildType(CHILD_TYPE_NODE);
			leftChildNode.setIndexId(pNode->getIndexId());
			
			free(leafBuffer);
			free(nodeBuffer);
			return true;
		}

		//接下来还有剩下不够8个字节的部分
		unsigned long long suppleSize = 0;
		for (; subCmpLen + suppleSize < lastNeedReadSize; ++suppleSize)
		{
			if (*(unsigned char*)(&leafBuffer[subCmpLen + suppleSize]) != *(unsigned char*)(&nodeBuffer[subCmpLen + suppleSize]))
			{
				break;
			}
		}

		if ((subCmpLen + suppleSize) == lastNeedReadSize)
		{
			//两个节点前面的部分全部都一样
			if (leafRemainSize - remainReadSize < 8)
			{
				pNotLeafNode->setParentID(parentId);
				pNotLeafNode->insertLeafSet(leafFilePos - preCmpLen);

				pNotLeafNode->setIsModified(true);

				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNotLeafNode->getIndexId());

				free(leafBuffer);
				free(nodeBuffer);
				return true;
			}

			//叶子节点比较长
			switch (pNotLeafNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				unsigned long long key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen + subCmpLen + suppleSize;
				if (!dstFile.read(pos, &key, 8))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + subCmpLen + suppleSize + 8);

				IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}
			}
				break;
			case NODE_TYPE_TWO:
			{
				unsigned int key;
				//从文件当中读取key
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen + subCmpLen + suppleSize;
				if (!dstFile.read(pos, &key, 4))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + subCmpLen + suppleSize + 4);

				IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}
			}
			break;
			case NODE_TYPE_THREE:
			{
				unsigned short key;
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen + subCmpLen + suppleSize;
				if (!dstFile.read(pos, &key, 2))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + subCmpLen + suppleSize + 2);

				IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}
			}
			break;
			case NODE_TYPE_FOUR:
			{
				unsigned char key;
				fpos_t pos;
				pos.__pos = leafFilePos + cmpLen + subCmpLen + suppleSize;
				if (!dstFile.read(pos, &key, 1))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}

				pNotLeafNode->setParentID(parentId);
				IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, leafFilePos + cmpLen + subCmpLen + suppleSize + 1);

				IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)pNotLeafNode;

				if (!tmpNode->insertChildNode(this, key, indexNodeChild))
				{
					free(leafBuffer);
					free(nodeBuffer);
					return false;
				}
			}
			break;
			default:
				break;
			}

			//叶子节点加入到了非叶子节点可能比较大缩小下大小
			if (!cutNodeSize(pNotLeafNode->getIndexId(), pNotLeafNode))
			{
				free(leafBuffer);
				free(nodeBuffer);
				return false;
			}

			pNotLeafNode->setIsModified(true);
			leftChildNode.setChildType(CHILD_TYPE_NODE);
			leftChildNode.setIndexId(pNotLeafNode->getIndexId());
			free(leafBuffer);
			free(nodeBuffer);
			return true;
		}

		//最后8个字节里面有不一样的地方
		IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, preCmpLen);

		if (pNode == nullptr)
		{
			free(leafBuffer);
			free(nodeBuffer);
			return false;
		}

		//读取完整的key值
		fpos_t pos;
		pos.__pos = nodeFilePos + remainReadSize;
		if (!dstFile.read(pos, &nodeBuffer[lastNeedReadSize], subCmpLen + 8 - lastNeedReadSize))
		{
			free(leafBuffer);
			free(nodeBuffer);
			return false;
		}

		pNode->setStart(leafFilePos);
		pNode->setLen(cmpLen + subCmpLen);
		pNode->setParentID(parentId);

		//把非叶子节点当中相同部分前面的节点加入到公共节点
		if (!pNode->appendLeafSet(pNotLeafNode, cmpLen + subCmpLen + 8, dstFileSize))
		{
			free(leafBuffer);
			free(nodeBuffer);
			return false;
		}

		IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;
		
		//缩小非叶子节点的大小
		pNotLeafNode->setStart(pNotLeafNode->getStart() + cmpLen + subCmpLen + 8);
		pNotLeafNode->setLen(pNotLeafNode->getLen() - cmpLen - subCmpLen - 8);

		if (!indexFile.changePreCmpLen(pNotLeafNode->getIndexId(), pNotLeafNode->getPreCmpLen(), pNotLeafNode->getPreCmpLen() + cmpLen + subCmpLen + 8))
		{
			free(leafBuffer);
			free(nodeBuffer);
			return false;
		}

		pNotLeafNode->setParentID(pNode->getIndexId());
		pNotLeafNode->setIsModified(true);

		IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, pNotLeafNode->getIndexId());

		if (!tmpNode->insertChildNode(this, *(unsigned long long*)(&nodeBuffer[subCmpLen]), indexNodeChild))
		{
			free(leafBuffer);
			free(nodeBuffer);
			return false;
		}

		//插入叶子节点
		pNode->insertLeafSet(leafFilePos - preCmpLen);

		pNode->setIsModified(true);

		leftChildNode.setChildType(CHILD_TYPE_NODE);
		leftChildNode.setIndexId(pNode->getIndexId());

		free(leafBuffer);
		free(nodeBuffer);
		return true;

	}

	return true;
}

bool BuildIndex::addVMergeNode(unsigned long long preCmpLen, unsigned long long parentId, IndexNodeChild& leftChildNode, const IndexNodeChild& rightChildNode)
{
	unsigned char leftType = leftChildNode.getType();
	unsigned char rightType = rightChildNode.getType();
	if (leftType == CHILD_TYPE_NODE && rightType == CHILD_TYPE_NODE)
	{
		//合并的两个孩子节点都是非叶子节点
		IndexNode* leftNode = kvIndexFile.getIndexNode(leftChildNode.getIndexId(), BUILD_TYPE_KV);
		if (leftNode == nullptr)
		{
			return false;
		}

		IndexNode* rightNode = kvIndexFile.getIndexNode(rightChildNode.getIndexId(), BUILD_TYPE_KV);
		if (rightNode == nullptr)
		{
			return false;
		}

		unsigned long long leftFilePos = leftNode->getStart();

		unsigned long long leftRemainSize = leftNode->getLen();
		unsigned long long rightRemainSize = rightNode->getLen();
		unsigned long long remainReadSize = leftRemainSize;
		if (rightRemainSize < remainReadSize)
		{
			remainReadSize = rightRemainSize;
		}

		//如果开始比较位置不是8的整数倍的先比较前面那一部分
		unsigned long long offset = leftFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = remainReadSize;

			unsigned char leftData[8];
			unsigned char rightData[8];

			*(unsigned long long*)leftData = leftNode->getPartOfKey();
			*(unsigned long long*)rightData = rightNode->getPartOfKey();

			int curCmpLen = 0;
			while (cmpLen != needChartoEight)
			{
				if ((needChartoEight - cmpLen) % 4 == 0)
				{
					if (*(int*)(&leftData[cmpLen]) == *(int*)(&rightData[cmpLen]))
					{
						cmpLen += 4;
					}
					else
					{
						curCmpLen = 4;
						break;
					}
				}
				else if ((needChartoEight - cmpLen) % 2 == 0)
				{
					if (*(short*)(&leftData[cmpLen]) == *(short*)(&rightData[cmpLen]))
					{
						cmpLen += 2;
					}
					else
					{
						curCmpLen = 2;
						break;
					}
				}
				else
				{
					if (*(char*)(&leftData[cmpLen]) == *(char*)(&rightData[cmpLen]))
					{
						cmpLen++;
					}
					else
					{
						curCmpLen = 1;
						break;
					}
				}
			}

			if (cmpLen != needChartoEight)
			{
				//前面不够8个字节里面有不一样的地方
				//根据不同的字节创建不同的对象
				IndexNode* pNode = nullptr;
				switch (curCmpLen)
				{
				case 4:
					pNode = kvIndexFile.newIndexNode(NODE_TYPE_TWO, preCmpLen);
					break;
				case 2:
					pNode = kvIndexFile.newIndexNode(NODE_TYPE_THREE, preCmpLen);
					break;
				case 1:
					pNode = kvIndexFile.newIndexNode(NODE_TYPE_FOUR, preCmpLen);
					break;
				default:
					break;
				}

				if (pNode == nullptr)
				{
					return false;
				}

				//设置这个新设置的节点的各个参数
				pNode->setStart(leftFilePos);
				pNode->setLen(cmpLen);
				pNode->setParentID(parentId);
				pNode->setPartOfKey(leftNode->getPartOfKey());
				switch (pNode->getType())
				{
				case NODE_TYPE_TWO:
				{

					IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNode;

					//左边节点修改相应的节点长度
					leftNode->swiftPartOfKey(cmpLen + 4);
					leftNode->setStart(leftNode->getStart() + cmpLen + 4);
					leftNode->setLen(leftNode->getLen() - cmpLen - 4);
					//要修改preCmpLen也要修改优先级
					if (!kvIndexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + 4))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&leftData[cmpLen]), indexNodeChild, BUILD_TYPE_KV))
					{
						return false;
					}

					//右边节点修改相应节点的长度
					rightNode->swiftPartOfKey(cmpLen + 4);
					rightNode->setStart(rightNode->getStart() + cmpLen + 4);
					rightNode->setLen(rightNode->getLen() - cmpLen - 4);
					//要修改preCmpLen也要修改优先级
					if (!kvIndexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + 4))
					{
						return false;
					}
					rightNode->setParentID(pNode->getIndexId());
					rightNode->setIsModified(true);

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());
					if (!tmpNode->insertChildNode(this, *(unsigned int*)(&rightData[cmpLen]), rIndexNodeChild, BUILD_TYPE_KV))
					{
						return false;
					}
				}
				break;
				case NODE_TYPE_THREE:
				{

					IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)pNode;

					//左边节点修改相应节点长度
					leftNode->swiftPartOfKey(cmpLen + 2);
					leftNode->setStart(leftNode->getStart() + cmpLen + 2);
					leftNode->setLen(leftNode->getLen() - cmpLen - 2);
					//要修改preCmpLen也要修改优先级
					if (!kvIndexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + 2))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&leftData[cmpLen]), indexNodeChild, BUILD_TYPE_KV))
					{
						return false;
					}

					//右边节点修改相应节点的长度
					rightNode->swiftPartOfKey(cmpLen + 2);
					rightNode->setStart(rightNode->getStart() + cmpLen + 2);
					rightNode->setLen(rightNode->getLen() - cmpLen - 2);
					//要修改preCmpLen也要修改优先级
					if (!kvIndexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + 2))
					{
						return false;
					}

					rightNode->setParentID(pNode->getIndexId());
					rightNode->setIsModified(true);

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());
					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&rightData[cmpLen]), rIndexNodeChild, BUILD_TYPE_KV))
					{
						return false;
					}
				}
				break;
				case NODE_TYPE_FOUR:
				{

					IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)pNode;

					//左边节点修改相应节点长度
					leftNode->swiftPartOfKey(cmpLen + 1);
					leftNode->setStart(leftNode->getStart() + cmpLen + 1);
					leftNode->setLen(leftNode->getLen() - cmpLen - 1);
					//要修改preCmpLen也要修改优先级
					if (!kvIndexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen() + cmpLen + 1))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&leftData[cmpLen]), indexNodeChild, BUILD_TYPE_KV))
					{
						return false;
					}

					//右边节点修改相应节点的长度
					rightNode->swiftPartOfKey(cmpLen + 1);
					rightNode->setStart(rightNode->getStart() + cmpLen + 1);
					rightNode->setLen(rightNode->getLen() - cmpLen - 1);
					//要修改preCmpLen也要修改优先级
					if (!kvIndexFile.changePreCmpLen(rightNode->getIndexId(), rightNode->getPreCmpLen(), rightNode->getPreCmpLen() + cmpLen + 1))
					{
						return false;
					}

					rightNode->setParentID(pNode->getIndexId());
					rightNode->setIsModified(true);

					IndexNodeChild rIndexNodeChild(CHILD_TYPE_NODE, rightNode->getIndexId());
					if (!tmpNode->insertChildNode(this, *(unsigned char*)(&rightData[cmpLen]), rIndexNodeChild, BUILD_TYPE_KV))
					{
						return false;
					}
				}
				break;
				default:
					break;
				}

				//已经修改了节点设置节点为已经修改状态
				pNode->setIsModified(true);

				//两个节点合并成一个节点了以后设置左边的那个孩子节点
				leftChildNode.setIndexId(pNode->getIndexId());

				return true;
			}
		}

		//其中一个节点的所有部分和另外一个节点相同这个时候有两种情况,一种情况是长度相同,另一种情况是长度不同
		if (leftRemainSize == rightRemainSize)
		{
			//改变节点的类型让两个节点的类型相同
			while (leftNode->getType() != rightNode->getType())
			{
				if (compareTwoType(leftNode->getType(), rightNode->getType()))
				{
					rightNode = changeNodeType(rightNode->getIndexId(), rightNode, BUILD_TYPE_KV);
					if (rightNode == nullptr)
					{
						return false;
					}
				}
				else
				{
					leftNode = changeNodeType(leftNode->getIndexId(), leftNode, BUILD_TYPE_KV);
					if (leftNode == nullptr)
					{
						return false;
					}
				}
			}

			//把两个长度相同类型相同的节点合并
			if (!leftNode->mergeSameLenNode(this, rightNode, BUILD_TYPE_KV))
			{
				return false;
			}

			//合并完成了以后左边的节点可能比较大要缩小一下节点
			if (!cutNodeSize(leftNode->getIndexId(), leftNode, BUILD_TYPE_KV))
			{
				return false;
			}

			leftNode->setIsModified(true);

			//右边的节点完全融入了左边的节点所以右边的节点可以说是完全不存在删除
			kvIndexFile.deleteIndexNode(rightNode->getIndexId());

			leftChildNode.setIndexId(leftNode->getIndexId());

			return true;
		}

		//有一个节点比较长一个节点比较短
		IndexNode* longNode = leftNode;
		IndexNode* anotherNode = rightNode;
		if (leftRemainSize < rightRemainSize)
		{
			longNode = rightNode;
			anotherNode = leftNode;
		}

		//从一个节点添加进另一个节点
		switch (anotherNode->getType())
		{
		case NODE_TYPE_THREE:
		{
			unsigned short key;
			unsigned long long partOfKey = longNode->getPartOfKey();
			unsigned char* p = (unsigned char*)&partOfKey;
			key = *(unsigned short*)(&p[cmpLen]);

			anotherNode->setParentID(parentId);

			//修改长节点的长度
			longNode->swiftPartOfKey(cmpLen + 2);
			longNode->setStart(longNode->getStart() + cmpLen + 2);
			longNode->setLen(longNode->getLen() - cmpLen - 2);

			if (!kvIndexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 2))
			{
				return false;
			}

			longNode->setParentID(anotherNode->getIndexId());
			longNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

			IndexNodeTypeThree* tmpNode = (IndexNodeTypeThree*)anotherNode;

			if (!tmpNode->insertChildNode(this, key, indexNodeChild, BUILD_TYPE_KV))
			{
				return false;
			}
		}
		break;
		case NODE_TYPE_FOUR:
		{
			unsigned char key;
			unsigned long long partOfKey = longNode->getPartOfKey();
			unsigned char* p = (unsigned char*)&partOfKey;
			key = *(unsigned char*)(&p[cmpLen]);

			anotherNode->setParentID(parentId);

			//修改长节点的长度
			longNode->swiftPartOfKey(cmpLen + 1);
			longNode->setStart(longNode->getStart() + cmpLen + 1);
			longNode->setLen(longNode->getLen() - cmpLen - 1);

			if (!kvIndexFile.changePreCmpLen(longNode->getIndexId(), longNode->getPreCmpLen(), longNode->getPreCmpLen() + cmpLen + 1))
			{
				return false;
			}

			longNode->setParentID(anotherNode->getIndexId());
			longNode->setIsModified(true);

			IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, longNode->getIndexId());

			IndexNodeTypeFour* tmpNode = (IndexNodeTypeFour*)anotherNode;

			if (!tmpNode->insertChildNode(this, key, indexNodeChild, BUILD_TYPE_KV))
			{
				return false;
			}
		}
		break;
		default:
			break;
		}

		//比较小的节点加入了新的节点可能比较大缩小下大小
		if (!cutNodeSize(anotherNode->getIndexId(), anotherNode, BUILD_TYPE_KV))
		{
			return false;
		}

		anotherNode->setIsModified(true);

		leftChildNode.setIndexId(anotherNode->getIndexId());
		
		return true;
	}
	return false;
}

IndexNode* BuildIndex::changeNodeType(unsigned long long indexId, IndexNode* indexNode, unsigned char buildType)
{
	if (indexNode == nullptr)
	{
		return nullptr;
	}

	//直接调用节点的函数改变节点的类型
	IndexNode* newNode = indexNode->changeType(this, buildType);
	if (newNode == nullptr)
	{
		return nullptr;
	}

	if (buildType == BUILD_TYPE_FILE)
	{
		//创建了已经减小了的节点和原来的节点交换
		if (!indexFile.swapNode(indexId, newNode))
		{
			delete newNode;
			return nullptr;
		}
	}
	else
	{
		if (!kvIndexFile.swapNode(indexId, newNode))
		{
			delete newNode;
			return nullptr;
		}
	}

	//成功交换了节点了以后交换出来的节点要被删掉
	delete indexNode;
	return newNode;
}

IndexNode* BuildIndex::newKvNode(unsigned char nodeType, unsigned long long preCmpLen)
{
	return kvIndexFile.newIndexNode(nodeType, preCmpLen);
}

bool BuildIndex::addKV(unsigned long long key, unsigned long long value)
{
	unsigned long long bigEndKey = swiftBigLittleEnd(key);
	unsigned long long rootIndexId = kvIndexFile.getRootIndexId();
	if (rootIndexId == 0)
	{
		//没有创建根节点先创建根节点
		IndexNode* newIndexNode = kvIndexFile.newIndexNode(NODE_TYPE_ONE, 0);
		newIndexNode->setStart(0);
		newIndexNode->setLen(0);
		newIndexNode->setParentID(0);
		IndexNodeChild indexNodeChild(CHILD_TYPE_VALUE, value);
		IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)newIndexNode;
		if (!pTmpNode->insertChildNode(this, bigEndKey, indexNodeChild, BUILD_TYPE_KV))
		{
			return false;
		}
		pTmpNode->setIsModified(true);

		kvIndexFile.setRootIndexId(newIndexNode->getIndexId());
		return true;
	}

	IndexNodeChild leftNodeChild(CHILD_TYPE_NODE, rootIndexId);
	IndexNode* newIndexNode = kvIndexFile.newIndexNode(NODE_TYPE_ONE, 0);
	newIndexNode->setStart(0);
	newIndexNode->setLen(0);
	newIndexNode->setParentID(0);
	IndexNodeChild indexNodeChild(CHILD_TYPE_VALUE, value);
	IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)newIndexNode;
	if (!pTmpNode->insertChildNode(this, bigEndKey, indexNodeChild, BUILD_TYPE_KV))
	{
		return false;
	}
	pTmpNode->setIsModified(true);

	IndexNodeChild rightNodeChild(CHILD_TYPE_NODE, newIndexNode->getIndexId());
	if (!addVMergeNode(0, 0, leftNodeChild, rightNodeChild))
	{
		return false;
	}

	kvIndexFile.reduceCache();

	return true;
}

bool BuildIndex::build()
{
	//判断需要多少个4k个字节的块才开始放第一个节点
	//每2m个字节做为一块看看至少可以弄多少个根节点id
	unsigned long rootIndexCount = (dstFileSize + 2 * 1024 * 1024 - 1) / (2 * 1024 * 1024);
	//算出这么多个根节点id还有前面的存储数量的东西要多少个4k块存储
	unsigned long needBlock = ((rootIndexCount + 1) * 8 + 4 * 1024 - 1) / (4 * 1024);
	indexFile.setInitMaxUniqueNum(needBlock);
	bool needNewleftNode = true;
	IndexNodeChild leftNode(CHILD_TYPE_LEAF, 0);
	struct timeval start;
	struct timeval end;
	unsigned long diff;
	gettimeofday(&start, nullptr);
	int times = 0;
	//接下来把各个8字节开始的位置做一个节点然后不停的合并到一起
	for (unsigned long long filePos = 0; filePos < dstFileSize; filePos += 8)
	{
		if (needNewleftNode)
		{
			leftNode.setChildType(CHILD_TYPE_LEAF);
			leftNode.setIndexId(filePos);
			needNewleftNode = false;
			continue;
		}
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, filePos);

		//printf("mergeNode filePos %llu\n", filePos);

		if (!mergeNode(0, 0, leftNode, indexNodeChild))
		{
			printf("mergeNode fail filePos %llu\n", filePos);
			return false;
		}

		if (filePos % (2 * 1024 * 1024) == 0)
		{
			gettimeofday(&end, nullptr);
			diff = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
			FlwPrintf("out", "time %d cal use time %ld\n", times, diff);
			needNewleftNode = true;
			indexFile.pushRootIndexId(leftNode.getIndexId());
			gettimeofday(&start, nullptr);
			//后面合成新的节点里面的数据和这里无关这里就先把里面的数据先清空
			if (!indexFile.writeCacheWithoutRootIndex())
			{
				return false;
			}
			gettimeofday(&end, nullptr);
			diff = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
			FlwPrintf("out", "time %d write disk use time %ld\n", times, diff);
			gettimeofday(&start, nullptr);
			++times;
		}
		//printf("%lu\n", indexFile.size());
	}

	if (!needNewleftNode)
	{
		//这个时候分两种情况一种情况是leftNode是叶子节点一种情况是leftNode是非叶子节点
		if (leftNode.getType() == CHILD_TYPE_LEAF)
		{
			//叶子节点就做成一个节点放进根节点当中
			IndexNode* pNode = indexFile.newIndexNode(NODE_TYPE_ONE, 0);

			if (pNode == nullptr)
			{
				return false;
			}

			pNode->setStart(leftNode.getIndexId());
			pNode->setLen(dstFileSize - leftNode.getIndexId() - dstFileSize % 8);
			pNode->setParentID(0);

			pNode->insertLeafSet(leftNode.getIndexId());				//根节点是特殊的有一个叶子节点从开头直到结尾
			indexFile.pushRootIndexId(pNode->getIndexId());
			if (!indexFile.writeCacheWithoutRootIndex())
			{
				return false;
			}
		}
		else
		{
			//不是叶子结点的话直接直接放进文件当中
			indexFile.pushRootIndexId(leftNode.getIndexId());
			if (!indexFile.writeCacheWithoutRootIndex())
			{
				return false;
			}
		}
	}

	//把所有的根节点写入到索引文件当中
	if (!indexFile.writeEveryRootIndexId())
	{
		return false;
	}
	printf("build finish\n");
	return true;
}

bool BuildIndex::writeKvEveryCache()
{
	return kvIndexFile.writeEveryCache();
}
