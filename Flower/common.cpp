#include "common.h"
#include <string.h>

bool GetIndexPath(const char* dstFilePath, char* indexPath)
{
	//�ж�·�����ȼ��Ϻ�׺���Ժ��Ƿ�ᳬ�������
	size_t len = strlen(dstFilePath);
	if (len + 5 > 4096)
	{
		return false;
	}

	strcpy(indexPath, dstFilePath);

	strcpy(indexPath + len, ".idx");

	return true;
}