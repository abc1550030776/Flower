#include "BuildIndex.h"
#include <memory.h>
#include "common.h"
#include <sys/stat.h>

BuildIndex::BuildIndex()
{
	dstFileSize = 0;
}

bool BuildIndex::init(const char* fileName, Index* index)
{
	if (index == nullptr)
	{
		return false;
	}

	char indexFileName[4096];
	memset(indexFileName, 0, sizeof(indexFileName));
	//获取索引文件的名字
	if (!GetIndexPath(fileName, indexFileName))
	{
		return false;
	}

	//对目标文件进行初始化
	if (!dstFile.init(fileName))
	{
		return false;
	}
	
	//对索引文件初始化先要创建一个索引文件
	Myfile tmpIndexFile;
	if (!tmpIndexFile.init(indexFileName))
	{
		return false;
	}

	indexFile.init(tmpIndexFile, index);

	//获取文件的大小
	struct stat statbuf;
	stat(fileName, &statbuf);
	dstFileSize = statbuf.st_size;
	return true;
}

bool BuildIndex::cutNodeSize(unsigned long long indexId, IndexNode* indexNode)
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
	IndexNode* newNode = changeNodeType(indexId, indexNode);

	if (newNode == nullptr)
	{
		return false;
	}

	//改变了节点类型但是还是无法排除当前节点可能比256要大和产生的新的孩子节点比256要大所以调用节点的函数改变节点
	newNode->cutNodeSize(this, indexId);
	return true;
}

IndexNode* BuildIndex::getIndexNode(unsigned long long indexId)
{
	return indexFile.getIndexNode(indexId);
}

bool BuildIndex::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	return indexFile.changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
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
					if (!tmpNode->insertChildNode(this, *(unsigned short*)(&leftData[cmpLen]), rIndexNodeChild))
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
		unsigned long long chooseKey = *(unsigned long long*)(&leftBuffer[subCmpLen]);
		if (rightFilePos < chooseFilePos)
		{
			chooseFilePos = rightFilePos;
			anotherFilePos = leftFilePos;
			chooseKey = *(unsigned long long*)(&rightBuffer[subCmpLen]);
		}

		pNode->setStart(leftFilePos);
		pNode->setLen(cmpLen + subCmpLen);
		pNode->setParentID(parentId);

		IndexNodeTypeOne* tmpNode = (IndexNodeTypeOne*)pNode;
		IndexNodeChild indexNodeChild(CHILD_TYPE_LEAF, chooseFilePos + cmpLen + subCmpLen + 8);

		if (!tmpNode->insertChildNode(this, chooseKey, indexNodeChild))
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
					//读取左边节点的key
					unsigned int leftKey;
					fpos_t leftPos;
					leftPos.__pos = leftFilePos + cmpLen;
					if (!dstFile.read(leftPos, &leftKey, 4))
					{
						return false;
					}

					//读取右边节点的key
					unsigned int rightKey;
					fpos_t rightPos;
					rightPos.__pos = rightFilePos + cmpLen;
					if (!dstFile.read(rightPos, &rightKey, 4))
					{
						return false;
					}

					IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNode;

					//左边节点修改相应的节点长度
					leftNode->setStart(leftNode->getStart() + cmpLen + 4);
					leftNode->setLen(leftNode->getLen() - cmpLen - 4);
					//要修改preCmpLen也要修改优先级
					if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen + cmpLen + 4))
					{
						return false;
					}
					leftNode->setParentID(pNode->getIndexId());
					leftNode->setIsModified(true);

					IndexNodeChild indexNodeChild(CHILD_TYPE_NODE, leftNode->getIndexId());

					if (!tmpNode->insertChildNode(this, leftKey, indexNodeChild))
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
					if (!tmpNode->insertChildNode(this, rightKey, rIndexNodeChild))
					{
						return false;
					}
				}
					break;
				case NODE_TYPE_THREE:
				{
					//读取左边节点的key
					unsigned short leftKey;
					fpos_t leftPos;
					leftPos.__pos = leftFilePos + cmpLen;
					if (!dstFile.read(leftPos, &leftKey, 2))
					{
						return false;
					}

					//读取右边节点的key
					unsigned short rightKey;
					fpos_t rightPos;
					rightPos.__pos = rightFilePos + cmpLen;
					if (!dstFile.read(rightPos, &rightKey, 2))
					{
						return false;
					}

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

					if (!tmpNode->insertChildNode(this, leftKey, indexNodeChild))
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
					if (!tmpNode->insertChildNode(this, rightKey, rIndexNodeChild))
					{
						return false;
					}
				}
					break;
				case NODE_TYPE_FOUR:
				{
					//读取左边节点的key
					unsigned char leftKey;
					fpos_t leftPos;
					leftPos.__pos = leftFilePos + cmpLen;
					if (!dstFile.read(leftPos, &leftKey, 1))
					{
						return false;
					}

					//读取右边节点的key
					unsigned char rightKey;
					fpos_t rightPos;
					rightPos.__pos = rightFilePos + cmpLen;
					if (!dstFile.read(rightPos, &rightKey, 1))
					{
						return false;
					}

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

					if (!tmpNode->insertChildNode(this, leftKey, indexNodeChild))
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
					if (!tmpNode->insertChildNode(this, rightKey, rIndexNodeChild))
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
	}
}

IndexNode* BuildIndex::changeNodeType(unsigned long long indexId, IndexNode* indexNode)
{
	if (indexNode == nullptr)
	{
		return nullptr;
	}
	
	//直接调用节点的函数改变节点的类型
	IndexNode* newNode = indexNode->changeType(this);
	if (newNode == nullptr)
	{
		return nullptr;
	}
	
	//创建了已经减小了的节点和原来的节点交换
	if (!indexFile.swapNode(indexId, newNode))
	{
		delete newNode;
		return nullptr;
	}

	//成功交换了节点了以后交换出来的节点要被删掉
	delete indexNode;
	return newNode;
}