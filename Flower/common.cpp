#include "common.h"
#include <string.h>

bool GetIndexPath(const char* dstFilePath, char* indexPath)
{
	//判断路劲长度加上后缀了以后是否会超过最长长度
	size_t len = strlen(dstFilePath);
	if (len + 5 > 4096)
	{
		return false;
	}

	strcpy(indexPath, dstFilePath);

	strcpy(indexPath + len, ".idx");

	return true;
}

bool compareTwoType(const unsigned char leftType, const unsigned char rightType)
{
	//比较孩子节点key值大小,左边小的化返回true
	if (leftType > rightType)
	{
		return true;
	}
	return false;
}