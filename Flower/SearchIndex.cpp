#include "SearchIndex.h"
#include "memory.h"
#include "common.h"
#include <sys/stat.h>
#include <deque>

SearchIndex::SearchIndex()
{
	searchTarget = nullptr;
	targetLen = 0;
	resultSet = nullptr;
	dstFileSize = 0;
	skipCharNum = 0;
}

bool SearchIndex::init(const char* searchTarget, unsigned int targetLen, SetWithLock* resultSet, const char* fileName, Index* index, unsigned char skipCharNum)
{
	if (index == nullptr)
	{
		return false;
	}

	if (searchTarget == nullptr)
	{
		return false;
	}

	if (fileName == nullptr)
	{
		return false;
	}

	if (resultSet == nullptr)
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

	if (!dstFile.init(fileName, false))
	{
		return false;
	}

	if (!indexFile.init(indexFileName, index))
	{
		return false;
	}

	this->searchTarget = searchTarget;
	this->targetLen = targetLen;
	this->resultSet = resultSet;

	//读取目标文件的大小
	struct stat statbuf;
	stat(fileName, &statbuf);
	dstFileSize = statbuf.st_size;
	this->skipCharNum = skipCharNum;
	return true;
}

class SkipStruct
{
public:
	SkipStruct()
	{
		memset(this, 0, sizeof(*this));
	}
	void setSkipNum(unsigned char skipNum)
	{
		this->skipNum = skipNum;
	}

	void setIndexId(unsigned long long indexId)
	{
		this->indexId = indexId;
	}

	unsigned char getSkipNum()
	{
		return skipNum;
	}

	unsigned long long getIndexId()
	{
		return indexId;
	}
private:
	unsigned char skipNum;
	unsigned long long indexId;
};

class SearchTask
{
public:
	SearchTask()
	{
		memset(this, 0, sizeof(*this));
	}

	unsigned char getIndexIdOrStartPos()
	{
		return indexIdOrStartPos;
	}

	unsigned long long getSkipSize()
	{
		return skipSize;
	}

	unsigned long long getIndexId()
	{
		return indexId;
	}

	unsigned int getTargetStart()
	{
		return targetStart;
	}

	void setIndexIdOrStartPos(unsigned char indexIdOrStartPos)
	{
		this->indexIdOrStartPos = indexIdOrStartPos;
	}

	void setSkipSize(unsigned long long skipSize)
	{
		this->skipSize = skipSize;
	}

	void setIndexId(unsigned long long indexId)
	{
		this->indexId = indexId;
	}

	void setTargetStart(unsigned int targetStart)
	{
		this->targetStart = targetStart;
	}
private:
	unsigned char indexIdOrStartPos;						//接下来搜索的indexId表示的是indexId还是在文件当中的开始位置
	unsigned long long skipSize;							//跳过多少个字节之后再搜索
	unsigned long long indexId;								//索引节点的id或者是文件的开始位置
	unsigned int targetStart;								//从目标字符串哪个位置继续搜索
};

bool SearchIndex::search()
{
	//首先进行跳过比较的部分然后
	//获取索引文件的根节点
	unsigned long long rootIndexId = indexFile.getRootIndexId();
	if (rootIndexId == 0)
	{
		return false;
	}

	std::deque<SkipStruct> skipQue;
	std::deque<SearchTask> searchTaskQue;
	std::deque<unsigned long long> indexIdQue;
	skipQue.emplace_back();
	SkipStruct& skipStruct = skipQue.back();
	skipStruct.setSkipNum(skipCharNum);
	skipStruct.setIndexId(rootIndexId);
	for (; !skipQue.empty(); skipQue.pop_front())
	{
		SkipStruct& skipStruct = skipQue.front();

		//获取节点
		IndexNode* pNode = indexFile.getIndexNode(skipStruct.getIndexId());
		if (pNode == nullptr)
		{
			return false;
		}

		//这里分几种情况
		if (skipStruct.getSkipNum() < pNode->getLen())
		{
			searchTaskQue.emplace_back();
			SearchTask& searchTask = searchTaskQue.back();
			searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
			searchTask.setSkipSize(skipStruct.getSkipNum());
			searchTask.setIndexId(skipStruct.getIndexId());
			searchTask.setTargetStart(0);
		}
		else
		{
			switch (pNode->getType())
			{
			case NODE_TYPE_ONE:
			{
				if (skipStruct.getSkipNum() < pNode->getLen() + 8)
				{
					unsigned long long subSkipNum = skipStruct.getSkipNum() - pNode->getLen();
					IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)pNode;
					std::unordered_map<unsigned long long, IndexNodeChild>& children = pTmpNode->getChildren();
					//这里分两种情况
					if (subSkipNum + targetLen <= 8)
					{
						for (auto& child : children)
						{
							const unsigned char* p = (const unsigned char*)& child.first;
							unsigned long long i = 0;
							for (; i < targetLen; ++i)
							{
								if (p[subSkipNum + i] != searchTarget[i])
								{
									break;
								}
							}

							if (i == targetLen)
							{
								if (child.second.getType() == CHILD_TYPE_LEAF)
								{
									resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 8 + skipCharNum);
								}
								else
								{
									indexIdQue.push_back(child.second.getIndexId());
								}
							}
						}
					}
					else
					{
						for (auto& child : children)
						{
							const unsigned char* p = (const unsigned char*)& child.first;
							unsigned long long i = 0;
							unsigned long long remainSize = 8 - subSkipNum;
							for (; i < remainSize; ++i)
							{
								if (p[subSkipNum + i] != searchTarget[i])
								{
									break;
								}
							}

							if (i == remainSize)
							{
								if (child.second.getType() == CHILD_TYPE_LEAF)
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
									searchTask.setSkipSize(pNode->getPreCmpLen() + pNode->getLen() + 8);
									searchTask.setIndexId(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 8);
									searchTask.setTargetStart((unsigned int)remainSize);
								}
								else
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
									searchTask.setSkipSize(0);
									searchTask.setIndexId(child.second.getIndexId());
									searchTask.setTargetStart((unsigned int)remainSize);
								}
							}
						}
					}
				}
				else
				{
					IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)pNode;
					std::unordered_map<unsigned long long, IndexNodeChild>& children = pTmpNode->getChildren();
					for (auto& child : children)
					{
						if (child.second.getType() == CHILD_TYPE_LEAF)
						{
							unsigned long long filePos = child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 8;
							unsigned long long leafLen = dstFileSize - filePos;
							if (leafLen >= pNode->getPreCmpLen() + skipStruct.getSkipNum() + targetLen)
							{
								searchTaskQue.emplace_back();
								SearchTask& searchTask = searchTaskQue.back();
								searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
								searchTask.setSkipSize(pNode->getPreCmpLen() + skipStruct.getSkipNum());
								searchTask.setIndexId(filePos);
								searchTask.setTargetStart(0);
							}
						}
						else
						{
							skipQue.emplace_back();
							SkipStruct& tmpSkipStruct = skipQue.back();
							tmpSkipStruct.setSkipNum((unsigned char)(skipStruct.getSkipNum() - pNode->getLen() - 8));
							tmpSkipStruct.setIndexId(child.second.getIndexId());
						}
					}
				}
			}
				break;
				case NODE_TYPE_TWO:
				{
					if (skipStruct.getSkipNum() < pNode->getLen() + 4)
					{
						unsigned long long subSkipNum = skipStruct.getSkipNum() - pNode->getLen();
						IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)pNode;
						std::unordered_map<unsigned int, IndexNodeChild>& children = pTmpNode->getChildren();
						//这里分两种情况
						if (subSkipNum + targetLen <= 4)
						{
							for (auto& child : children)
							{
								const unsigned char* p = (const unsigned char*)&child.first;
								unsigned long long i = 0;
								for (; i < targetLen; ++i)
								{
									if (p[subSkipNum + i] != searchTarget[i])
									{
										break;
									}
								}

								if (i == targetLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 4 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							for (auto& child : children)
							{
								const unsigned char* p = (const unsigned char*)& child.first;
								unsigned long long i = 0;
								unsigned long long remainSize = 4 - subSkipNum;
								for (; i < remainSize; ++i)
								{
									if (p[subSkipNum + i] != searchTarget[i])
									{
										break;
									}
								}

								if (i == remainSize)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + pNode->getLen() + 4);
										searchTask.setIndexId(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 4);
										searchTask.setTargetStart((unsigned int)remainSize);
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(child.second.getIndexId());
										searchTask.setTargetStart((unsigned int)remainSize);
									}
								}
							}
						}
					}
					else
					{
						IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)pNode;
						std::unordered_map<unsigned int, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								unsigned long long filePos = child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 4;
								unsigned long long leafLen = dstFileSize - filePos;
								if (leafLen >= pNode->getPreCmpLen() + skipStruct.getSkipNum() + targetLen)
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
									searchTask.setSkipSize(pNode->getPreCmpLen() + skipStruct.getSkipNum());
									searchTask.setIndexId(filePos);
									searchTask.setTargetStart(0);
								}
							}
							else
							{
								skipQue.emplace_back();
								SkipStruct& tmpSkipStruct = skipQue.back();
								tmpSkipStruct.setSkipNum((unsigned char)(skipStruct.getSkipNum() - pNode->getLen() - 4));
								tmpSkipStruct.setIndexId(child.second.getIndexId());
							}
						}
					}
				}
					break;
				case NODE_TYPE_THREE:
				{
					if (skipStruct.getSkipNum() < pNode->getLen() + 2)
					{
						unsigned long long subSkipNum = skipStruct.getSkipNum() - pNode->getLen();
						IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)pNode;
						std::unordered_map<unsigned short, IndexNodeChild>& children = pTmpNode->getChildren();
						//这里分两种情况
						if (subSkipNum + targetLen <= 2)
						{
							for (auto& child : children)
							{
								const unsigned char* p = (const unsigned char*)&child.first;
								unsigned long long i = 0;
								for (; i < targetLen; ++i)
								{
									if (p[subSkipNum + i] != searchTarget[i])
									{
										break;
									}
								}

								if (i == targetLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 2 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							for (auto& child : children)
							{
								const unsigned char* p = (const unsigned char*)& child.first;
								unsigned long long i = 0;
								unsigned long long remainSize = 2 - subSkipNum;
								for (; i < remainSize; ++i)
								{
									if (p[subSkipNum + i] != searchTarget[i])
									{
										break;
									}
								}

								if (i == remainSize)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + pNode->getLen() + 2);
										searchTask.setIndexId(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 2);
										searchTask.setTargetStart((unsigned int)remainSize);
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(child.second.getIndexId());
										searchTask.setTargetStart((unsigned int)remainSize);
									}
								}
							}
						}
					}
					else
					{
						IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)pNode;
						std::unordered_map<unsigned short, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								unsigned long long filePos = child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 2;
								unsigned long long leafLen = dstFileSize - filePos;
								if (leafLen >= pNode->getPreCmpLen() + skipStruct.getSkipNum() + targetLen)
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
									searchTask.setSkipSize(pNode->getPreCmpLen() + skipStruct.getSkipNum());
									searchTask.setIndexId(filePos);
									searchTask.setTargetStart(0);
								}
							}
							else
							{
								skipQue.emplace_back();
								SkipStruct& tmpSkipStruct = skipQue.back();
								tmpSkipStruct.setSkipNum((unsigned char)(skipStruct.getSkipNum() - pNode->getLen() - 2));
								tmpSkipStruct.setIndexId(child.second.getIndexId());
							}
						}
					}
				}
				break;
				case NODE_TYPE_FOUR:
				{
					if (skipStruct.getSkipNum() < pNode->getLen() + 1)
					{
						unsigned long long subSkipNum = skipStruct.getSkipNum() - pNode->getLen();
						IndexNodeTypeFour* pTmpNode = (IndexNodeTypeFour*)pNode;
						std::unordered_map<unsigned char, IndexNodeChild>& children = pTmpNode->getChildren();
						//这里分两种情况
						if (subSkipNum + targetLen <= 1)
						{
							for (auto& child : children)
							{
								const unsigned char* p = (const unsigned char*)&child.first;
								unsigned long long i = 0;
								for (; i < targetLen; ++i)
								{
									if (p[subSkipNum + i] != searchTarget[i])
									{
										break;
									}
								}

								if (i == targetLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 1 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							for (auto& child : children)
							{
								const unsigned char* p = (const unsigned char*)& child.first;
								unsigned long long i = 0;
								unsigned long long remainSize = 1 - subSkipNum;
								for (; i < remainSize; ++i)
								{
									if (p[subSkipNum + i] != searchTarget[i])
									{
										break;
									}
								}

								if (i == remainSize)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + pNode->getLen() + 1);
										searchTask.setIndexId(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 1);
										searchTask.setTargetStart((unsigned int)remainSize);
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(child.second.getIndexId());
										searchTask.setTargetStart((unsigned int)remainSize);
									}
								}
							}
						}
					}
					else
					{
						IndexNodeTypeFour* pTmpNode = (IndexNodeTypeFour*)pNode;
						std::unordered_map<unsigned char, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								unsigned long long filePos = child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 1;
								unsigned long long leafLen = dstFileSize - filePos;
								if (leafLen >= pNode->getPreCmpLen() + skipStruct.getSkipNum() + targetLen)
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
									searchTask.setSkipSize(pNode->getPreCmpLen() + skipStruct.getSkipNum());
									searchTask.setIndexId(filePos);
									searchTask.setTargetStart(0);
								}
							}
							else
							{
								skipQue.emplace_back();
								SkipStruct& tmpSkipStruct = skipQue.back();
								tmpSkipStruct.setSkipNum((unsigned char)(skipStruct.getSkipNum() - pNode->getLen() - 1));
								tmpSkipStruct.setIndexId(child.second.getIndexId());
							}
						}
					}
				}
					break;
				default:
					break;

			}

			//还有可能有个叶子节点的一小部分相同
			unsigned long long filePos = 0;
			if (pNode->getFirstLeafSet(&filePos))
			{
				unsigned long long leafLen = dstFileSize - filePos;
				if (leafLen > pNode->getPreCmpLen() + pNode->getLen())
				{
					unsigned long long overLen = leafLen - pNode->getPreCmpLen() - pNode->getLen();
					if (overLen < 8)
					{
						if (pNode->getPreCmpLen() + skipStruct.getSkipNum() + targetLen <= leafLen)
						{
							searchTaskQue.emplace_back();
							SearchTask& searchTask = searchTaskQue.back();
							searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
							searchTask.setSkipSize(pNode->getPreCmpLen() + skipStruct.getSkipNum());
							searchTask.setIndexId(filePos);
							searchTask.setTargetStart(0);
						}
					}
				}
			}
		}
		indexFile.putIndexNode(pNode);
	}

	unsigned char count = 0;
	for (; !searchTaskQue.empty(); searchTaskQue.pop_front())
	{
		SearchTask& searchTask = searchTaskQue.front();

		if (searchTask.getIndexIdOrStartPos() == CHILD_TYPE_LEAF)
		{
			//搜索后续的叶子节点
			unsigned long long skipSize = searchTask.getSkipSize();
			unsigned long long filePos = searchTask.getIndexId();
			unsigned int targetStart = searchTask.getTargetStart();
			unsigned int leftSearchTarget = targetLen - targetStart;
			unsigned long long leafLen = dstFileSize - filePos;
			if (leafLen - skipSize >= leftSearchTarget)
			{
				//比较文件当中相应长度的字符串看看是不是一样
				unsigned char* buffer = (unsigned char*)malloc(4 * 1024);
				if (buffer == nullptr)
				{
					return false;
				}

				unsigned int cmpLen = 0;
				for (; cmpLen + 4 * 1024 <= leftSearchTarget; cmpLen += 4 * 1024)
				{
					fpos_t pos;
					pos.__pos = filePos + skipSize + cmpLen;
					if (!dstFile.read(pos, buffer, 4 * 1024))
					{
						free(buffer);
						return false;
					}

					unsigned int i = 0;
					for (; i < 4 * 1024; i += 8)
					{
						if (*(unsigned long long*)(&buffer[i]) != *(unsigned long long*)(&searchTarget[targetStart + cmpLen + i]))
						{
							break;
						}
					}

					if (i != 4 * 1024)
					{
						break;
					}
				}

				if (cmpLen + 4 * 1024 > leftSearchTarget)
				{
					unsigned int lastNeedReadSize = leftSearchTarget - cmpLen;

					if (lastNeedReadSize != 0)
					{
						fpos_t pos;
						pos.__pos = filePos + skipSize + cmpLen;
						if (!dstFile.read(pos, buffer, lastNeedReadSize))
						{
							free(buffer);
							return false;
						}
					}

					unsigned int subCmpLen = 0;
					for (; subCmpLen + 8 <= lastNeedReadSize; subCmpLen += 8)
					{
						if (*(unsigned long long*)(&buffer[subCmpLen]) != *(unsigned long long*)(&searchTarget[targetStart + cmpLen + subCmpLen]))
						{
							break;
						}
					}

					if (subCmpLen + 8 > lastNeedReadSize)
					{
						unsigned int lastSize = lastNeedReadSize - subCmpLen;
						unsigned int suppleSize = 0;
						for (; suppleSize < lastSize; ++suppleSize)
						{
							if (*(unsigned char*)(&buffer[subCmpLen + suppleSize]) != *(unsigned char*)(&searchTarget[targetStart + cmpLen + subCmpLen + suppleSize]))
							{
								break;
							}
						}

						if (suppleSize == lastSize)
						{
							resultSet->insert(filePos + skipCharNum);
						}
					}
				}

				free(buffer);
			}

		}
		else
		{
			//搜索后续的非叶子节点
			unsigned long long skipSize = searchTask.getSkipSize();
			unsigned long long indexId = searchTask.getIndexId();
			unsigned int targetStart = searchTask.getTargetStart();

			//获取节点
			IndexNode* pNode = indexFile.getIndexNode(indexId);
			if (pNode == nullptr)
			{
				return false;
			}

			unsigned long long filePos = pNode->getStart();
			unsigned long long nodeLen = pNode->getLen();
			unsigned int leftSearchTarget = targetLen - targetStart;
			unsigned long long remainReadSize = leftSearchTarget;
			if (nodeLen - skipSize < remainReadSize)
			{
				remainReadSize = nodeLen - skipSize;
			}

			bool isSameHead = false;
			//比较文件当中相应长度的字符串看看是不是一样
			unsigned char* buffer = (unsigned char*)malloc(4 * 1024);
			if (buffer == nullptr)
			{
				indexFile.putIndexNode(pNode);
				return false;
			}

			unsigned int cmpLen = 0;
			for (; cmpLen + 4 * 1024 <= remainReadSize; cmpLen += 4 * 1024)
			{
				fpos_t pos;
				pos.__pos = filePos + skipSize + cmpLen;
				if (!dstFile.read(pos, buffer, 4 * 1024))
				{
					indexFile.putIndexNode(pNode);
					free(buffer);
					return false;
				}

				unsigned int i = 0;
				for (; i < 4 * 1024; i += 8)
				{
					if (*(unsigned long long*)(&buffer[i]) != *(unsigned long long*)(&searchTarget[targetStart + cmpLen + i]))
					{
						break;
					}
				}

				if (i != 4 * 1024)
				{
					break;
				}
			}

			if (cmpLen + 4 * 1024 > remainReadSize)
			{
				unsigned long long lastNeedReadSize = remainReadSize - cmpLen;

				if (lastNeedReadSize != 0)
				{
					fpos_t pos;
					pos.__pos = filePos + skipSize + cmpLen;
					if (!dstFile.read(pos, buffer, lastNeedReadSize))
					{
						free(buffer);
						indexFile.putIndexNode(pNode);
						return false;
					}
				}

				unsigned long long subCmpLen = 0;
				for (; subCmpLen + 8 <= lastNeedReadSize; subCmpLen += 8)
				{
					if (*(unsigned long long*)(&buffer[subCmpLen]) != *(unsigned long long*)(&searchTarget[targetStart + cmpLen + subCmpLen]))
					{
						break;
					}
				}

				if (subCmpLen + 8 > lastNeedReadSize)
				{
					unsigned long long lastSize = lastNeedReadSize - subCmpLen;
					unsigned long long suppleSize = 0;
					for (; suppleSize < lastSize; ++suppleSize)
					{
						if (*(unsigned char*)(&buffer[subCmpLen + suppleSize]) != *(unsigned char*)(&searchTarget[targetStart + cmpLen + subCmpLen + suppleSize]))
						{
							break;
						}
					}

					if (suppleSize == lastSize)
					{
						isSameHead = true;
					}
				}
			}

			if (isSameHead)
			{
				//前面的字节完全一样接下来分二种情况考虑
				if (skipSize + leftSearchTarget <= nodeLen)
				{
					pNode->addLeafPosToResult(skipSize + leftSearchTarget, skipCharNum, dstFileSize, *resultSet);
					switch (pNode->getType())
					{
					case NODE_TYPE_ONE:
					{
						IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)pNode;
						std::unordered_map<unsigned long long, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 8 + skipCharNum);
							}
							else
							{
								indexIdQue.push_back(child.second.getIndexId());
							}
						}
					}
						break;
					case NODE_TYPE_TWO:
					{
						IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)pNode;
						std::unordered_map<unsigned int, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 4 + skipCharNum);
							}
							else
							{
								indexIdQue.push_back(child.second.getIndexId());
							}
						}
					}
						break;
					case NODE_TYPE_THREE:
					{
						IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)pNode;
						std::unordered_map<unsigned short, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 2 + skipCharNum);
							}
							else
							{
								indexIdQue.push_back(child.second.getIndexId());
							}
						}
					}
						break;
					case NODE_TYPE_FOUR:
					{
						IndexNodeTypeFour* pTmpNode = (IndexNodeTypeFour*)pNode;
						std::unordered_map<unsigned char, IndexNodeChild>& children = pTmpNode->getChildren();
						for (auto& child : children)
						{
							if (child.second.getType() == CHILD_TYPE_LEAF)
							{
								resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 1 + skipCharNum);
							}
							else
							{
								indexIdQue.push_back(child.second.getIndexId());
							}
						}

					}
						break;
					default:
						break;
					}
				}
				else
				{
					switch (pNode->getType())
					{
					case NODE_TYPE_ONE:
					{
						IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)pNode;
						if (skipSize + leftSearchTarget < nodeLen + 8)
						{
							std::unordered_map<unsigned long long, IndexNodeChild>& children = pTmpNode->getChildren();
							for (auto child : children)
							{
								unsigned char* p = (unsigned char*)& child.first;
								unsigned long long needCmpLen = skipSize + leftSearchTarget - nodeLen;
								unsigned long long i = 0;
								for (; i < needCmpLen; ++i)
								{
									if (p[i] != searchTarget[targetStart + nodeLen - skipSize + i])
									{
										break;
									}
								}

								if (i == needCmpLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 8 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							IndexNodeChild* indexNodeChild = pTmpNode->getIndexNodeChild(*(unsigned long long*)(&searchTarget[targetStart + nodeLen - skipSize]));
							if (indexNodeChild != nullptr)
							{
								if (skipSize + leftSearchTarget == nodeLen + 8)
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 8 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(indexNodeChild->getIndexId());
									}
								}
								else
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + nodeLen + 8);
										searchTask.setIndexId(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 8);
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 8));
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(indexNodeChild->getIndexId());
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 8));
									}
								}
							}
						}
					}
						break;
					case NODE_TYPE_TWO:
					{
						IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)pNode;
						if (skipSize + leftSearchTarget < nodeLen + 4)
						{
							std::unordered_map<unsigned int, IndexNodeChild>& children = pTmpNode->getChildren();
							for (auto child : children)
							{
								unsigned char* p = (unsigned char*)& child.first;
								unsigned long long needCmpLen = skipSize + leftSearchTarget - nodeLen;
								unsigned long long i = 0;
								for (; i < needCmpLen; ++i)
								{
									if (p[i] != searchTarget[targetStart + nodeLen - skipSize + i])
									{
										break;
									}
								}

								if (i == needCmpLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 4 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							IndexNodeChild* indexNodeChild = pTmpNode->getIndexNodeChild(*(unsigned int*)(&searchTarget[targetStart + nodeLen - skipSize]));
							if (indexNodeChild != nullptr)
							{
								if (skipSize + leftSearchTarget == nodeLen + 4)
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 4 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(indexNodeChild->getIndexId());
									}
								}
								else
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + nodeLen + 4);
										searchTask.setIndexId(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 4);
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 4));
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(indexNodeChild->getIndexId());
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 4));
									}
								}
							}
						}
					}
						break;
					case NODE_TYPE_THREE:
					{
						IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)pNode;
						if (skipSize + leftSearchTarget < nodeLen + 2)
						{
							std::unordered_map<unsigned short, IndexNodeChild>& children = pTmpNode->getChildren();
							for (auto child : children)
							{
								unsigned char* p = (unsigned char*)& child.first;
								unsigned long long needCmpLen = skipSize + leftSearchTarget - nodeLen;
								unsigned long long i = 0;
								for (; i < needCmpLen; ++i)
								{
									if (p[i] != searchTarget[targetStart + nodeLen - skipSize + i])
									{
										break;
									}
								}

								if (i == needCmpLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 2 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							IndexNodeChild* indexNodeChild = pTmpNode->getIndexNodeChild(*(unsigned short*)(&searchTarget[targetStart + nodeLen - skipSize]));
							if (indexNodeChild != nullptr)
							{
								if (skipSize + leftSearchTarget == nodeLen + 2)
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 2 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(indexNodeChild->getIndexId());
									}
								}
								else
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + nodeLen + 2);
										searchTask.setIndexId(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 2);
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 2));
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(indexNodeChild->getIndexId());
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 2));
									}
								}
							}
						}
					}
						break;
					case NODE_TYPE_FOUR:
					{
						IndexNodeTypeFour* pTmpNode = (IndexNodeTypeFour*)pNode;
						if (skipSize + leftSearchTarget < nodeLen + 1)
						{
							std::unordered_map<unsigned char, IndexNodeChild>& children = pTmpNode->getChildren();
							for (auto child : children)
							{
								unsigned char* p = (unsigned char*)& child.first;
								unsigned long long needCmpLen = skipSize + leftSearchTarget - nodeLen;
								unsigned long long i = 0;
								for (; i < needCmpLen; ++i)
								{
									if (p[i] != searchTarget[targetStart + nodeLen - skipSize + i])
									{
										break;
									}
								}

								if (i == needCmpLen)
								{
									if (child.second.getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - nodeLen - 1 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(child.second.getIndexId());
									}
								}
							}
						}
						else
						{
							IndexNodeChild* indexNodeChild = pTmpNode->getIndexNodeChild(*(unsigned char*)(&searchTarget[targetStart + nodeLen - skipSize]));
							if (indexNodeChild != nullptr)
							{
								if (skipSize + leftSearchTarget == nodeLen + 1)
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										resultSet->insert(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 1 + skipCharNum);
									}
									else
									{
										indexIdQue.push_back(indexNodeChild->getIndexId());
									}
								}
								else
								{
									if (indexNodeChild->getType() == CHILD_TYPE_LEAF)
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
										searchTask.setSkipSize(pNode->getPreCmpLen() + nodeLen + 1);
										searchTask.setIndexId(indexNodeChild->getIndexId() - pNode->getPreCmpLen() - nodeLen - 1);
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 1));
									}
									else
									{
										searchTaskQue.emplace_back();
										SearchTask& searchTask = searchTaskQue.back();
										searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
										searchTask.setSkipSize(0);
										searchTask.setIndexId(indexNodeChild->getIndexId());
										searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize + 1));
									}
								}
							}
						}
					}
						break;
					default:
						break;
					}
					//还有一种叶子节点是在leafset里面比覆盖了节点长度但是还不到8个字节的这里也要搜索下
					unsigned long long filePos = 0;
					if (pNode->getFirstLeafSet(&filePos))
					{
						unsigned long long leafLen = dstFileSize - filePos;
						if (leafLen > pNode->getPreCmpLen() + pNode->getLen())
						{
							unsigned long long overLen = leafLen - pNode->getPreCmpLen() - nodeLen;
							if (overLen < 8)
							{
								if (pNode->getPreCmpLen() + skipSize + leftSearchTarget <= leafLen)
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_LEAF);
									searchTask.setSkipSize(pNode->getPreCmpLen() + nodeLen);
									searchTask.setIndexId(filePos);
									searchTask.setTargetStart((unsigned int)(targetStart + nodeLen - skipSize));
								}
							}
						}
					}
				}
			}
			free(buffer);
			indexFile.putIndexNode(pNode);

			++count;
			if (count % 8 == 0)
			{
				//搜索几次再对缓存进行处理
				indexFile.reduceCache();
			}
		}
	}

	count = 0;
	for (; !indexIdQue.empty(); indexIdQue.pop_front())
	{
		unsigned long long indexId = indexIdQue.front();
		IndexNode* pNode = indexFile.getIndexNode(indexId);
		if (pNode == nullptr)
		{
			return false;
		}

		//将所有的leafSet中的叶子节点添加到结果当中因为全都是有效的
		pNode->addLeafPosToResult(0, skipCharNum, dstFileSize, *resultSet);

		//把所有的叶子节点也加入
		switch (pNode->getType())
		{
		case NODE_TYPE_ONE:
		{
			IndexNodeTypeOne* pTmpNode = (IndexNodeTypeOne*)pNode;
			std::unordered_map<unsigned long long, IndexNodeChild>& children = pTmpNode->getChildren();
			for (auto& child : children)
			{
				if (child.second.getType() == CHILD_TYPE_LEAF)
				{
					resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 8 + skipCharNum);
				}
				else
				{
					indexIdQue.push_back(child.second.getIndexId());
				}
			}
		}
			break;
		case NODE_TYPE_TWO:
		{
			IndexNodeTypeTwo* pTmpNode = (IndexNodeTypeTwo*)pNode;
			std::unordered_map<unsigned int, IndexNodeChild>& children = pTmpNode->getChildren();
			for (auto& child : children)
			{
				if (child.second.getType() == CHILD_TYPE_LEAF)
				{
					resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 4 + skipCharNum);
				}
				else
				{
					indexIdQue.push_back(child.second.getIndexId());
				}
			}
		}
			break;
		case NODE_TYPE_THREE:
		{
			IndexNodeTypeThree* pTmpNode = (IndexNodeTypeThree*)pNode;
			std::unordered_map<unsigned short, IndexNodeChild>& children = pTmpNode->getChildren();
			for (auto& child : children)
			{
				if (child.second.getType() == CHILD_TYPE_LEAF)
				{
					resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 2 + skipCharNum);
				}
				else
				{
					indexIdQue.push_back(child.second.getIndexId());
				}
			}
		}
			break;
		case NODE_TYPE_FOUR:
		{
			IndexNodeTypeFour* pTmpNode = (IndexNodeTypeFour*)pNode;
			std::unordered_map<unsigned char, IndexNodeChild>& children = pTmpNode->getChildren();
			for (auto& child : children)
			{
				if (child.second.getType() == CHILD_TYPE_LEAF)
				{
					resultSet->insert(child.second.getIndexId() - pNode->getPreCmpLen() - pNode->getLen() - 1 + skipCharNum);
				}
				else
				{
					indexIdQue.push_back(child.second.getIndexId());
				}
			}
		}
		default:
			break;
		}

		indexFile.putIndexNode(pNode);

		++count;
		if (count % 8 == 0)
		{
			//搜索几次再对缓存进行处理
			indexFile.reduceCache();
		}
	}

	return true;
}
