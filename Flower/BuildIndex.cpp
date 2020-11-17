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
	//��ȡ�����ļ�������
	if (!GetIndexPath(fileName, indexFileName))
	{
		return false;
	}

	//��Ŀ���ļ����г�ʼ��
	if (!dstFile.init(fileName))
	{
		return false;
	}
	
	//�������ļ���ʼ����Ҫ����һ�������ļ�
	Myfile tmpIndexFile;
	if (!tmpIndexFile.init(indexFileName))
	{
		return false;
	}

	indexFile.init(tmpIndexFile, index);

	//��ȡ�ļ��Ĵ�С
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
	//�������жϽڵ�Ĵ�С�Ƿ��Ԥ�ƵĻ�Ҫ��
	if (indexNode->getChildrenNum() <= 256)
	{
		return true;
	}

	//�ı�ڵ������ýڵ�ĺ��ӽ��ļ�С���������ӽڵ���ٵ�
	IndexNode* newNode = changeNodeType(indexId, indexNode);

	if (newNode == nullptr)
	{
		return false;
	}

	//�ı��˽ڵ����͵��ǻ����޷��ų���ǰ�ڵ���ܱ�256Ҫ��Ͳ������µĺ��ӽڵ��256Ҫ�����Ե��ýڵ�ĺ����ı�ڵ�
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
	//һ�������ֶ���Ҷ�ӽڵ㣬һ��������������ͨ�Ľڵ㣬һ����һ������ͨ�ڵ�һ����Ҷ�ӽڵ㡣
	unsigned char leftType = leftChildNode.getType();
	unsigned char rightType = rightChildNode.getType();
	if (leftType == CHILD_TYPE_LEAF && rightType == CHILD_TYPE_LEAF)
	{
		unsigned long long leftFilePos = leftChildNode.getIndexId();
		unsigned long long rightFilePos = rightChildNode.getIndexId();
		//�����ʼ�Ƚ�λ�ò���8�����������ȱȽ�ǰ����һ����
		unsigned long long offset = leftFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = 8 - offset;
			//���ļ���������λ�õ��н��ж�ȡʣ���ֽڵ�����
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
				//ǰ�治��8���ֽ������в�һ���ĵط�
				//���ݲ�ͬ���ֽڴ�����ͬ�Ķ���
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

				//������������õĽڵ�ĸ�������
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

				//�Ѿ��޸��˽ڵ����ýڵ�Ϊ�Ѿ��޸�״̬
				pNode->setIsModified(true);

				//����Ҷ�ڵ�ϲ���һ�������ڵ����Ժ�������ߵ��Ǹ����ӽڵ�
				leftChildNode.setChildType(CHILD_TYPE_NODE);
				leftChildNode.setIndexId(pNode->getIndexId());

				return true;
			}
		}

		//ǰ�治��8���ֽڵĲ����Ѿ��Ƚ���������Ƚϳ������ǲ��֡�
		//�ļ���ÿ�ζ�ȡ4k���ֽ�����������������һ�ζ�̫���ڴ�Ҳ������
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

		//��������Ҫ�Ƚϵ�����ֽ���
		unsigned long long leftRemainSize = dstFileSize - leftFilePos;
		unsigned long long rightRemainSize = dstFileSize - rightFilePos;
		unsigned long long remainReadSize = leftRemainSize;
		if (rightRemainSize < remainReadSize)
		{
			remainReadSize = rightRemainSize;
		}

		while (cmpLen + 4 * 1024 <= remainReadSize)
		{
			//�������ļ����а���Ӧ�Ĳ��ֶ�ȡ����
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

		//����ıȽϳ��Ȳ���4k
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

		//����������ʣ�²���8���ֽڵĲ���
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
			//����Ҷ�ӽڵ�ǰ��Ĳ���ȫ����һ��
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

		//��󼸸��ֽڲ�һ��
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

		//��ȡ������keyֵ
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
		//�ϲ����������ӽڵ㶼�Ƿ�Ҷ�ӽڵ�
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

		//�����ʼ�Ƚ�λ�ò���8�����������ȱȽ�ǰ����һ����
		unsigned long long offset = leftFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = 8 - offset;

			//�п���ʣ�µĶ�ȡ��С��8���ֽڻ�ҪС
			if (remainReadSize < needChartoEight)
			{
				needChartoEight = remainReadSize;
			}

			//���ļ���������λ�õ��ж�ȡʣ�µ��ֽڵ�����
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
				//ǰ�治��8���ֽ������в�һ���ĵط�
				//���ݲ�ͬ���ֽڴ�����ͬ�Ķ���
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

				//������������õĽڵ�ĸ�������
				pNode->setStart(leftFilePos);
				pNode->setLen(cmpLen);
				pNode->setParentID(parentId);
				switch (pNode->getType())
				{
				case NODE_TYPE_TWO:
				{

					IndexNodeTypeTwo* tmpNode = (IndexNodeTypeTwo*)pNode;

					//��߽ڵ��޸���Ӧ�Ľڵ㳤��
					leftNode->setStart(leftNode->getStart() + cmpLen + 4);
					leftNode->setLen(leftNode->getLen() - cmpLen - 4);
					//Ҫ�޸�preCmpLenҲҪ�޸����ȼ�
					if (!indexFile.changePreCmpLen(leftNode->getIndexId(), leftNode->getPreCmpLen(), leftNode->getPreCmpLen + cmpLen + 4))
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

					//�ұ߽ڵ��޸���Ӧ�ڵ�ĳ���
					rightNode->setStart(rightNode->getStart() + cmpLen + 4);
					rightNode->setLen(rightNode->getLen() - cmpLen - 4);
					//Ҫ�޸�preCmpLenҲҪ�޸����ȼ�
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

					//��߽ڵ��޸���Ӧ�ڵ㳤��
					leftNode->setStart(leftNode->getStart() + cmpLen + 2);
					leftNode->setLen(leftNode->getLen() - cmpLen - 2);
					//Ҫ�޸�preCmpLenҲҪ�޸����ȼ�
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

					//�ұ߽ڵ��޸���Ӧ�ڵ�ĳ���
					rightNode->setStart(rightNode->getStart() + cmpLen + 2);
					rightNode->setLen(rightNode->getLen() - cmpLen - 2);
					//Ҫ�޸�preCmpLenҲҪ�޸����ȼ�
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

					//��߽ڵ��޸���Ӧ�ڵ㳤��
					leftNode->setStart(leftNode->getStart() + cmpLen + 1);
					leftNode->setLen(leftNode->getLen() - cmpLen - 1);
					//Ҫ�޸�preCmpLenҲҪ�޸����ȼ�
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

					//�ұ߽ڵ��޸���Ӧ�ڵ�ĳ���
					rightNode->setStart(rightNode->getStart() + cmpLen + 1);
					rightNode->setLen(rightNode->getLen() - cmpLen - 1);
					//Ҫ�޸�preCmpLenҲҪ�޸����ȼ�
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

				//�Ѿ��޸��˽ڵ����ýڵ�Ϊ�Ѿ��޸�״̬
				pNode->setIsModified(true);

				//�����ڵ�ϲ���һ���ڵ����Ժ�������ߵ��Ǹ����ӽڵ�
				leftChildNode.setIndexId(pNode->getIndexId());

				return true;
			}
		}

		if (cmpLen == remainReadSize)
		{
			//����һ���ڵ�����в��ֺ�����һ���ڵ���ͬ���ʱ�����������,һ������ǳ�����ͬ,��һ������ǳ��Ȳ�ͬ
			if (leftRemainSize == rightRemainSize)
			{
				//�ı�ڵ�������������ڵ��������ͬ
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

				//������������ͬ������ͬ�Ľڵ�ϲ�
				if (!leftNode->mergeSameLenNode(this, rightNode))
				{
					return false;
				}

				//�ϲ�������Ժ���ߵĽڵ���ܱȽϴ�Ҫ��Сһ�½ڵ�
				if (!cutNodeSize(leftNode->getIndexId(), leftNode))
				{
					return false;
				}

				leftNode->setIsModified(true);

				//�ұߵĽڵ���ȫ��������ߵĽڵ������ұߵĽڵ����˵����ȫ������ɾ��
				indexFile.deleteIndexNode(rightNode->getIndexId());

				leftChildNode.setIndexId(leftNode->getIndexId());

				return true;
			}

			//��һ���ڵ�Ƚϳ�һ���ڵ�Ƚ϶�
			IndexNode* longNode = leftNode;
			IndexNode* anotherNode = rightNode;
			unsigned long long longNodeStart = leftFilePos;
			if (leftRemainSize < rightRemainSize)
			{
				longNode = rightNode;
				anotherNode = leftNode;
				longNodeStart = rightFilePos;
			}

			//��һ���ڵ���ӽ���һ���ڵ�
			switch (anotherNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				unsigned long long key;
				//���ļ����ж�ȡkey
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 8))
				{
					return false;
				}

				anotherNode->setParentID(parentId);
				
				//�޸ĳ��ڵ�ĳ���
				longNode->setStart(longNode->getStart() + cmpLen + 8);
				longNode->setLen(longNode->getLen - cmpLen - 8);

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
				//���ļ����ж�ȡkey
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 4))
				{
					return false;
				}

				anotherNode->setParentID(parentId);

				//�޸ĳ��ڵ�ĳ���
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
				//���ļ����ж�ȡkey
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 2))
				{
					return false;
				}

				anotherNode->setParentID(parentId);

				//�޸ĳ��ڵ�ĳ���
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
				//���ļ����ж�ȡkey
				fpos_t pos;
				pos.__pos = longNodeStart + cmpLen;
				if (!dstFile.read(pos, &key, 1))
				{
					return false;
				}

				anotherNode->setParentID(parentId);

				//�޸ĳ��ڵ�ĳ���
				longNode->setStart(longNode->getStart() + cmpLen + 1);
				longNode->setLen(longNode->getLen - cmpLen - 1);

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

			//�Ƚ�С�Ľڵ�������µĽڵ���ܱȽϴ���С�´�С
			if (!cutNodeSize(anotherNode->getIndexId(), anotherNode))
			{
				return false;
			}

			anotherNode->setIsModified(true);

			leftChildNode.setIndexId(anotherNode->getIndexId());

			return true;
		}

		//ǰ�治��8���ֽڵıȽ��������洦��������һ��
		//�ļ���ÿ�ζ�ȡ4k���ֽ�����������������һ�ζ�̫���ڴ�Ҳ������
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
			//�������ļ����а���Ӧ�Ĳ��ֶ�ȡ����
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
				//�м��в�һ���ĵط�
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

				//�������ڵ㵱�е���ͬ����ǰ���Ҷ�ӽڵ���뵽�����ڵ�
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

				//�޸���߽ڵ�ĳ���
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

				//�ұ߽ڵ��޸���Ӧ�ڵ�ĳ���
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

		//����Ƚϵĳ��Ȳ���4k
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
			//��һ���в�һ���ĵط�
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

			//�������ڵ㵱�е���ͬ����ǰ���Ҷ�ӽڵ���뵽�����ڵ�
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

			//�޸���߽ڵ�ĳ���
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

			//�ұ߽ڵ��޸���Ӧ�ڵ�ĳ���
			rightNode->setStart(rightNode->getStart() + cmpLen + subCmpLen + 8);
			rightNode->setLen(rightNode->getLen() - cmpLen - subCmpLen + 8);

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

		//ǰ�����еıȽ϶���һ����
		if (leftRemainSize == rightRemainSize)
		{
			//�ı�ڵ�������������ڵ��������ͬ
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

			//������������ͬ������ͬ�Ľڵ�ϲ�
			if (!leftNode->mergeSameLenNode(this, rightNode))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			//�ϲ�������Ժ���ߵĽڵ���ܱȽϴ�Ҫ��Сһ�½ڵ�
			if (!cutNodeSize(leftNode->getIndexId(), leftNode))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			leftNode->setIsModified(true);

			//�ұߵĽڵ���ȫ��������ߵĽڵ������ұߵĽڵ����˵��ȫ������ɾ��
			indexFile.deleteIndexNode(rightNode->getIndexId());

			leftChildNode.setIndexId(leftNode->getIndexId());

			free(leftBuffer);
			free(rightBuffer);
			return true;
		}

		//��һ���ڵ�Ƚϳ�һ���ڵ�Ƚ϶�
		IndexNode* longNode = leftNode;
		IndexNode* anotherNode = rightNode;
		unsigned long long longNodeStart = leftFilePos;
		if (leftRemainSize < rightRemainSize)
		{
			longNode = rightNode;
			anotherNode = leftNode;
			longNodeStart = rightFilePos;
		}

		//��һ���ڵ���ӽ���һ���ڵ�
		switch (anotherNode->getType())
		{
		case NODE_TYPE_ONE:
		{
			unsigned long long key;
			//���ļ����ж�ȡkey
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 8))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//�ѳ��ڵ�ǰ����ͬ���ֵ�Ҷ�ӽڵ�ӽ�ȥ
			anotherNode->appendLeafSet(longNode, cmpLen + 8, dstFileSize);

			//�޸ĳ��ڵ�ĳ���
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
			//���ļ����ж�ȡkey
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 4))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//�ѳ��ڵ�ǰ����ͬ���ֵ�Ҷ�ӽڵ�ӽ�ȥ
			anotherNode->appendLeafSet(longNode, cmpLen + 8, dstFileSize);

			//�޸ĳ��ڵ�ĳ���

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
			//���ļ����ж�ȡkey
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 2))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//�ѳ��ڵ�ǰ����ͬ���ֵ�Ҷ�ӽڵ�ӽ�ȥ
			anotherNode->appendLeafSet(longNode, cmpLen + 8, dstFileSize);

			//�޸ĳ��ڵ�ĳ���
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
			//���ļ����ж�ȡkey
			fpos_t pos;
			pos.__pos = longNodeStart + cmpLen;
			if (!dstFile.read(pos, &key, 1))
			{
				free(leftBuffer);
				free(rightBuffer);
				return false;
			}

			anotherNode->setParentID(parentId);

			//�ѳ��ڵ�ǰ����ͬ�Ĳ��ֵ�Ҷ�ӽڵ�ӽ�ȥ
			anotherNode->appendLeafSet(longNode, cmpLen + 8, dstFileSize);

			//�޸ĳ��ڵ�ĳ���
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

		//�Ƚ�С�Ľڵ�������µĽڵ���ܱȽϴ���С�´�С
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
		//����һ���ڵ��Ƿ�Ҷ�ӽڵ�,��һ���ڵ���Ҷ�ӽڵ�
		unsigned long long nodeIndexId = leftChildNode.getIndexId();
		unsigned long long leafFilePos = rightChildNode.getIndexId();
		if (rightChildNode.getType() == CHILD_TYPE_NODE)
		{
			nodeIndexId = rightChildNode.getIndexId();
			leafFilePos = leftChildNode.getIndexId();
		}

		IndexNode* pNode = indexFile.getIndexNode(nodeIndexId);
		if (pNode == nullptr)
		{
			return false;
		}

		unsigned long long nodeFilePos = pNode->getStart();
		unsigned long long leafRemainSize = dstFileSize - leafFilePos;
		unsigned long long nodeRemainSize = pNode->getLen();
		unsigned long long remainReadSize = leafRemainSize;
		if (nodeRemainSize < remainReadSize)
		{
			remainReadSize = nodeRemainSize;
		}

		//�����ʼ�Ƚ�λ�ò���8�����������ȱȽ�ǰ����һ����
		unsigned long long offset = leafFilePos % 8;
		unsigned long long cmpLen = 0;
		if (offset != 0)
		{
			unsigned long long needChartoEight = 8 - offset;

			//�п���ʣ�µĶ�ȡ��С��8���ֽڻ�С
			if (remainReadSize < needChartoEight)
			{
				needChartoEight = remainReadSize;
			}

			//���ļ���������λ�õ��ж�ȡʣ�µ��ֽڵ�����
			unsigned char leafData[8];
			unsigned char nodeData[8];
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
	}
}

IndexNode* BuildIndex::changeNodeType(unsigned long long indexId, IndexNode* indexNode)
{
	if (indexNode == nullptr)
	{
		return nullptr;
	}
	
	//ֱ�ӵ��ýڵ�ĺ����ı�ڵ������
	IndexNode* newNode = indexNode->changeType(this);
	if (newNode == nullptr)
	{
		return nullptr;
	}
	
	//�������Ѿ���С�˵Ľڵ��ԭ���Ľڵ㽻��
	if (!indexFile.swapNode(indexId, newNode))
	{
		delete newNode;
		return nullptr;
	}

	//�ɹ������˽ڵ����Ժ󽻻������Ľڵ�Ҫ��ɾ��
	delete indexNode;
	return newNode;
}