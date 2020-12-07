#include "KVContent.h"
#include "common.h"
#include <memory.h>
#include "IndexNode.h"

bool KVContent::init(const char* fileName, Index* index)
{
	return indexFile.init(fileName, index);
}

const char CMP_STATUS_LITTLE = 0;
const char CMP_STATUS_BIG = 1;
const char CMP_STATUS_EQUAL = 2;

bool KVContent::get(unsigned long long key, unsigned long long& lowerBound, unsigned long long& upperBound, unsigned long long& value)
{
	unsigned long long bigEndKey = swiftBigLittleEnd(key);
	unsigned long long rootIndexId = indexFile.getRootIndexId();
	unsigned char* kp = (unsigned char*)&bigEndKey;
	if (rootIndexId == 0)
	{
		return false;
	}

	IndexNode* rootIndexNode = indexFile.getIndexNode(rootIndexId);
	if (rootIndexNode == nullptr)
	{
		return false;
	}
	unsigned long long retLowerBound = 0;
	unsigned long long retUpperBound = 0;
	unsigned long long cmpLen = 0;					//这里记录主查询的比较长度
	unsigned long long lowerCmpLen = 0;				//比较低的位置的比较长度
	unsigned long long upperCmpLen = 0;				//比较高的位置的比较长度
	IndexNode* indexNode = rootIndexNode;
	IndexNode* lowerIndexNode = nullptr;
	IndexNode* upperIndexNode = nullptr;
	bool needFindBiggestLowerBound = false;
	bool needFindSmallestUppderBound = false;
	while (cmpLen != 8)
	{
		unsigned long long len = indexNode->getLen();
		unsigned long long partOfKey = indexNode->getPartOfKey();
		unsigned char* p = (unsigned char*)&partOfKey;
		if (len != 0)
		{
			unsigned char status = CMP_STATUS_EQUAL;
			unsigned long long subCmpLen = 0;
			while (subCmpLen != len)
			{
				if ((len - subCmpLen) >= 4)
				{
					if (*(unsigned int*)(&kp[cmpLen + subCmpLen]) == *(unsigned int*)(&p[subCmpLen]))
					{
						subCmpLen += 4;
					}
					else if (swiftBigLittleEnd(*(unsigned int*)(&p[subCmpLen])) < swiftBigLittleEnd(*(unsigned int*)(&kp[cmpLen + subCmpLen])))
					{
						status = CMP_STATUS_LITTLE;
						break;
					}
					else
					{
						status = CMP_STATUS_BIG;
						break;
					}
				}
				else if ((len - subCmpLen) >= 2)
				{
					if (*(unsigned short*)(&kp[cmpLen + subCmpLen]) == *(unsigned short*)(&p[subCmpLen]))
					{
						subCmpLen += 2;
					}
					else if (swiftBigLittleEnd(*(unsigned short*)(&p[subCmpLen])) < swiftBigLittleEnd(*(unsigned short*)(&kp[cmpLen + subCmpLen])))
					{
						status = CMP_STATUS_LITTLE;
						break;
					}
					else
					{
						status = CMP_STATUS_BIG;
						break;
					}
				}
				else
				{
					if (*(unsigned char*)(&kp[cmpLen + subCmpLen]) == *(unsigned char*)(&p[subCmpLen]))
					{
						subCmpLen += 1;
					}
					else if (*(unsigned char*)(&p[subCmpLen]) < *(unsigned char*)(&kp[cmpLen + subCmpLen]))
					{
						status = CMP_STATUS_LITTLE;
						break;
					}
					else
					{
						status = CMP_STATUS_BIG;
						break;
					}
				}
			}

			//比较到某个位置如果找到不一样的地方的话就可以退出循环了。
			bool needBreak = false;
			//根据比较的结果进行处理
			switch (status)
			{
			case CMP_STATUS_EQUAL:
			{
				cmpLen += len;
			}
				break;
			case CMP_STATUS_BIG:
				//目前的部分比搜素的key值大
				needFindBiggestLowerBound = true;
				memcpy(&retUpperBound, &bigEndKey, cmpLen);
				upperIndexNode = indexNode;
				indexNode = nullptr;
				needFindSmallestUppderBound = true;
				needBreak = true;
				break;
			case CMP_STATUS_LITTLE:
				needFindSmallestUppderBound = true;
				memcpy(&retLowerBound, &bigEndKey, cmpLen);
				lowerIndexNode = indexNode;
				indexNode = nullptr;
				needFindBiggestLowerBound = true;
				needBreak = true;
				break;
			default:
				break;
			}
			if (needBreak)
			{
				break;
			}
		}

		bool needBreak = false;
		//节点的这一段和key值完全一样或者是长度是0
		//看看孩子节点的大小
		switch (indexNode->getType())
		{
		case NODE_TYPE_ONE:
		{
			IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)indexNode;
			std::unordered_map<unsigned long long, IndexNodeChild>& map = pTmpNode->getChildren();
			unsigned long long biggestLowerKey = *(unsigned long long*)(&kp[cmpLen]);
			unsigned long long biggestLowerId = 0;
			bool hasBiggestLowerKey = false;
			unsigned long long smallestUpperKey = *(unsigned long long*)(&kp[cmpLen]);
			unsigned long long smallestUpperId = 0;
			bool hasSmallestUpperKey = false;
			unsigned long long findKeyIndexId = 0;
			bool hasEqualKey = false;
			for (auto& val : map)
			{
				if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned long long*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned long long*)(&kp[cmpLen]))
					{
						biggestLowerKey = val.first;
						biggestLowerId = val.second.getIndexId();
					}
					hasBiggestLowerKey = true;
				}
				else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned long long*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned long long*)(&kp[cmpLen]))
					{
						smallestUpperKey = val.first;
						smallestUpperId = val.second.getIndexId();
					}
					hasSmallestUpperKey = true;
				}
				else
				{
					findKeyIndexId = val.second.getIndexId();
					hasEqualKey = true;
				}
			}

			//这里分几种情况考虑
			if (hasEqualKey)
			{
				//有到这一部分完全相等的
				//检查有没有比到这里这个key有更大的
				if (hasSmallestUpperKey)
				{
					//有最小的比这个key值更大的值
					//设置到目前位置最小的比这个key更大的值
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned long long*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 8) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
					}
					upperCmpLen = cmpLen + 8;
				}
				else
				{
					//如果没有的情况下然后到达了搜索的末尾这个时候应该用之前的最小值继续往下搜索最小值
					if ((cmpLen + 8) == 8)
					{
						needFindSmallestUppderBound = true;
					}
				}

				//检查是否已经是结尾了如果是结尾的话就直接设置值
				if (cmpLen+ 8 == 8)
				{
					retLowerBound = bigEndKey;
					value = findKeyIndexId;
					lowerCmpLen = 8;
				}
				else
				{
					if (hasBiggestLowerKey)
					{
						//有最大的比这个key值更小的值
						//设置到目前位置的比这个key更小的值
						memcpy(&retLowerBound, &bigEndKey, cmpLen);
						unsigned char* pLB = (unsigned char*)&retLowerBound;
						*(unsigned long long*)(&pLB[cmpLen]) = biggestLowerKey;
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						lowerCmpLen = cmpLen + 8;
					}

					if (indexNode != nullptr && indexNode != rootIndexNode)
					{
						indexFile.putIndexNode(indexNode);
					}

					indexNode = indexFile.getIndexNode(findKeyIndexId);
					if (indexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (rootIndexNode != nullptr)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						return false;
					}
				}
			}
			else
			{
				//到这个位置没有相等的key
				if (hasSmallestUpperKey)
				{
					//有最小的比这个key值更大的值
					//设置到目前位置最小的比这个key更大的值
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned long long*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 8) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						//由于没有相同的而且还没有搜索到末尾所以接下来找最小的比这个key大的值
						needFindSmallestUppderBound = true;
					}
					upperCmpLen = cmpLen + 8;
				}
				else
				{
					//这个位置完全没有比key大的相等的都没有就上面的往下搜索最小的key
					needFindSmallestUppderBound = true;
				}

				if (hasBiggestLowerKey)
				{
					//有最大的比这个key值更小的值
					//设置到目前位置的比这个key更小的值
					memcpy(&retLowerBound, &bigEndKey, cmpLen);
					unsigned char* pLB = (unsigned char*)&retLowerBound;
					*(unsigned long long*)(&pLB[cmpLen]) = biggestLowerKey;
					if ((cmpLen + 8) < 8)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						//由于没有和key相等然后这里比key值小所以要继续找最大值
						needFindBiggestLowerBound = true;
					}
					else
					{
						//找到最后这个值比key值小所以这个应该是最大值
						value = biggestLowerId;
					}
					lowerCmpLen = cmpLen + 8;
				}
				else
				{
					//没有相等的值这里也没有比较小的值所以应该是前面的值去找更大的值
					needFindBiggestLowerBound = true;
				}
				needBreak = true;
			}
			cmpLen += 8;
		}
		break;
		case NODE_TYPE_TWO:
		{
			IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)indexNode;
			std::unordered_map<unsigned int, IndexNodeChild>& map = pTmpNode->getChildren();
			unsigned int biggestLowerKey = *(unsigned int*)(&kp[cmpLen]);
			unsigned long long biggestLowerId = 0;
			bool hasBiggestLowerKey = false;
			unsigned int smallestUpperKey = *(unsigned int*)(&kp[cmpLen]);
			unsigned long long smallestUpperId = 0;
			bool hasSmallestUpperKey = false;
			unsigned long long findKeyIndexId = 0;
			bool hasEqualKey = false;
			for (auto& val : map)
			{
				if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned int*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned int*)(&kp[cmpLen]))
					{
						biggestLowerKey = val.first;
						biggestLowerId = val.second.getIndexId();
					}
					hasBiggestLowerKey = true;
				}
				else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned int*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned int*)(&kp[cmpLen]))
					{
						smallestUpperKey = val.first;
						smallestUpperId = val.second.getIndexId();
					}
					hasSmallestUpperKey = true;
				}
				else
				{
					findKeyIndexId = val.second.getIndexId();
					hasEqualKey = true;
				}
			}

			//这里分几种情况考虑
			if (hasEqualKey)
			{
				//有到这一部分完全相等的
				//检查有没有比到这里这个key有更大的
				if (hasSmallestUpperKey)
				{
					//有最小的比这个key值更大的值
					//设置到目前位置最小的比这个key更大的值
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned int*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 4) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
					}
					upperCmpLen = cmpLen + 4;
				}
				else
				{
					//如果没有的情况下然后到达了搜索的末尾这个时候应该用之前的最小值继续往下搜索最小值
					if ((cmpLen + 4) == 8)
					{
						needFindSmallestUppderBound = true;
					}
				}

				//检查是否已经是结尾了如果是结尾的话就直接设置值
				if (cmpLen + 4 == 8)
				{
					retLowerBound = bigEndKey;
					value = findKeyIndexId;
					lowerCmpLen = 8;
				}
				else
				{
					if (hasBiggestLowerKey)
					{
						//有最大的比这个key值更小的值
						//设置到目前位置的比这个key更小的值
						memcpy(&retLowerBound, &bigEndKey, cmpLen);
						unsigned char* pLB = (unsigned char*)&retLowerBound;
						*(unsigned int*)(&pLB[cmpLen]) = biggestLowerKey;
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						lowerCmpLen = cmpLen + 4;
					}

					if (indexNode != nullptr && indexNode != rootIndexNode)
					{
						indexFile.putIndexNode(indexNode);
					}

					indexNode = indexFile.getIndexNode(findKeyIndexId);
					if (indexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (rootIndexNode != nullptr)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						return false;
					}
				}
			}
			else
			{
				//到这个位置没有相等的key
				if (hasSmallestUpperKey)
				{
					//有最小的比这个key值更大的值
					//设置到目前位置最小的比这个key更大的值
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned int*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 4) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						//由于没有相同的而且还没有搜索到末尾所以接下来找最小的比这个key大的值
						needFindSmallestUppderBound = true;
					}
					upperCmpLen = cmpLen + 4;
				}
				else
				{
					//这个位置完全没有比key大的相等的都没有就上面的往下搜索最小的key
					needFindSmallestUppderBound = true;
				}

				if (hasBiggestLowerKey)
				{
					//有最大的比这个key值更小的值
					//设置到目前位置的比这个key更小的值
					memcpy(&retLowerBound, &bigEndKey, cmpLen);
					unsigned char* pLB = (unsigned char*)&retLowerBound;
					*(unsigned int*)(&pLB[cmpLen]) = biggestLowerKey;
					if ((cmpLen + 4) < 8)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						//由于没有和key相等然后这里比key值小所以要继续找最大值
						needFindBiggestLowerBound = true;
					}
					else
					{
						//找到最后这个值比key值小所以这个应该是最大值
						value = biggestLowerId;
					}
					lowerCmpLen = cmpLen + 4;
				}
				else
				{
					//没有相等的值这里也没有比较小的值所以应该是前面的值去找更大的值
					needFindBiggestLowerBound = true;
				}
				needBreak = true;
			}
			cmpLen += 4;
		}
		break;
		case NODE_TYPE_THREE:
		{
			IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)indexNode;
			std::unordered_map<unsigned short, IndexNodeChild>& map = pTmpNode->getChildren();
			unsigned short biggestLowerKey = *(unsigned short*)(&kp[cmpLen]);
			unsigned long long biggestLowerId = 0;
			bool hasBiggestLowerKey = false;
			unsigned short smallestUpperKey = *(unsigned short*)(&kp[cmpLen]);
			unsigned long long smallestUpperId = 0;
			bool hasSmallestUpperKey = false;
			unsigned long long findKeyIndexId = 0;
			bool hasEqualKey = false;
			for (auto& val : map)
			{
				if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned short*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned short*)(&kp[cmpLen]))
					{
						biggestLowerKey = val.first;
						biggestLowerId = val.second.getIndexId();
					}
					hasBiggestLowerKey = true;
				}
				else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned short*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned short*)(&kp[cmpLen]))
					{
						smallestUpperKey = val.first;
						smallestUpperId = val.second.getIndexId();
					}
					hasSmallestUpperKey = true;
				}
				else
				{
					findKeyIndexId = val.second.getIndexId();
					hasEqualKey = true;
				}
			}

			//这里分几种情况考虑
			if (hasEqualKey)
			{
				//有到这一部分完全相等的
				//检查有没有比到这里这个key有更大的
				if (hasSmallestUpperKey)
				{
					//有最小的比这个key值更大的值
					//设置到目前位置最小的比这个key更大的值
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned short*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 2) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
					}
					upperCmpLen = cmpLen + 2;
				}
				else
				{
					if ((cmpLen + 2) == 8)
					{
						needFindSmallestUppderBound = true;
					}
				}

				//检查是否已经是结尾了如果是结尾的话就直接设置值
				if (cmpLen + 2 == 8)
				{
					retLowerBound = bigEndKey;
					value = findKeyIndexId;
					lowerCmpLen = 8;
				}
				else
				{
					if (hasBiggestLowerKey)
					{
						memcpy(&retLowerBound, &bigEndKey, cmpLen);
						unsigned char* pLB = (unsigned char*)&retLowerBound;
						*(unsigned short*)(&pLB[cmpLen]) = biggestLowerKey;
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						lowerCmpLen = cmpLen + 2;
					}

					if (indexNode != nullptr && indexNode != rootIndexNode)
					{
						indexFile.putIndexNode(indexNode);
					}

					indexNode = indexFile.getIndexNode(findKeyIndexId);
					if (indexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (rootIndexNode != nullptr)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						return false;
					}
				}
			}
			else
			{
				if (hasSmallestUpperKey)
				{
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned short*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 2) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						needFindSmallestUppderBound = true;
					}
					upperCmpLen = cmpLen + 2;
				}
				else
				{
					needFindSmallestUppderBound = true;
				}

				if (hasBiggestLowerKey)
				{
					memcpy(&retLowerBound, &bigEndKey, cmpLen);
					unsigned char* pLB = (unsigned char*)&retLowerBound;
					*(unsigned short*)(&pLB[cmpLen]) = biggestLowerKey;
					if ((cmpLen + 2) < 8)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						needFindBiggestLowerBound = true;
					}
					else
					{
						value = biggestLowerId;
					}
					lowerCmpLen = cmpLen + 2;
				}
				else
				{
					needFindBiggestLowerBound = true;
				}
				needBreak = true;
			}
			cmpLen += 2;
		}
		break;
		case NODE_TYPE_FOUR:
		{
			IndexNodeTypeFour* pTmpNode = (IndexNodeTypeFour*)indexNode;
			std::unordered_map<unsigned char, IndexNodeChild>& map = pTmpNode->getChildren();
			unsigned char biggestLowerKey = *(unsigned char*)(&kp[cmpLen]);
			unsigned long long biggestLowerId = 0;
			bool hasBiggestLowerKey = false;
			unsigned char smallestUpperKey = *(unsigned char*)(&kp[cmpLen]);
			unsigned long long smallestUpperId = 0;
			bool hasSmallestUpperKey = false;
			unsigned long long findKeyIndexId = 0;
			bool hasEqualKey = false;
			for (auto& val : map)
			{
				if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned char*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned char*)(&kp[cmpLen]))
					{
						biggestLowerKey = val.first;
						biggestLowerId = val.second.getIndexId();
					}
					hasBiggestLowerKey = true;
				}
				else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned char*)(&kp[cmpLen])))
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned char*)(&kp[cmpLen]))
					{
						smallestUpperKey = val.first;
						smallestUpperId = val.second.getIndexId();
					}
				}
				else
				{
					findKeyIndexId = val.second.getIndexId();
					hasEqualKey = true;
				}
			}

			if (hasEqualKey)
			{
				if (hasSmallestUpperKey)
				{
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)retUpperBound;
					*(unsigned char*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 1) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
					}
					upperCmpLen = cmpLen + 2;
				}
				else
				{
					if ((cmpLen + 1) == 8)
					{
						needFindSmallestUppderBound = true;
					}
				}

				if (cmpLen + 1 == 8)
				{
					retLowerBound = bigEndKey;
					value = findKeyIndexId;
					lowerCmpLen = 8;
				}
				else
				{
					if (hasBiggestLowerKey)
					{
						memcpy(&retLowerBound, &bigEndKey, cmpLen);
						unsigned char* pLB = (unsigned char*)&retLowerBound;
						*(unsigned char*)(&pLB[cmpLen]) = biggestLowerKey;
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						lowerCmpLen = cmpLen + 1;
					}

					if (indexNode != nullptr && indexNode != rootIndexNode)
					{
						indexFile.putIndexNode(indexNode);
					}

					indexNode = indexFile.getIndexNode(findKeyIndexId);
					if (indexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (rootIndexNode != nullptr)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						return false;
					}
				}
			}
			else
			{
				if (hasSmallestUpperKey)
				{
					memcpy(&retUpperBound, &bigEndKey, cmpLen);
					unsigned char* pUB = (unsigned char*)&retUpperBound;
					*(unsigned char*)(&pUB[cmpLen]) = smallestUpperKey;
					if ((cmpLen + 1) < 8)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						upperIndexNode = indexFile.getIndexNode(smallestUpperId);
						if (upperIndexNode == nullptr)
						{
							if (lowerIndexNode != nullptr)
							{
								indexFile.putIndexNode(lowerIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						needFindSmallestUppderBound = true;
					}
					upperCmpLen = cmpLen + 1;
				}
				else
				{
					needFindSmallestUppderBound = true;
				}

				if (hasBiggestLowerKey)
				{
					memcpy(&retLowerBound, &bigEndKey, cmpLen);
					unsigned char* pLB = (unsigned char*)&retLowerBound;
					*(unsigned char*)(&pLB[cmpLen]) = biggestLowerKey;
					if ((cmpLen + 1) < 8)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						lowerIndexNode = indexFile.getIndexNode(biggestLowerId);
						if (lowerIndexNode == nullptr)
						{
							if (upperIndexNode != nullptr)
							{
								indexFile.putIndexNode(upperIndexNode);
							}

							if (indexNode != rootIndexNode)
							{
								indexFile.putIndexNode(rootIndexNode);
							}

							indexFile.putIndexNode(indexNode);
							return false;
						}
						needFindBiggestLowerBound = true;
					}
					else
					{
						value = biggestLowerId;
					}
					lowerCmpLen = cmpLen + 1;
				}
				else
				{
					needFindBiggestLowerBound = true;
				}
				needBreak = true;
			}
			cmpLen += 1;
		}
			break;
		default:
			break;
		}

		if (needBreak)
		{
			break;
		}
	}

	//判断需不需要寻找最大的lowerbound
	if (needFindBiggestLowerBound)
	{
		if (lowerIndexNode == nullptr)
		{
			//需要寻找最大的lowerbound但是前面又没取得任何小于key的lowerbound所以这样的值是不存在的不存在就直接返回false
			if (upperIndexNode != nullptr)
			{
				indexFile.putIndexNode(upperIndexNode);
			}

			if (indexNode != rootIndexNode)
			{
				indexFile.putIndexNode(rootIndexNode);
			}

			if (indexNode != nullptr)
			{
				indexFile.putIndexNode(indexNode);
			}

			return false;
		}
		while (lowerCmpLen != 8)
		{
			if (lowerIndexNode->getLen() != 0)
			{
				unsigned long long partOfKey = lowerIndexNode->getPartOfKey();
				unsigned char* pLB = (unsigned char*)&retLowerBound;
				memcpy(&pLB[lowerCmpLen], &partOfKey, lowerIndexNode->getLen());
				lowerCmpLen += lowerIndexNode->getLen();
			}

			//一直找最大的孩子节点
			switch (lowerIndexNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				IndexNodeTypeOne* pTmpIndexNode = (IndexNodeTypeOne*)lowerIndexNode;
				std::unordered_map<unsigned long long, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned long long key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pLB = (unsigned char*)&retLowerBound;
				memcpy(&pLB[lowerCmpLen], &key, 8);
				//找到了最大值判断是否已经到达了8个字节
				if (lowerCmpLen + 8 < 8)
				{
					indexFile.putIndexNode(lowerIndexNode);

					lowerIndexNode = indexFile.getIndexNode(indexId);
					if (lowerIndexNode == nullptr)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				else
				{
					//这个时候已经搜索完了设置值
					value = indexId;
				}
				lowerCmpLen += 8;
			}
				break;
			case NODE_TYPE_TWO:
			{
				IndexNodeTypeTwo* pTmpIndexNode = (IndexNodeTypeTwo*)lowerIndexNode;
				std::unordered_map<unsigned int, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned int key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pLB = (unsigned char*)&retLowerBound;
				memcpy(&pLB[lowerCmpLen], &key, 4);
				if (lowerCmpLen + 4 < 8)
				{
					indexFile.putIndexNode(lowerIndexNode);

					lowerIndexNode = indexFile.getIndexNode(indexId);
					if (lowerIndexNode == nullptr)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				else
				{
					value = indexId;
				}
				lowerCmpLen += 4;
			}
				break;
			case NODE_TYPE_THREE:
			{
				IndexNodeTypeThree* pTmpIndexNode = (IndexNodeTypeThree*)lowerIndexNode;
				std::unordered_map<unsigned short, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned short key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pLB = (unsigned char*)&retLowerBound;
				memcpy(&pLB[lowerCmpLen], &key, 2);
				if (lowerCmpLen + 2 < 8)
				{
					indexFile.putIndexNode(lowerIndexNode);

					lowerIndexNode = indexFile.getIndexNode(indexId);
					if (lowerIndexNode == nullptr)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				else
				{
					value = indexId;
				}
				lowerCmpLen += 2;
			}
				break;
			case NODE_TYPE_FOUR:
			{
				IndexNodeTypeFour* pTmpIndexNode = (IndexNodeTypeFour*)lowerIndexNode;
				std::unordered_map<unsigned char, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned char key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pLB = (unsigned char*)&retLowerBound;
				memcpy(&pLB[lowerCmpLen], &key, 1);
				if (lowerCmpLen + 1 < 8)
				{
					indexFile.putIndexNode(lowerIndexNode);

					lowerIndexNode = indexFile.getIndexNode(indexId);
					if (lowerIndexNode == nullptr)
					{
						if (upperIndexNode != nullptr)
						{
							indexFile.putIndexNode(upperIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				else
				{
					value = indexId;
				}
				lowerCmpLen += 1;
			}
				break;
			default:
				break;
			}
		}
	}

	//判断需不需要找最小的upperBound
	if (needFindSmallestUppderBound)
	{
		if (upperIndexNode == nullptr)
		{
			//需要寻找小小的upperbound但是前面又没取得任何大于key的upperbound所以这样的值是不存在的不存在就直接让值等于查找的值
			retUpperBound = bigEndKey;
		}

		while (upperCmpLen != 8)
		{
			if (upperIndexNode->getLen() != 0)
			{
				unsigned long long partOfKey = upperIndexNode->getPartOfKey();
				unsigned char* pLB = (unsigned char*)&retUpperBound;
				memcpy(&pLB[upperCmpLen], &partOfKey, upperIndexNode->getLen());
				upperCmpLen += lowerIndexNode->getLen();
			}

			//一直找最小的孩子节点
			switch (upperIndexNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				IndexNodeTypeOne* pTmpIndexNode = (IndexNodeTypeOne*)upperIndexNode;
				std::unordered_map<unsigned long long, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned long long key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pUB = (unsigned char*)&retUpperBound;
				memcpy(&pUB[upperCmpLen], &key, 8);
				if (upperCmpLen + 8 < 8)
				{
					indexFile.putIndexNode(upperIndexNode);

					upperIndexNode = indexFile.getIndexNode(indexId);
					if (upperIndexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				upperCmpLen += 8;
			}
				break;
			case NODE_TYPE_TWO:
			{
				IndexNodeTypeTwo* pTmpIndexNode = (IndexNodeTypeTwo*)upperIndexNode;
				std::unordered_map<unsigned int, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned int key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pUB = (unsigned char*)&retUpperBound;
				memcpy(&pUB[upperCmpLen], &key, 4);
				if (upperCmpLen + 4 < 8)
				{
					indexFile.putIndexNode(upperIndexNode);

					upperIndexNode = indexFile.getIndexNode(indexId);
					if (upperIndexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				upperCmpLen += 4;
			}
				break;
			case NODE_TYPE_THREE:
			{
				IndexNodeTypeThree* pTmpIndexNode = (IndexNodeTypeThree*)upperIndexNode;
				std::unordered_map<unsigned short, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned short key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pUB = (unsigned char*)&retUpperBound;
				memcpy(&pUB[upperCmpLen], &key, 2);
				if (upperCmpLen + 2 < 8)
				{
					indexFile.putIndexNode(upperIndexNode);

					upperIndexNode = indexFile.getIndexNode(indexId);
					if (upperIndexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				upperIndexNode += 2;
			}
				break;
			case NODE_TYPE_FOUR:
			{
				IndexNodeTypeFour* pTmpIndexNode = (IndexNodeTypeFour*)upperIndexNode;
				std::unordered_map<unsigned char, IndexNodeChild>& map = pTmpIndexNode->getChildren();
				unsigned char key = map.begin()->first;
				unsigned long long indexId = map.begin()->second.getIndexId();
				for (auto& val : map)
				{
					if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(key))
					{
						key = val.first;
						indexId = val.second.getIndexId();
					}
				}
				unsigned char* pUB = (unsigned char*)&retUpperBound;
				memcpy(&pUB[upperCmpLen], &key, 1);
				if (upperCmpLen + 1 < 8)
				{
					indexFile.putIndexNode(upperIndexNode);

					upperIndexNode = indexFile.getIndexNode(indexId);
					if (upperIndexNode == nullptr)
					{
						if (lowerIndexNode != nullptr)
						{
							indexFile.putIndexNode(lowerIndexNode);
						}

						if (indexNode != rootIndexNode)
						{
							indexFile.putIndexNode(rootIndexNode);
						}

						if (indexNode != nullptr)
						{
							indexFile.putIndexNode(indexNode);
						}
						return false;
					}
				}
				upperCmpLen += 1;
			}
				break;
			default:
				break;
			}
		}
	}

	//lowerbound和upperbound都找出来了以后把结果返回
	lowerBound = swiftBigLittleEnd(retLowerBound);
	upperBound = swiftBigLittleEnd(retUpperBound);

	//多线程取了某些指针还回去
	if (lowerIndexNode != nullptr)
	{
		indexFile.putIndexNode(lowerIndexNode);
	}

	if (upperIndexNode != nullptr)
	{
		indexFile.putIndexNode(upperIndexNode);
	}

	if (indexNode != rootIndexNode)
	{
		indexFile.putIndexNode(rootIndexNode);
	}

	if (indexNode != nullptr)
	{
		indexFile.putIndexNode(indexNode);
	}

	indexFile.reduceCache();
	return true;
}
