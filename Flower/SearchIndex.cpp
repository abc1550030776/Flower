#include "SearchIndex.h"
#include "memory.h"
#include "common.h"
#include <sys/stat.h>
#include <deque>

SearchIndex::SearchIndex()
{
	searchTarget = nullptr;
	targetLen = 0;
	resultSet = 0;
	dstFileSize = 0;
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
	if (!GetIndexPath(fileName, indexFileName))
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

	void setIndexId(unsigned char indexId)
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
										if (subSkipNum + targetLen <= overLen)
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
									searchTask.setTargetStart(remainSize);
								}
								else
								{
									searchTaskQue.emplace_back();
									SearchTask& searchTask = searchTaskQue.back();
									searchTask.setIndexIdOrStartPos(CHILD_TYPE_NODE);
									searchTask.setSkipSize(0);
									searchTask.setIndexId(child.second.getIndexId());
									searchTask.setTargetStart(remainSize);
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
							SkipStruct& skipStruct = skipQue.back();
							skipStruct.setSkipNum(skipStruct.getSkipNum() - pNode->getLen() - 8);
							skipStruct.setIndexId(child.second.getIndexId());
						}
					}
				}
			}
				break;
			}
		}
		indexFile.putIndexNode(pNode);
	}

	return true;
}
