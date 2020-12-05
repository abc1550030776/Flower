#include "KVContent.h"
#include "common.h"
#include <memory.h>

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
				//和节点的这一段完全一样
				//看看孩子节点的大小
				switch (indexNode->getType())
				{
				case NODE_TYPE_ONE:
				{
					IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)indexNode;
					std::unordered_map<unsigned long long, IndexNodeChild>& map = pTmpNode->getChildren();
					unsigned long long biggestLowerKey = *(unsigned long long*)(&kp[cmpLen + len]);
					unsigned long long biggestLowerId = 0;
					bool hasBiggestLowerKey = false;
					unsigned long long smallestUpperKey = *(unsigned long long*)(&kp[cmpLen + len]);
					unsigned long long smallestUpperId = 0;
					bool hasSmallestUpperKey = false;
					unsigned long long findKeyIndexId = 0;
					bool hasEqualKey = false;
					for (auto& val : map)
					{
						if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned long long*)(&kp[cmpLen + len])))
						{
							if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned long long*)(&kp[cmpLen + len]))
							{
								biggestLowerKey = val.first;
								biggestLowerId = val.second.getIndexId();
							}
							hasBiggestLowerKey = true;
						}
						else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned long long*)(&kp[cmpLen + len])))
						{
							if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned long long*)(&kp[cmpLen + len]))
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
							memcpy(&retUpperBound, &bigEndKey, cmpLen + len);
							unsigned char* pUB = (unsigned char*)&retUpperBound;
							*(unsigned long long*)(&pUB[cmpLen + len]) = smallestUpperKey;
							if ((cmpLen + len + 8) < 8)
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
							upperCmpLen = cmpLen + len + 8;
						}
						else
						{
							//如果没有的情况下然后到达了搜索的末尾这个时候应该用之前的最小值继续往下搜索最小值
							if ((cmpLen + len + 8) == 8)
							{
								needFindSmallestUppderBound = true;
							}
						}

						//检查是否已经是结尾了如果是结尾的话就直接设置值
						if (cmpLen + len + 8 == 8)
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
								memcpy(&retLowerBound, &bigEndKey, cmpLen + len);
								unsigned char* pLB = (unsigned char*)&retLowerBound;
								*(unsigned long long*)(&pLB[cmpLen + len]) = biggestLowerKey;
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
								lowerCmpLen = cmpLen + len + 8;
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
							memcpy(&retUpperBound, &bigEndKey, cmpLen + len);
							unsigned char* pUB = (unsigned char*)&retUpperBound;
							*(unsigned long long*)(&pUB[cmpLen + len]) = smallestUpperKey;
							if ((cmpLen + len + 8) < 8)
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
							upperCmpLen = cmpLen + len + 8;
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
							memcpy(&retLowerBound, &bigEndKey, cmpLen + len);
							unsigned char* pLB = (unsigned char*)&retLowerBound;
							*(unsigned long long*)(&pLB[cmpLen + len]) = biggestLowerKey;
							if ((cmpLen + len + 8) < 8)
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
							lowerCmpLen = cmpLen + len + 8;
						}
						else
						{
							//没有相等的值这里也没有比较小的值所以应该是前面的值去找更大的值
							needFindBiggestLowerBound = true;
						}

						//没有相等的key值可以判断退出循环了
						needBreak = true;
					}
				}
				break;
				case NODE_TYPE_TWO:
				{
					IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)indexNode;
					std::unordered_map<unsigned int, IndexNodeChild>& map = pTmpNode->getChildren();
					unsigned int biggestLowerKey = *(unsigned int*)(&kp[cmpLen + len]);
					unsigned long long biggestLowerId = 0;
					bool hasBiggestLowerKey = false;
					unsigned int smallestUpperKey = *(unsigned int*)(&kp[cmpLen + len]);
					unsigned long long smallestUpperId = 0;
					bool hasSmallestUpperKey = false;
					unsigned long long findKeyIndexId = 0;
					bool hasEqualKey = false;
					for (auto& val : map)
					{
						if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned int*)(&kp[cmpLen + len])))
						{
							if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned int*)(&kp[cmpLen + len]))
							{
								biggestLowerKey = val.first;
								biggestLowerId = val.second.getIndexId();
							}
							hasBiggestLowerKey = true;
						}
						else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned int*)(&kp[cmpLen + len])))
						{
							if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned int*)(&kp[cmpLen + len]))
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
							memcpy(&retUpperBound, &bigEndKey, cmpLen + len);
							unsigned char* pUB = (unsigned char*)&retUpperBound;
							*(unsigned int*)(&pUB[cmpLen + len]) = smallestUpperKey;
							if ((cmpLen + len + 4) < 8)
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
							upperCmpLen = cmpLen + len + 4;
						}
						else
						{
							//如果没有的情况下然后到达了搜索的末尾这个时候应该用之前的最小值继续往下搜索最小值
							if ((cmpLen + len + 4) == 8)
							{
								needFindSmallestUppderBound = true;
							}
						}

						//检查是否已经是结尾了如果是结尾的话就直接设置值
						if (cmpLen + len + 4 == 8)
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
								memcpy(&retLowerBound, &bigEndKey, cmpLen + len);
								unsigned char* pLB = (unsigned char*)&retLowerBound;
								*(unsigned int*)(&pLB[cmpLen + len]) = biggestLowerKey;
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
								lowerCmpLen = cmpLen + len + 4;
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
							memcpy(&retUpperBound, &bigEndKey, cmpLen + len);
							unsigned char* pUB = (unsigned char*)&retUpperBound;
							*(unsigned int*)(&pUB[cmpLen + len]) = smallestUpperKey;
							if ((cmpLen + len + 4) < 8)
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
							upperCmpLen = cmpLen + len + 4;
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
							memcpy(&retLowerBound, &bigEndKey, cmpLen + len);
							unsigned char* pLB = (unsigned char*)&retLowerBound;
							*(unsigned int*)(&pLB[cmpLen + len]) = biggestLowerKey;
							if ((cmpLen + len + 4) < 8)
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
							lowerCmpLen = cmpLen + len + 4;
						}
						else
						{
							//没有相等的值这里也没有比较小的值所以应该是前面的值去找更大的值
							needFindBiggestLowerBound = true;
						}

						//没有相等的key值可以判断退出循环了
						needBreak = true;
					}
				}
				break;
				case NODE_TYPE_THREE:
				{
					IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)indexNode;
					std::unordered_map<unsigned short, IndexNodeChild>& map = pTmpNode->getChildren();
					unsigned short biggestLowerKey = *(unsigned short*)(&kp[cmpLen + len]);
					unsigned long long biggestLowerId = 0;
					bool hasBiggestLowerKey = false;
					unsigned short smallestUpperKey = *(unsigned short*)(&kp[cmpLen + len]);
					unsigned long long smallestUpperId = 0;
					bool hasSmallestUpperKey = false;
					unsigned long long findKeyIndexId = 0;
					bool hasEqualKey = false;
					for (auto& val : map)
					{
						if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(*(unsigned short*)(&kp[cmpLen + len])))
						{
							if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(biggestLowerKey) || biggestLowerKey == *(unsigned short*)(&kp[cmpLen + len]))
							{
								biggestLowerKey = val.first;
								biggestLowerId = val.second.getIndexId();
							}
							hasBiggestLowerKey = true;
						}
						else if (swiftBigLittleEnd(val.first) > swiftBigLittleEnd(*(unsigned short*)(&kp[cmpLen + len])))
						{
							if (swiftBigLittleEnd(val.first) < swiftBigLittleEnd(smallestUpperKey) || smallestUpperKey == *(unsigned short*)(&kp[cmpLen + len]))
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
							memcpy(&retUpperBound, &bigEndKey, cmpLen + len);
							unsigned char* pUB = (unsigned char*)&retUpperBound;
							*(unsigned short*)(&pUB[cmpLen + len]) = smallestUpperKey;
							if ((cmpLen + len + 2) < 8)
							{
								if (upperIndexNode != nullptr)
								{
									indexFile.putIndexNode(upperIndexNode);
								}

								upperIndexNode = indexFile.getIndexNode(smallestUpperId);
								if (upperIndexNode == nullptr)
								{
									if (lowerIndexNode == nullptr)
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
							upperCmpLen = cmpLen + len + 2;
						}
						else
						{
							if ((cmpLen + len + 2) == 8)
							{
								needFindSmallestUppderBound = true;
							}
						}

						//检查是否已经是结尾了如果是结尾的话就直接设置值
						if (cmpLen + len + 2 == 8)
						{
							retLowerBound = bigEndKey;
							value = findKeyIndexId;
							lowerCmpLen = 8;
						}
						else
						{
							if (hasBiggestLowerKey)
							{
								memcpy(&retLowerBound, &bigEndKey, cmpLen + len);
								unsigned char* pLB = (unsigned char*)&retLowerBound;
								*(unsigned short*)(&pLB[cmpLen + len]) = biggestLowerKey;
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
								lowerCmpLen = cmpLen + len + 2;
							}
						}
					}
					else
					{
						if (hasSmallestUpperKey)
						{
							memcpy(&retUpperBound, &bigEndKey, cmpLen + len);
							unsigned char* pUB = (unsigned char*)&retUpperBound;
							*(unsigned short*)(&pUB[cmpLen + len]) = smallestUpperKey;
							if ((cmpLen + len + 2) < 8)
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
							upperCmpLen = cmpLen + len + 2;
						}
						else
						{
							needFindSmallestUppderBound = true;
						}

						if (hasBiggestLowerKey)
						{
							memcpy(&retLowerBound, &bigEndKey, cmpLen + len);
							unsigned char* pLB = (unsigned char*)&retLowerBound;
							*(unsigned short*)(&pLB[cmpLen + len]) = biggestLowerKey;
							if ((cmpLen + len + 4) < 8)
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
							lowerCmpLen = cmpLen + len + 2;
						}
						else
						{
							needFindBiggestLowerBound = true;
						}

						needBreak = true;
					}
				}
					break;
				}
			}
				break;
			}
		}
	}

	indexFile.reduceCache();
	return true;
}
