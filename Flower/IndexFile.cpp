#include "IndexFile.h"
#include "UniqueGenerator.h"

IndexFile::IndexFile()
{
	pIndex = nullptr;
}

void IndexFile::init(const Myfile& file, Index* index)
{
	indexFile = file;
	pIndex = index;
}

IndexNode* IndexFile::getIndexNode(unsigned long long indexId)
{
	//�ж��Ƿ��Ѿ���ʼ��
	if (pIndex == nullptr)
	{
		return nullptr;
	}

	//�ȴӻ��浱�в���Ȼ�󷵻�
	IndexNode* pIndexNode = pIndex->getIndexNode(indexId);
	if (pIndexNode != nullptr)
	{
		return pIndexNode;
	}

	//���ļ����а����ݶ�ȡ����
	char* buffer = (char*)malloc(8 * 1024);

	//���ļ����еĴ洢λ����������id * 4 * 1024������,��Щ�洢�Ĵ洢�ıȽϴ�����4k
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.read(pos, buffer, 8 * 1024);

	//���ݲ�ͬ�Ľڵ����ʹ����ڵ�
	char* p = buffer;
	switch (*((unsigned char*)p))
	{
	case NODE_TYPE_ONE:
		pIndexNode = new IndexNodeTypeOne();
		break;
	case NODE_TYPE_TWO:
		pIndexNode = new IndexNodeTypeTwo();
		break;
	case NODE_TYPE_THREE:
		pIndexNode = new IndexNodeTypeThree();
		break;
	case NODE_TYPE_FOUR:
		pIndexNode = new IndexNodeTypeFour();
		break;
	default:
		free(buffer);
		return nullptr;
		break;
	}
	p++;
	unsigned short len = *((unsigned short*)p);
	p += 2;
	if ((len + 3) > 4 * 1024)
	{
		pIndexNode->setIsBig(true);
	}

	//�Ѷ�����ת�ɽڵ�����������
	if (!pIndexNode->toObject(p, len))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}

	free(buffer);

	//����������Ժ���뵽�����ڵ�����
	if (!pIndex->insert(indexId, pIndexNode))
	{
		delete pIndexNode;
		return nullptr;
	}

	pIndexNode->setIndexId(indexId);
	//���뵽�����������Ժ��ٰ���������
	return pIndexNode;
}

//��ȡ��ʱ�ڵ�
IndexNode* IndexFile::getTempIndexNode(unsigned long long indexId)
{
	//�ж��Ƿ��Ѿ���ʼ��
	if (pIndex == nullptr)
	{
		return nullptr;
	}

	//�ȴӻ��浱�в���Ȼ�󷵻�
	IndexNode* pIndexNode = pIndex->getIndexNode(indexId);
	if (pIndexNode != nullptr)
	{
		//��ǰ���浱�д��ڵĽڵ����Դ�һ�����
		tempIndexNodeId.insert(indexId);
		return pIndexNode;
	}

	//���ļ����ж�ȡ���ǲ����뻺��

	//���ļ����а����ݶ�ȡ����
	char* buffer = (char*)malloc(8 * 1024);

	//���ļ����еĴ洢λ����������id * 4 * 1024������,��Щ�洢�Ĵ洢�ıȽϴ�����4k
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.read(pos, buffer, 8 * 1024);

	//���ݲ�ͬ�Ľڵ����ʹ����ڵ�
	char* p = buffer;
	switch (*((unsigned char*)p))
	{
	case NODE_TYPE_ONE:
		pIndexNode = new IndexNodeTypeOne();
		break;
	case NODE_TYPE_TWO:
		pIndexNode = new IndexNodeTypeTwo();
		break;
	case NODE_TYPE_THREE:
		pIndexNode = new IndexNodeTypeThree();
		break;
	case NODE_TYPE_FOUR:
		pIndexNode = new IndexNodeTypeFour();
		break;
	default:
		free(buffer);
		return nullptr;
	}
	p++;
	unsigned short len = *((unsigned short*)p);
	p += 2;
	if ((len + 3) > 4 * 1024)
	{
		pIndexNode->setIsBig(true);
	}

	//�Ѷ�����ת�ɽڵ�����������
	if (!pIndexNode->toObject(p, len))
	{
		delete pIndexNode;
		free(buffer);
		return nullptr;
	}

	free(buffer);

	pIndexNode->setIndexId(indexId);

	return pIndexNode;
}

//��ĳ���ڵ�д�뵽�ļ�����
bool IndexFile::writeFile(unsigned long long indexId, IndexNode* pIndexNode)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}
	//�жϽڵ��Ƿ����Ѿ��޸Ĺ��˵�
	if (!pIndexNode->getIsModified())
	{
		return true;
	}

	char* buffer = (char*)malloc(8 * 1024);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, 8 * 1024 - 1);
	if (!ok)
	{
		free(buffer);
		return false;
	}
	short len = *((short*)p);
	if ((len + 3) > 4 * 1024 && !pIndexNode->getIsBig())
	{
		std::vector<unsigned long long> indexIdVec;
		std::vector<IndexNode*> indexNodeVec;
		//�ڵ�Ĵ�С��Ĭ�ϵ�4k�Ĵ�С��Ҫ�����ʱ��һ�����Ա���8k��С��id
		unsigned long long newIndexId = UniqueGenerator::getUGenerator().acquireTwoNumber();

		//���ڽڵ��id�Ѿ��ı�������ҲҪ�Ѹ��ڵ��Ӧ�ĺ��ӽڵ�id�ͺ��ӽڵ��Ӧ�ĸ��ڵ�id�޸�
		
		//������������ʱ�����ݵ�Ҳ����˵�ó������Ժ�Ҫ����Ż�ȥ��
		unsigned long long parentIndexId = pIndexNode->getParentId();
		if (parentIndexId != 0)
		{
			IndexNode* pTempIndexNode = getTempIndexNode(parentIndexId);
			if (pTempIndexNode == nullptr)
			{
				for (unsigned int i = 0; i < indexIdVec.size(); ++i)
				{
					writeTempFile(indexIdVec[i], indexNodeVec[i]);
				}
				free(buffer);
				return false;
			}

			indexIdVec.push_back(parentIndexId);
			indexNodeVec.push_back(pTempIndexNode);
			
			//�޸ĸ��ڵ��Ӧ���ӽڵ��id
			if (!pTempIndexNode->changeChildIndexId(indexId, newIndexId))
			{
				for (unsigned int i = 0; i < indexIdVec.size(); ++i)
				{
					writeTempFile(indexIdVec[i], indexNodeVec[i]);
				}
				free(buffer);
				return false;
			}
		}

		//�޸������ӽڵ�ĸ��ڵ�id

		//�Ȱ����е��ӽڵ�id�ҳ���
		std::vector<unsigned long long> childIndexId;
		pIndexNode->getAllChildNodeId(childIndexId);

		//�����еĺ��ӽڵ�����ݶ�ȡ����
		std::vector< IndexNode*> childIndexNode;
		for (auto& value : childIndexId)
		{
			IndexNode* childNode = getTempIndexNode(value);
			if (childNode == nullptr)
			{
				for (unsigned int i = 0; i < indexIdVec.size(); ++i)
				{
					writeTempFile(indexIdVec[i], indexNodeVec[i]);
				}
				free(buffer);
				return false;
			}

			indexIdVec.push_back(value);
			indexNodeVec.push_back(childNode);
			childIndexNode.push_back(childNode);
		}

		//�����еĺ��ӽڵ�ĸ��ڵ�id�ĵ�
		for (auto& value : childIndexNode)
		{
			value->setParentID(newIndexId);
		}

		//��������ʱ�򿪵��ļ������ȥ
		for (unsigned int i = 0; i < indexIdVec.size(); ++i)
		{
			writeTempFile(indexIdVec[i], indexNodeVec[i]);
		}

		//���ڵ㻹�����еĺ��ӽڵ�ĸ��ڵ�id���ı����Ժ�����ڵ�������½ڵ�id�ˡ�
		//�������µĽڵ��id���ԾɵĽڵ��id����Ч�˷Ż�ȥ
		UniqueGenerator::getUGenerator().recycleNumber(indexId);
		indexId = newIndexId;
		pIndexNode->setIndexId(indexId);
	}

	//������ڵ������д����������
	*((unsigned char*)buffer) = pIndexNode->getType();
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.write(pos, buffer, len + 3);

	free(buffer);
	return true;
}

bool IndexFile::writeTempFile(unsigned long long indexId, IndexNode* pIndexNode)
{
	if (pIndexNode == nullptr)
	{
		return false;
	}
	//���ȼ���»����Ƿ��Ѿ�����
	auto it = tempIndexNodeId.find(indexId);
	if (it != end(tempIndexNodeId))
	{
		pIndexNode->setIsModified(true);
		tempIndexNodeId.erase(it);
		return true;
	}

	//������д���ļ�����
	char* buffer = (char*)malloc(8 * 1024);
	char* p = buffer + 1;
	bool ok = pIndexNode->toBinary(p, 8 * 1024 - 1);
	if (!ok)
	{
		free(buffer);
		delete pIndexNode;
		return false;
	}

	//���ڶ�ȡ��ʱ�ļ���ʱ��ֻ�Ƕ������id�ֶν����˸Ķ����Դ�С�ǲ����иı��ֱ�Ӵ��뵽�ļ�����Ϳ�����

	//����������д��Ӧ���͵��ֶ�
	*((unsigned char*)buffer) = pIndexNode->getType();
	short len = *((short*)p);
	fpos_t pos;
	pos.__pos = indexId * 4 * 1024;
	indexFile.write(pos, buffer, len + 3);
	
	//д��������Ժ���ڴ�����ͷ�
	free(buffer);
	delete pIndexNode;
	return true;
}

//����ά��һ����С��Ҫ̫��
bool IndexFile::reduceCache()
{
	if (pIndex == nullptr)
	{
		return false;
	}

	unsigned size = pIndex->size();
	if (size <= 1024)
	{
		return true;
	}

	unsigned int needReduceNum = size - 1024;

	//�����ȼ���͵���Щ�ڵ�ȡ������
	std::vector<unsigned long long> indexIdVec;
	std::vector<IndexNode*> indexNodeVec;
	indexIdVec.reserve(needReduceNum);
	indexNodeVec.reserve(needReduceNum);
	
	if (!pIndex->getLastNodes(needReduceNum, indexIdVec, indexNodeVec))
	{
		return false;
	}

	//��������Ҫ���ٵĽڵ�ȫ��д��
	for (unsigned int i = 0; i < needReduceNum; ++i)
	{
		if (!writeFile(indexIdVec[i], indexNodeVec[i]))
		{
			return false;
		}
	}

	//���������������Ӧ����������
	if (!pIndex->reduceCache(needReduceNum))
	{
		return false;
	}
	return true;
}

bool IndexFile::changePreCmpLen(unsigned long long indexId, unsigned long long orgPreCmpLen, unsigned long long newPreCmpLen)
{
	if (pIndex == nullptr)
	{
		return false;
	}

	return pIndex->changePreCmpLen(indexId, orgPreCmpLen, newPreCmpLen);
}

bool IndexFile::swapNode(unsigned long long indexId, IndexNode* newNode)
{
	if (pIndex == nullptr)
	{
		return false;
	}

	if (newNode == nullptr)
	{
		return false;
	}

	return pIndex->swapNode(indexId, newNode);
}

IndexNode* IndexFile::newIndexNode(unsigned char nodeType, unsigned long long preCmpLen)
{
	if (pIndex == nullptr)
	{
		return nullptr;
	}

	return pIndex->newIndexNode(nodeType, preCmpLen);
}

bool IndexFile::deleteIndexNode(unsigned long long indexId)
{
	if (pIndex == nullptr)
	{
		return false;
	}

	return pIndex->deleteIndexNode(indexId);
}