#include "Index.h"

IndexNode* Index::getIndexNode(unsigned long long indexId)
{
	auto it = indexNodeCache.find(indexId);
	if (it == end(indexNodeCache))
	{
		return NULL;
	}

	return it->second;
}